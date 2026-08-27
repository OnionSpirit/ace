#pragma once
/// @file arena.h
/// @brief Thread-local framework allocator with cross-arena chunk recycling.
///
/// @details Arena-per-thread allocator shared by coroutine frames, I/O buffers,
/// and framework containers.
///
/// Chunks up to @c kMaxSize bytes are served from a
/// @c std::pmr::unsynchronized_pool_resource backed by a custom memory resource.
/// The pool retains freed chunks in its
/// free lists and returns its blocks to the system only when the arena — a
/// thread-local singleton destroyed at thread exit — is destroyed.
///
/// Requests larger than @c kMaxSize are served by @c malloc() directly and are
/// freed back to the system immediately on deallocation (transient chunks).
/// Transient chunks are never pooled and never routed through the transfer
/// channel.
///
/// The chunk header stores a pointer to the owner's @c extern_release context.
/// A pooled chunk deallocated on a foreign thread is handed back through its
/// @c nukes::dynamic::mpsc_queue.  A transient chunk is freed immediately and
/// atomically reports its size through @c extern_release::_released_bytes; the
/// owner subtracts all reported bytes while processing the release context.
/// The owner drains its channel according to the utilization formula:
///     N = max_allocation_size / occupied_bytes
/// every N-th allocation/deallocation operation drains the channel; with no
/// application-wide limit configured (0) the channel is drained on every
/// allocation operation.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <new>
#include <type_traits>

#include <nukes/dynamic/mpsc_queue.h>

#include "ace/core/config.h"
#include "ace/core/tools/macro.h"

namespace ace::core {

    struct extern_release_debug;
    struct extern_release_release;

    /// @brief Build-specific external release state used by chunk headers.
    using extern_release = std::conditional_t<is_debug, extern_release_debug, extern_release_release>;

    /// @brief Per-chunk header: owner release context + size/flags.
    struct chunk_header {
        extern_release* _release { nullptr };
        std::size_t _size { 0 };
    };

    static_assert(sizeof(chunk_header) == 16);

    /// @brief State accessed by foreign threads when they release arena-owned chunks.
    struct extern_release_base {
        nukes::dynamic::mpsc_queue<chunk_header*> _channel;
        std::atomic<std::size_t> _released_bytes { 0 };
    };

    /// @brief Debug build release state with exact transient allocation accounting.
    struct extern_release_debug : extern_release_base {
        std::atomic<std::size_t> _malloc_count { 0 };
    };

    /// @brief Release build state without debug-only transient allocation accounting.
    struct extern_release_release : extern_release_base {};

    /**
     * @brief Debug-only observability counters (empty in release builds).
     *
     * @details Enabled in debug builds only.  Tests are built with
     * @c debug=true (no NDEBUG), which maps to @c is_debug == true.
     */
    template <bool Enabled>
    struct arena_stats {
        void note_pool_allocate(std::size_t) noexcept {}
        void note_pool_deallocate(std::size_t) noexcept {}
        void note_drain() noexcept {}
        [[nodiscard]] std::size_t pool_held() const noexcept { return 0; }
        [[nodiscard]] std::size_t drains() const noexcept { return 0; }
    };

    /// @brief Stats specialization: real counters in debug builds.
    template <>
    struct arena_stats<true> {
        std::size_t pool_held_bytes = 0;  ///< System bytes currently retained by the pmr pool.
        std::size_t drain_count     = 0;  ///< Channel drains performed.

        void note_pool_allocate(std::size_t bytes) noexcept { pool_held_bytes += bytes; }
        void note_pool_deallocate(std::size_t bytes) noexcept { pool_held_bytes -= bytes; }
        void note_drain() noexcept { ++drain_count; }
        [[nodiscard]] std::size_t pool_held() const noexcept { return pool_held_bytes; }
        [[nodiscard]] std::size_t drains() const noexcept { return drain_count; }
    };

