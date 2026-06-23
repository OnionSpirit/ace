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

#include <nukes/dynamic/regular_freelist.h>
#include <sys/uio.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <bit>

namespace ace::core::tools {

struct iovec_allocator {

    static constexpr size_t kMinSize = 128;
    static constexpr size_t kMaxSize = 4096;

    iovec_allocator() = default;

    [[nodiscard]] auto allocate(size_t size) noexcept -> iovec* {
        uint8_t sc = size_to_class(size);
        if (sc > 5) return nullptr;
        return _capture_table[sc](_pool_ptrs[sc]);
    }

    auto deallocate(iovec* iov) noexcept -> void {
        if (!iov) return;
        uint8_t sc = size_to_class(iov->iov_len);
        if (sc > 5) return;
        iov->iov_len = 0;
        _release_table[sc](_pool_ptrs[sc], iov);
    }

    [[nodiscard]] auto will_malloc(size_t size) const noexcept -> bool {
        uint8_t sc = size_to_class(size);
        return sc > 5 or _empty_table[sc](_pool_ptrs[sc]);
    }

private:

    static auto size_to_class(size_t size) noexcept -> uint8_t {
        if (size <= kMinSize) return 0;
        auto cls = static_cast<uint8_t>(std::bit_width(size - 1) - 7);
        return cls > 5 ? 6 : cls;
    }

    struct alignas(64) buffer_128  { iovec _iov{}; std::array<uint8_t, 128>  _data{}; };
    struct alignas(64) buffer_256  { iovec _iov{}; std::array<uint8_t, 256>  _data{}; };
    struct alignas(64) buffer_512  { iovec _iov{}; std::array<uint8_t, 512>  _data{}; };
    struct alignas(64) buffer_1k   { iovec _iov{}; std::array<uint8_t, 1024> _data{}; };
    struct alignas(64) buffer_2k   { iovec _iov{}; std::array<uint8_t, 2048> _data{}; };
    struct alignas(64) buffer_4k   { iovec _iov{}; std::array<uint8_t, 4096> _data{}; };

    nukes::dynamic::reg_freelist<buffer_128> _pool_128;
    nukes::dynamic::reg_freelist<buffer_256> _pool_256;
    nukes::dynamic::reg_freelist<buffer_512> _pool_512;
    nukes::dynamic::reg_freelist<buffer_1k>  _pool_1k;
    nukes::dynamic::reg_freelist<buffer_2k>  _pool_2k;
    nukes::dynamic::reg_freelist<buffer_4k>  _pool_4k;

    using capture_fn_t = iovec* (*)(void*);
    using release_fn_t = bool   (*)(void*, void*);
    using empty_fn_t   = bool   (*)(void*);

    template <typename BufT, size_t DataSize>
    static iovec* _capture_impl(void* pool) {
        auto& p = *static_cast<nukes::dynamic::reg_freelist<BufT>*>(pool);
        BufT* ptr = nullptr;
        if (!p.capture(ptr)) return nullptr;
        ptr->_iov.iov_base = ptr->_data.data();
        ptr->_iov.iov_len  = DataSize;
        return &ptr->_iov;
    }

    template <typename BufT>
    static bool _release_impl(void* pool, void* node) {
        auto& p = *static_cast<nukes::dynamic::reg_freelist<BufT>*>(pool);
        return p.raw_sync(node);
    }

    template <typename BufT>
    static bool _empty_impl(void* pool) {
        return static_cast<nukes::dynamic::reg_freelist<BufT>*>(pool)->empty();
    }

    static constexpr capture_fn_t _capture_table[6] = {
        &_capture_impl<buffer_128, 128>,
        &_capture_impl<buffer_256, 256>,
        &_capture_impl<buffer_512, 512>,
        &_capture_impl<buffer_1k,  1024>,
        &_capture_impl<buffer_2k,  2048>,
        &_capture_impl<buffer_4k,  4096>,
    };

    static constexpr release_fn_t _release_table[6] = {
        &_release_impl<buffer_128>,
        &_release_impl<buffer_256>,
        &_release_impl<buffer_512>,
        &_release_impl<buffer_1k>,
        &_release_impl<buffer_2k>,
        &_release_impl<buffer_4k>,
    };

    static constexpr empty_fn_t _empty_table[6] = {
        &_empty_impl<buffer_128>,
        &_empty_impl<buffer_256>,
        &_empty_impl<buffer_512>,
        &_empty_impl<buffer_1k>,
        &_empty_impl<buffer_2k>,
        &_empty_impl<buffer_4k>,
    };

    void* _pool_ptrs[6] = {
        &_pool_128, &_pool_256, &_pool_512,
        &_pool_1k,  &_pool_2k,  &_pool_4k,
    };
};

} // namespace ace::core::tools
