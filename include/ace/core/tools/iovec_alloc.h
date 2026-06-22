#pragma once
/// @file iovec_alloc.h
/// @brief Thread-local iovec buffer allocator using nukes::reg_freelist.
///
/// Provides power-of-2 sized buffers (128B – 4096B) suitable for
/// io_uring registered buffer I/O.  Each size class has its own
/// reg_freelist.  Buffers are page-aligned (4096) for DMA.
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
#include <optional>

namespace ace::core::tools {

class iovec_allocator {
public:
    static constexpr size_t kMinSize = 128;
    static constexpr size_t kMaxSize = 4096;

    struct iovec_buf {
        iovec iov{};
        uint8_t size_class{0};
    };

    iovec_allocator() = default;

    [[nodiscard]] auto allocate(size_t size) noexcept -> std::optional<iovec_buf> {
        uint8_t sc = size_to_class(size);
        if (sc > 5) return std::nullopt;

        iovec_buf result;
        result.size_class = sc;

        switch (sc) {
        case 0: {
            buffer_128* ptr = nullptr;
            if (!_pool_128.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = kMinSize;
            break;
        }
        case 1: {
            buffer_256* ptr = nullptr;
            if (!_pool_256.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = 256;
            break;
        }
        case 2: {
            buffer_512* ptr = nullptr;
            if (!_pool_512.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = 512;
            break;
        }
        case 3: {
            buffer_1k* ptr = nullptr;
            if (!_pool_1k.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = 1024;
            break;
        }
        case 4: {
            buffer_2k* ptr = nullptr;
            if (!_pool_2k.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = 2048;
            break;
        }
        case 5: {
            buffer_4k* ptr = nullptr;
            if (!_pool_4k.capture(ptr)) return std::nullopt;
            result.iov.iov_base = ptr->data.data();
            result.iov.iov_len  = 4096;
            break;
        }
        }
        return result;
    }

    auto deallocate(iovec_buf buf) noexcept -> void {
        switch (buf.size_class) {
        case 0: _pool_128.raw_sync(static_cast<buffer_128*>(buf.iov.iov_base)); break;
        case 1: _pool_256.raw_sync(static_cast<buffer_256*>(buf.iov.iov_base)); break;
        case 2: _pool_512.raw_sync(static_cast<buffer_512*>(buf.iov.iov_base)); break;
        case 3: _pool_1k.raw_sync(static_cast<buffer_1k*>(buf.iov.iov_base));  break;
        case 4: _pool_2k.raw_sync(static_cast<buffer_2k*>(buf.iov.iov_base));  break;
        case 5: _pool_4k.raw_sync(static_cast<buffer_4k*>(buf.iov.iov_base));  break;
        }
    }

    static auto size_to_class(size_t size) noexcept -> uint8_t {
        if (size <= 128)  return 0;
        if (size <= 256)  return 1;
        if (size <= 512)  return 2;
        if (size <= 1024) return 3;
        if (size <= 2048) return 4;
        if (size <= 4096) return 5;
        return 6;
    }

    static auto class_to_size(uint8_t sc) noexcept -> size_t {
        constexpr size_t sizes[] = {128, 256, 512, 1024, 2048, 4096};
        return sc < 6 ? sizes[sc] : 0;
    }

private:
    struct alignas(64) buffer_128  { std::array<uint8_t, 128>  data{}; };
    struct alignas(64) buffer_256  { std::array<uint8_t, 256>  data{}; };
    struct alignas(64) buffer_512  { std::array<uint8_t, 512>  data{}; };
    struct alignas(64) buffer_1k   { std::array<uint8_t, 1024> data{}; };
    struct alignas(64) buffer_2k   { std::array<uint8_t, 2048> data{}; };
    struct alignas(64) buffer_4k   { std::array<uint8_t, 4096> data{}; };

    nukes::dynamic::reg_freelist<buffer_128> _pool_128;
    nukes::dynamic::reg_freelist<buffer_256> _pool_256;
    nukes::dynamic::reg_freelist<buffer_512> _pool_512;
    nukes::dynamic::reg_freelist<buffer_1k>  _pool_1k;
    nukes::dynamic::reg_freelist<buffer_2k>  _pool_2k;
    nukes::dynamic::reg_freelist<buffer_4k>  _pool_4k;
};

} // namespace ace::core::tools