    /**
     * @brief Thread-local arena shared by framework allocations.
     *
     * @details All arena state is touched from the owning thread only, except
     * @c extern_release, whose queue and counters are accessed atomically by
     * foreign threads.
     */
    struct arena : arena_stats<is_debug> {

        /// @brief Largest total chunk size served from the pmr pool.
        static constexpr std::size_t kMaxSize = 4096;

        /// @brief Size of the per-chunk header stored before the user payload.
        static constexpr std::size_t kHeaderSize = sizeof(chunk_header);

        /// @brief Guaranteed alignment of returned storage.
        static constexpr std::size_t kAlignment = 16;

        /// @brief Bit 63 of the header size field marks a transient (malloc-served) chunk.
        static constexpr std::size_t kTransientFlag = std::size_t{1} << 63;

        /// @brief Returns the thread-local arena singleton.
        static arena& get_instance() {
            static thread_local arena instance;
            return instance;
        }

        /**
         * @brief Allocates @p size bytes from the current thread's arena.
         * @param size Requested payload size.
         * @return Pointer to the user payload area (chunk header + 16 bytes).
         * @throws std::bad_alloc on allocation failure or when the arena limit
         *         is reached and @c breach_memory_limit is disabled.
         */
        [[nodiscard]] void* allocate(std::size_t size) {
            maybe_drain(true);
            if (size > std::numeric_limits<std::size_t>::max() - kHeaderSize)
                throw std::bad_alloc();
            const auto with_header = size + kHeaderSize;
            if (with_header > std::numeric_limits<std::size_t>::max() - (kAlignment - 1))
                throw std::bad_alloc();
            const auto total = align_up(with_header, kAlignment);
            auto* chunk = obtain_chunk(total);
            _occupied += total;
            return chunk + 1;
        }

        /**
         * @brief Allocates uninitialised storage for @p count objects of @p data_t.
         * @throws std::bad_array_new_length when the element count overflows.
         * @throws std::bad_alloc when the arena cannot allocate the storage.
         */
        template <typename data_t>
        [[nodiscard]] data_t* allocate_as(std::size_t count = 1) {
            static_assert(alignof(data_t) <= kAlignment,
                "ace::core::arena does not support over-aligned types");
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(data_t))
                throw std::bad_array_new_length();
            return static_cast<data_t*>(allocate(sizeof(data_t) * count));
        }

        /**
         * @brief Returns an allocation to its owning arena.
         * @param mem_ptr Pointer returned by @c allocate().
         * @details The unnamed size argument is unused because the chunk size is
         * read from the allocation header.
         */
        void deallocate(void* mem_ptr, std::size_t) noexcept {
            if (not mem_ptr) return;
            auto* chunk = static_cast<chunk_header*>(mem_ptr) - 1;
            const auto flags = chunk->_size;
            const auto size = chunk_size_of(flags);
            auto* release = chunk->_release;
            if (is_transient(flags)) {
                // NOTE: Transient chunks never travel through the channel — free immediately.
                // Foreign frees report their bytes atomically; the owner subtracts
                // them from _occupied on its next channel drain.
                if (release == &_extern_release)
                    _occupied -= size;
                else
                    release->_released_bytes.fetch_add(size, std::memory_order_relaxed);
                note_malloc_deallocate(*release);
                std::free(chunk);
                if constexpr (is_debug) {
                    live_system_chunks.fetch_sub(1, std::memory_order_relaxed);
                }
            } else if (release != &_extern_release) {
                // NOTE: Pooled chunk owned by another arena — hand it back through its channel.
                if (not release->_channel.push(std::move(chunk)))
                    std::terminate();
            } else {
                // NOTE: Local pooled chunk — back to the pool free list (never to the system).
                _small_pool.deallocate(chunk, size);
                _occupied -= size;
            }
            maybe_drain(false);
        }

        /** @brief Deallocates storage returned by @c allocate_as(). */
        template <typename data_t>
        void deallocate_as(data_t* mem_ptr, std::size_t count = 1) noexcept {
            (void)count;
            deallocate(mem_ptr, 0);
        }

