#pragma once
/// @file iovec_alloc.h
/// @brief Thread-local iovec buffer allocator using nukes::reg_freelist.
///
/// Provides power-of-2 sized buffers (128B – 4096B) suitable for
/// io_uring registered buffer I/O.  Each size class has its own
/// reg_freelist.  Buffers embed an iovec at offset 0 — the returned
/// iovec* points directly into the chunk.
///
/// Node overhead (dyn_reg_node<T>::_next ptr + alignment padding)
/// is accounted for — buffers returned are the data payload area
/// inside the node, not the full node allocation.

#include <cstddef>
#include <memory_resource>

namespace ace::core::tools {

/**
 * @brief Thread-local iovec allocator with a small-buffer pool.
 *
 * @details Buffers up to @c kMaxSize come from an internal pmr pool;
 * larger ones are allocated with @c malloc().  Each iovec is embedded
 * at offset 0 of its allocation, with the data area right after it.
 */
struct iovec_allocator {

    /// @brief Largest size served from the small-buffer pool.
    static constexpr size_t kMaxSize = 4096;

    /// @brief Default constructor.
    iovec_allocator() = default;

    // NOTE: Allocates requested size and puts it into the iovec struct
    /**
     * @brief Allocates an iovec with a data buffer of the requested size.
     * @param size Data buffer size.
     * @return Pointer to the iovec.
     * @throws std::bad_alloc when the allocation fails.
     */
    [[nodiscard]] auto allocate(size_t size) -> iovec* {

        iovec* iov = nullptr;
        void* mem = nullptr;

        if (size > kMaxSize) mem = malloc(sizeof(iovec) + size);
        else mem = _small_pool.allocate(sizeof(iovec) + size);
        if (mem == nullptr) throw std::bad_alloc();

        iov = static_cast<iovec*>(mem);
        iov->iov_base = static_cast<std::byte*>(mem) + sizeof(iovec);
        iov->iov_len = size;

        return iov;
    }

    /**
     * @brief Returns an iovec to the pool.
     * @param iov Iovec to deallocate.
     */
    auto deallocate(iovec* iov) -> void {
        if (!iov) return;
        if (iov->iov_len > kMaxSize)
            return free(iov); // NOTE: allocated with malloc() in allocate()
        _small_pool.deallocate(iov, iov->iov_len + sizeof(iovec));
        iov->iov_len = 0;
    }

    /**
     * @brief Allocates a packed array of @c len elements of type @c data_t.
     * @tparam data_t Element type.
     * @param len Number of elements.
     * @return Pointer to the array, or @c nullptr when too large for the pool.
     */
    template <typename data_t>
    [[nodiscard]] auto allocate_as(size_t len = 1) noexcept -> data_t* {
        if ((sizeof(data_t) * len) > kMaxSize) return nullptr;
        auto data = static_cast<data_t*>(_small_pool.allocate(sizeof(data_t) * len));
        return data;
    }

    /**
     * @brief Deallocates a packed array allocated with @c allocate_as().
     * @param mem Pointer returned by @c allocate_as().
     * @param len Number of elements.
     */
    auto deallocate_as(void* mem, const size_t len) noexcept -> void {
        _small_pool.deallocate(mem, len);
    }

private:

    /**
     * @brief PMR resource backing the small-buffer pool.
     * @details With @c is_debug (release builds, NDEBUG defined) deallocation
     * is a no-op — the fine allocator never deallocates from itself.  In debug
     * builds real deallocation is performed to keep sanitizers quiet.
     */
    struct memory_controller : std::pmr::memory_resource {

        void* do_allocate(std::size_t bytes, std::size_t alignment) override {
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }

        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
            // NOTE: Actually never deallocate on release builds.
            // NOTE: Fine allocator never deallocates from itself
            if constexpr (is_debug)
                (void)p, (void)bytes, (void)alignment;
            // NOTE: Enables actual deallocation to suppress ASAN if it is naughty.
            else
                std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
        }

        [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept override {
            return this == &other;
        }
    };

    memory_controller                        _controller;   ///< Backing memory resource.
    std::pmr::unsynchronized_pool_resource   _small_pool {&_controller}; ///< Small-buffer pool.
};

} // namespace ace::core::tools
