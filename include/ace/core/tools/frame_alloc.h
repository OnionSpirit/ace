#pragma once
/// @file frame_alloc.h
/// @brief Thread-local coroutine frame allocator with cross-arena chunk recycling.
///
/// @details Arena-per-thread allocator used by @c promise_traits::operator new/delete.
///
/// Chunks up to @c kMaxSize bytes are served from a
/// @c std::pmr::unsynchronized_pool_resource backed by a custom memory resource
/// (same structure as @c iovec_alloc.h).  The pool retains freed chunks in its
/// free lists and returns its blocks to the system only when the arena — a
/// thread-local singleton destroyed at thread exit — is destroyed.
///
/// Requests larger than @c kMaxSize are served by @c malloc() directly and are
/// freed back to the system immediately on deallocation (transient chunks).
/// Transient chunks are never pooled and never routed through the transfer
/// channel.
///
/// A pooled chunk deallocated on a foreign thread is handed back to its owning
/// arena through the owner's @c nukes::dynamic::mpsc_queue; the pointer to that
/// channel is stored in the chunk header, so the allocator can tell whether the
/// chunk belongs to the current arena.  The owner drains its channel according
/// to the utilization formula:
///     N = max_allocation_size / occupied_bytes
/// every N-th allocation/deallocation operation drains the channel; with no
/// application-wide limit configured (0) the channel is drained on every
/// allocation operation.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory_resource>
#include <new>

#include <nukes/dynamic/mpsc_queue.h>

#include "ace/core/config.h"
#include "ace/core/tools/macro.h"

namespace ace::core::tools {

    /**
     * @brief Debug-only observability counters (empty in release builds).
     *
     * @details Enabled in debug builds only.  Tests are built with
     * @c debug=true (no NDEBUG), which maps to @c is_debug == false — note the
     * inverted naming of the flag in @c macro.h (is_debug == true means RELEASE).
     */
    template <bool Enabled>
    struct frame_alloc_stats {};

    /// @brief Stats specialization: real counters in debug builds.
    template <>
    struct frame_alloc_stats<true> {
        std::size_t pool_held_bytes = 0;  ///< System bytes currently retained by the pmr pool.
        std::size_t malloc_count    = 0;  ///< Outstanding transient malloc chunks.
        std::size_t drain_count     = 0;  ///< Channel drains performed.
    };

    /**
     * @brief Thread-local arena for coroutine frame allocations.
     *
     * @details All arena state is touched from the owning thread only, except
     * the incoming channel, which receives chunks pushed by foreign threads
     * (lock-free @c nukes::dynamic::mpsc_queue).
     */
    struct frame_allocator : frame_alloc_stats<not is_debug> {

        /// @brief Largest total chunk size served from the pmr pool.
        static constexpr std::size_t kMaxSize = 4096;

        /// @brief Size of the per-chunk header stored before the user payload.
        static constexpr std::size_t kHeaderSize = 16;

        /// @brief Bit 63 of the header size field marks a transient (malloc-served) chunk.
        static constexpr std::size_t kTransientFlag = std::size_t{1} << 63;

        /// @brief Per-chunk header: transfer channel of the owning arena + size/flags.
        struct chunk_header {
            nukes::dynamic::mpsc_queue<chunk_header*>* _channel { nullptr };
            std::size_t _size { 0 };
        };

        /// @brief Returns the thread-local arena singleton.
        static frame_allocator& get_instance() {
            static thread_local frame_allocator instance;
            return instance;
        }

        /**
         * @brief Allocates @p size bytes for a coroutine frame.
         * @param size Requested payload size.
         * @return Pointer to the user payload area (chunk header + 16 bytes).
         * @throws std::bad_alloc on allocation failure or when the arena limit
         *         is reached and @c breach_memory_limit is disabled.
         */
        [[nodiscard]] void* allocate(std::size_t size) {
            maybe_drain(true);
            const auto total = align_up(size + kHeaderSize, 16);
            auto* chunk = obtain_chunk(total);
            _occupied += total;
            return chunk + 1;
        }