        /**
         * @brief Snapshot of the current arena state (meaningful in debug builds).
         */
        struct stats_view {
            std::size_t in_use_bytes;      ///< Bytes currently lent to users (pool + transient).
            std::size_t pool_held_bytes;   ///< System bytes retained by the pmr pool.
            std::size_t malloc_count;      ///< Outstanding transient malloc chunks.
            std::size_t op_counter;        ///< Operation counter (utilization formula).
            std::size_t drain_count;       ///< Channel drains performed.
            std::size_t live_system_chunks;///< Process-wide chunks held by all arenas.
        };

        /**
         * @brief Current arena statistics. Debug-only fields are zero on release builds.
         */
        [[nodiscard]] stats_view stats() const noexcept {
            return stats_view {
                _occupied,
                pool_held(),
                malloc_count_of(_extern_release),
                _op_counter,
                drains(),
                live_system_chunks.load(std::memory_order_relaxed),
            };
        }

        /// @brief Process-wide count of system chunks currently held by all arenas (debug builds).
        static inline std::atomic<std::size_t> live_system_chunks { 0 };

    private:

        template <typename release_t>
        static void note_malloc_allocate(release_t& release) noexcept {
            if constexpr (std::is_same_v<std::remove_cvref_t<release_t>, extern_release_debug>)
                release._malloc_count.fetch_add(1, std::memory_order_relaxed);
        }

        template <typename release_t>
        static void note_malloc_deallocate(release_t& release) noexcept {
            if constexpr (std::is_same_v<std::remove_cvref_t<release_t>, extern_release_debug>)
                release._malloc_count.fetch_sub(1, std::memory_order_relaxed);
        }

        template <typename release_t>
        [[nodiscard]] static std::size_t malloc_count_of(const release_t& release) noexcept {
            if constexpr (std::is_same_v<std::remove_cvref_t<release_t>, extern_release_debug>)
                return release._malloc_count.load(std::memory_order_relaxed);
            else
                return 0;
        }

        /// @brief Rounds @p value up to a multiple of @p alignment (power of two).
        static std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        /// @brief Real chunk size from the header flags word.
        static std::size_t chunk_size_of(std::size_t flags) noexcept {
            return flags & ~kTransientFlag;
        }

        /// @brief Whether the chunk was served by @c malloc() (transient).
        static bool is_transient(std::size_t flags) noexcept {
            return (flags & kTransientFlag) != 0;
        }

        /// @brief Per-arena memory limit: application-wide limit divided by the runner count.
        [[nodiscard]] std::size_t arena_limit() const noexcept {
            const auto max = cfg::g_config._max_allocation_size;
            if (max == 0) return 0;
            const auto runners = cfg::g_config._runners_amount;
            return max / (runners == 0 ? 1 : runners);
        }

        /**
         * @brief Whether the arena has reached its limit and the allocation must
         *        be served as a transient malloc (or rejected).
         * @return @c true when the limit is reached and the fallback is enabled.
         * @throws std::bad_alloc when the limit is reached and the fallback is disabled.
         */
        [[nodiscard]] bool breach_required(std::size_t requested) {
            const auto max = cfg::g_config._max_allocation_size;
            if (max == 0) return false;
            const auto limit = arena_limit();
            if (requested <= limit and _occupied <= limit - requested) return false;
            if (cfg::g_config._breach_memory_limit) {
                if (not _breach_notified) {
                    _breach_notified = true;
                    std::cerr << "ace: arena limit reached (" << limit
                              << " bytes); falling back to malloc" << std::endl;
                }
                return true;
            }
            throw std::bad_alloc();
        }

