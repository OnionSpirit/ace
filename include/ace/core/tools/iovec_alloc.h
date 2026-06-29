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

struct iovec_allocator {

    static constexpr size_t kMaxSize = 4096;

    iovec_allocator() = default;

    // NOTE: Allocates requested size and puts it into the iovec struct
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

    auto deallocate(iovec* iov) -> void {
        if (!iov) return;
        if (iov->iov_len > kMaxSize)
            return delete iov;
        _small_pool.deallocate(iov, iov->iov_len + sizeof(iovec));
        iov->iov_len = 0;
    }

    template <typename data_t>
    [[nodiscard]] auto allocate_as(size_t len = 1) noexcept -> data_t* {
        if ((sizeof(data_t) * len) > kMaxSize) return nullptr;
        auto data = static_cast<data_t*>(_small_pool.allocate(sizeof(data_t) * len));
        return data;
    }

    auto deallocate_as(void* mem, const size_t len) noexcept -> void {
        _small_pool.deallocate(mem, len);
    }

private:

    struct memory_controller : std::pmr::memory_resource {

        void* do_allocate(std::size_t bytes, std::size_t alignment) override {
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }

        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
            (void)p; (void)bytes; (void)alignment;
        }

        [[nodiscard]] bool do_is_equal(const memory_resource& other) const noexcept override {
            return this == &other;
        }
    };

    memory_controller                        _controller;
    std::pmr::unsynchronized_pool_resource   _small_pool {&_controller};
};

} // namespace ace::core::tools