        /**
         * @brief Returns an allocation to its owning arena.
         * @param mem_ptr Pointer returned by @c allocate().
         * @param size Unused — the chunk size is read from the header.
         */
        void deallocate(void* mem_ptr, std::size_t) noexcept {
            if (not mem_ptr) return;
            auto* chunk = static_cast<chunk_header*>(mem_ptr) - 1;
            const auto flags = chunk->_size;
            if (is_transient(flags)) {
                // NOTE: Transient chunks never travel through the channel — free immediately.
                _occupied -= chunk_size_of(flags);
                std::free(chunk);
                if constexpr (not is_debug) {
                    --malloc_count;
                    live_system_chunks.fetch_sub(1, std::memory_order_relaxed);
                }
            } else if (chunk->_channel != &_channel) {
                // NOTE: Pooled chunk owned by another arena — hand it back through its channel.
                chunk->_channel->push(std::move(chunk));
            } else {
                // NOTE: Local pooled chunk — back to the pool free list (never to the system).
                _small_pool.deallocate(chunk, chunk_size_of(flags));
                _occupied -= chunk_size_of(flags);
            }
            maybe_drain(false);
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
         * @brief Current arena statistics.  Always zeros on release builds.
         */
        [[nodiscard]] stats_view stats() const noexcept {
            std::size_t pool_held = 0, mallocs = 0, drains = 0;
            if constexpr (not is_debug) {
                pool_held = pool_held_bytes;
                mallocs = malloc_count;
                drains = drain_count;
            }
            return stats_view {
                _occupied,
                pool_held,
                mallocs,
                _op_counter,
                drains,
                live_system_chunks.load(std::memory_order_relaxed),
            };
        }

        /// @brief Process-wide count of system chunks currently held by all arenas (debug builds).
        static inline std::atomic<std::size_t> live_system_chunks { 0 };

    private:

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
        [[nodiscard]] bool breach_required() {
            const auto limit = arena_limit();
            if (limit == 0 or _occupied < limit) return false;
            if (cfg::g_config._breach_memory_limit) {
                if (not _breach_notified) {
                    _breach_notified = true;
                    std::cerr << "ace: frame allocator arena limit reached (" << limit
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
            const auto transient = (total > kMaxSize) or breach_required();
            void* mem = nullptr;
            if (transient) {
                mem = std::malloc(total);
                if (not mem) throw std::bad_alloc();
                if constexpr (not is_debug) {
                    ++malloc_count;
                    live_system_chunks.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                mem = _small_pool.allocate(total);
                if (mem == nullptr) throw std::bad_alloc();
            }
            auto* chunk = static_cast<chunk_header*>(mem);
            chunk->_channel = &_channel;
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
         * @details Chunks in the channel are owned by this arena (the header
         * channel pointer equals @c &_channel by construction).  Uses the plain
         * per-node @c pop() loop — @c pop_batch() iterates from the queue dummy
         * node and yields garbage on the first dereference, so it is unusable
         * for batch consumption.
         */
        void drain_channel() {
            if constexpr (not is_debug) ++drain_count;
            chunk_header* chunk = nullptr;
            while (_channel.pop(chunk)) {
                const auto size = chunk_size_of(chunk->_size);
                _occupied -= size;
                _small_pool.deallocate(chunk, size);
            }
        }

        /// @brief PMR resource backing the pool: allocates from the system, and
        ///        actually deallocates (unlike iovec_alloc's controller) so that
        ///        the arena destructor returns all pool blocks to the system.
        struct memory_controller : std::pmr::memory_resource {

            frame_allocator* _arena { nullptr };

            explicit memory_controller(frame_allocator* arena)
                : _arena(arena) {}

            void* do_allocate(std::size_t bytes, std::size_t alignment) override {
                if constexpr (not is_debug) {
                    _arena->pool_held_bytes += bytes;
                    live_system_chunks.fetch_add(1, std::memory_order_relaxed);
                }
                return std::pmr::new_delete_resource()->allocate(bytes, alignment);
            }

            void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
                if constexpr (not is_debug) {
                    _arena->pool_held_bytes -= bytes;
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
        ~frame_allocator() {
            drain_channel();
        }

        nukes::dynamic::mpsc_queue<chunk_header*> _channel;             ///< Incoming transfer channel.
        std::size_t _occupied { 0 };                                    ///< Bytes in use (pool + transient).
        std::size_t _op_counter { 0 };                                  ///< Operation counter.
        bool _breach_notified { false };                                ///< One-time breach notice.
        memory_controller _controller { this };                         ///< Backing memory resource.
        std::pmr::unsynchronized_pool_resource _small_pool { &_controller }; ///< Small-buffer pool.
    };

} // namespace ace::core::tools