        /**
         * @brief Obtains a raw chunk of @p total bytes (pool or transient malloc)
         *        and fills its header.
         */
        [[nodiscard]] chunk_header* obtain_chunk(std::size_t total) {
            const auto transient = (total > kMaxSize) or breach_required(total);
            void* mem = nullptr;
            if (transient) {
                mem = std::malloc(total);
                if (not mem) throw std::bad_alloc();
                note_malloc_allocate(_extern_release);
                if constexpr (is_debug) {
                    live_system_chunks.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                mem = _small_pool.allocate(total);
                if (mem == nullptr) throw std::bad_alloc();
            }
            auto* chunk = static_cast<chunk_header*>(mem);
            chunk->_release = &_extern_release;
            chunk->_size = transient ? (total | kTransientFlag) : total;
            return chunk;
        }

        /**
         * @brief Decides whether the incoming channel must be drained on this operation.
         * @details Utilization formula: N = max_allocation_size / occupied_bytes;
         * every N-th operation (allocation or deallocation) drains the channel.
         * With no application-wide limit (0) or with nothing occupied the channel
         * is drained on every allocation operation.
         */
        void maybe_drain(const bool is_alloc) {
            const auto max = cfg::g_config._max_allocation_size;
            bool drain = false;
            if (max == 0 or _occupied == 0)
                drain = is_alloc;
            else {
                auto N = max / _occupied;
                if (N == 0) N = 1;
                drain = (++_op_counter % N) == 0;
            }
            if (drain) drain_channel();
        }

        /**
         * @brief Drains the incoming channel, returning every chunk to the pool.
         * @details First accounts for transient chunks freed by foreign threads,
         * then returns pooled chunks from the external release channel. Uses the
         * plain per-node @c pop() loop — @c pop_batch() iterates from the queue
         * dummy node and yields garbage on the first dereference, so it is
         * unusable for batch consumption.
         */
        void drain_channel() {
            note_drain();
            _occupied -= _extern_release._released_bytes.exchange(0, std::memory_order_relaxed);
            chunk_header* chunk = nullptr;
            while (_extern_release._channel.pop(chunk)) {
                const auto size = chunk_size_of(chunk->_size);
                _occupied -= size;
                _small_pool.deallocate(chunk, size);
            }
        }

        /// @brief PMR resource backing the pool: allocates from the system, and
        ///        actually deallocates so that the arena destructor returns all
        ///        pool blocks to the system.
        struct memory_controller : std::pmr::memory_resource {

            arena* _arena { nullptr };

            explicit memory_controller(arena* arena)
                : _arena(arena) {}

            void* do_allocate(std::size_t bytes, std::size_t alignment) override {
                if constexpr (is_debug) {
                    _arena->note_pool_allocate(bytes);
                    live_system_chunks.fetch_add(1, std::memory_order_relaxed);
                }
                return std::pmr::new_delete_resource()->allocate(bytes, alignment);
            }

            void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
                if constexpr (is_debug) {
                    _arena->note_pool_deallocate(bytes);
                    live_system_chunks.fetch_sub(1, std::memory_order_relaxed);
                }
                std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
            }

            [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
                return this == &other;
            }
        };

        /// @brief Destructor: returns foreign-freed chunks to the pool; the pool
        ///        itself releases all its blocks to the system on destruction.
        ~arena() {
            drain_channel();
        }

        extern_release _extern_release;                                 ///< Cross-thread release state.
        std::size_t _occupied { 0 };                                    ///< Bytes in use (pool + transient).
        std::size_t _op_counter { 0 };                                  ///< Operation counter.
        bool _breach_notified { false };                                ///< One-time breach notice.
        memory_controller _controller { this };                         ///< Backing memory resource.
        std::pmr::unsynchronized_pool_resource _small_pool { &_controller }; ///< Small-buffer pool.
    };

    /**
     * @brief Stateless standard-container allocator backed by the current
     * thread's @c ace::core::arena. Element alignment must not exceed
     * @c arena::kAlignment.
     *
     * @details Allocation uses the calling thread's arena. Deallocation may
     * happen on another thread because every chunk records its owner and is
     * routed back through the arena's external-release channel.
     */
    template <typename data_t>
    struct arena_allocator {
        using value_type = data_t;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;

        arena_allocator() noexcept = default;

        template <typename other_t>
        arena_allocator(const arena_allocator<other_t>&) noexcept {}

        [[nodiscard]] data_t* allocate(std::size_t count) {
            return arena::get_instance().template allocate_as<data_t>(count);
        }

        void deallocate(data_t* ptr, std::size_t count) noexcept {
            arena::get_instance().template deallocate_as<data_t>(ptr, count);
        }

        template <typename other_t>
        [[nodiscard]] bool operator==(const arena_allocator<other_t>&) const noexcept {
            return true;
        }
    };

    /**
     * @brief Thread-safe process-lifetime storage for Nukes queue nodes.
     *
     * @details Dynamic Nukes queues can be static and therefore outlive a
     * thread-local @c arena.  This dedicated synchronized pool deliberately
     * lives until process termination, preventing global queue teardown from
     * observing storage released by a departed owner thread.
     */
    class nukes_node_arena {
    public:
        [[nodiscard]] static void* allocate(
            const std::size_t bytes, const std::size_t alignment)
        {
            auto* const storage = resource().allocate(bytes, alignment);
            _outstanding_bytes.fetch_add(bytes, std::memory_order_relaxed);
            return storage;
        }

        static void deallocate(
            void* storage, const std::size_t bytes, const std::size_t alignment) noexcept
        {
            if (not storage)
                return;
            resource().deallocate(storage, bytes, alignment);
            _outstanding_bytes.fetch_sub(bytes, std::memory_order_relaxed);
        }

        /// @brief Bytes currently checked out to live Nukes nodes.
        [[nodiscard]] static std::size_t outstanding_bytes() noexcept {
            return _outstanding_bytes.load(std::memory_order_relaxed);
        }

    private:
        static inline std::atomic<std::size_t> _outstanding_bytes { 0 };

        [[nodiscard]] static std::pmr::synchronized_pool_resource& resource() {
            // Intentionally never destroyed: static queues may run their
            // destructors after thread-local arenas have already been torn down.
            static auto* instance = new std::pmr::synchronized_pool_resource {};
            return *instance;
        }
    };

    /**
     * @brief Standard allocator adapter for process-lifetime Nukes node storage.
     * @tparam data_t Element type requested by a standard allocator consumer.
     */
    template <typename data_t>
    struct nukes_node_allocator {
        using value_type = data_t;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        using propagate_on_container_swap = std::true_type;

        nukes_node_allocator() noexcept = default;

        template <typename other_t>
        nukes_node_allocator(const nukes_node_allocator<other_t>&) noexcept {}

        [[nodiscard]] data_t* allocate(const std::size_t count) {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(data_t))
                throw std::bad_array_new_length();
            return static_cast<data_t*>(nukes_node_arena::allocate(
                sizeof(data_t) * count, alignof(data_t)));
        }

        void deallocate(data_t* ptr, const std::size_t count) noexcept {
            nukes_node_arena::deallocate(ptr, sizeof(data_t) * count, alignof(data_t));
        }

        [[nodiscard]] static void* allocate_bytes(
            const std::size_t bytes, const std::size_t alignment)
        {
            return nukes_node_arena::allocate(bytes, alignment);
        }

        static void deallocate_bytes(
            void* storage, const std::size_t bytes, const std::size_t alignment) noexcept
        {
            nukes_node_arena::deallocate(storage, bytes, alignment);
        }

        template <typename other_t>
        [[nodiscard]] bool operator==(const nukes_node_allocator<other_t>&) const noexcept {
            return true;
        }
    };

    [[nodiscard]] inline bool configure_nukes_node_allocator() noexcept {
        static const bool configured = nukes::detail::nodes::node_allocation::get_instance()
            .template set_allocator<nukes_node_allocator>();
        return configured;
    }

    // Configure before main() and before any framework queue can allocate its
    // dummy node.  Direct Nukes clients must still configure their own backend
    // before their first allocation.
    [[maybe_unused]] inline const bool nukes_node_allocator_configured =
        configure_nukes_node_allocator();

} // namespace ace::core
