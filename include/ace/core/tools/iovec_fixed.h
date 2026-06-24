#pragma once
/// @file iovec_fixed.h
/// @brief Thread-local fixed-size iovec buffer allocator for io_uring
/// zero-copy sendmsg with pre-registered buffers.
///
/// Pre-allocates a configurable number of 512B and 2048B buffers.
/// All buffers are registered with io_uring as fixed buffers once,
/// and each allocation returns an iovec* that can be mapped to its
/// corresponding fixed-buffer index via buf_index().
///
/// The registration iovec array is built once during init() and can
/// be passed directly to io_uring_register_buffers().

#include <sys/uio.h>
#include <array>
#include <cstdint>
#include <cstddef>

namespace ace::core::tools {

struct iovec_fixed_allocator {

    static constexpr size_t kSize_512  = 512;
    static constexpr size_t kSize_2048 = 2048;

    iovec_fixed_allocator() = default;

    ~iovec_fixed_allocator() {
        delete[] _buf_512;
        delete[] _buf_2048;
        delete[] _iovec_pool;
    }

    iovec_fixed_allocator(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator& operator=(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator(iovec_fixed_allocator&&) = delete;
    iovec_fixed_allocator& operator=(iovec_fixed_allocator&&) = delete;

    void init(std::size_t count_512, std::size_t count_2048) {
        if (_total_count or _buf_512 or _buf_2048) return; // already initialized

        _count_512  = count_512;
        _count_2048 = count_2048;
        _total_count = count_512 + count_2048;

        if (_total_count == 0) return;
        _iovec_pool = new iovec[_total_count];

        // NOTE: Allocate and assign 512 chunks
        if (count_512 > 0) {
            _buf_512  = new buffer_512[count_512]{};
            for (std::size_t i = 0; i < count_512; ++i) {
                *reinterpret_cast<std::size_t*>(_buf_512[i].data()) = i + 1;
                _iovec_pool[i].iov_base = _buf_512[i].data();
                _iovec_pool[i].iov_len  = kSize_512;
            }
            *reinterpret_cast<std::size_t*>(_buf_512[count_512 - 1].data()) = kSentinel;
            _free_head_512 = 0;
        }

        // NOTE: Allocate and assign 2048 chunks
        if (count_2048 > 0) {
            _buf_2048 = new buffer_2048[count_2048]{};
            for (std::size_t i = 0; i < count_2048; ++i) {
                *reinterpret_cast<std::size_t*>(_buf_2048[i].data()) = count_512 + i + 1;
                _iovec_pool[i].iov_base = _buf_2048[i].data();
                _iovec_pool[i].iov_len  = kSize_2048;
            }
            *reinterpret_cast<std::size_t*>(_buf_2048[count_2048 - 1].data()) = kSentinel;
            _free_head_2048 = count_512;
        }

    }

    [[nodiscard]] auto allocate(size_t size) noexcept -> iovec* {
        if (_total_count == 0 and !_buf_512 and !_buf_2048) return nullptr;
        if (size <= kSize_512)
            return _alloc_512();
        if (size <= kSize_2048)
            return _alloc_2048();
        return nullptr;
    }

    auto deallocate(iovec* iov) noexcept -> void {
        if (not iov) return;
        iov->iov_len = 0;
        if (iov->iov_base) {
            auto ptr = static_cast<uint8_t*>(iov->iov_base);
            if (_is_in_512(ptr)) {
                const std::size_t idx = (ptr - _buf_512[0].data()) / sizeof(buffer_512);
                *reinterpret_cast<std::size_t*>(ptr) = _free_head_512;
                _free_head_512 = idx;
            } else if (_is_in_2048(ptr)) {
                const std::size_t idx = (ptr - _buf_2048[0].data()) / sizeof(buffer_2048);
                *reinterpret_cast<std::size_t*>(ptr) = _free_head_2048;
                _free_head_2048 = idx;
            }
        }
    }

    [[nodiscard]] auto buf_index(const iovec* iov) const noexcept -> std::size_t {
        if (not iov) return kSentinel;
        const auto ptr = static_cast<const uint8_t*>(iov->iov_base);
        if (_is_in_512(ptr)) {
            return (ptr - _buf_512[0].data()) / sizeof(buffer_512);
        }
        return _count_512 + ((ptr - _buf_2048[0].data()) / sizeof(buffer_2048));
    }

    [[nodiscard]] auto registration_iovecs() const noexcept -> const iovec* {
        return _iovec_pool;
    }

    [[nodiscard]] auto registration_count() const noexcept -> unsigned {
        return _total_count;
    }

    [[nodiscard]] auto is_initialized() const noexcept -> bool {
        return _total_count > 0 or _buf_512 or _buf_2048;
    }

private:

    static constexpr std::size_t kSentinel = std::numeric_limits<std::size_t>::max();

    using buffer_512 = std::array<uint8_t, 512>;
    using buffer_2048 = std::array<uint8_t, 2048>;

    buffer_512*  _buf_512  = nullptr;
    buffer_2048* _buf_2048 = nullptr;

    std::size_t _count_512  = 0;
    std::size_t _count_2048 = 0;
    std::size_t _total_count = 0;

    std::size_t _free_head_512  = kSentinel;
    std::size_t _free_head_2048 = kSentinel;

    iovec* _iovec_pool = nullptr;

    [[nodiscard]] auto _alloc_512() noexcept -> iovec* {
        if (_free_head_512 == kSentinel) return nullptr;
        const std::size_t idx = _free_head_512;
        _free_head_512 = *static_cast<std::size_t*>(_iovec_pool[idx].iov_base);
        _iovec_pool[idx].iov_len = kSize_512;
        return &_iovec_pool[idx];
    }

    [[nodiscard]] auto _alloc_2048() noexcept -> iovec* {
        if (_free_head_2048 == kSentinel) return nullptr;
        const std::size_t idx = _free_head_2048;
        _free_head_2048 = *static_cast<std::size_t*>(_iovec_pool[idx].iov_base);
        _iovec_pool[idx].iov_len = kSize_2048;
        return &_iovec_pool[idx];
    }

    [[nodiscard]] auto _is_in_512(const uint8_t* ptr) const noexcept -> bool {
        if (!_buf_512 or !_count_512) return false;
        const auto base = _buf_512[0].data();
        const auto end  = base + ( _count_512 * sizeof(buffer_512) );
        return ptr >= base && ptr < end;
    }

    [[nodiscard]] auto _is_in_2048(const uint8_t* ptr) const noexcept -> bool {
        if (!_buf_2048 or !_count_2048) return false;
        const auto base = _buf_2048[0].data();
        const auto end  = base + ( _count_2048 * sizeof(buffer_2048) );
        return ptr >= base && ptr < end;
    }
};

} // namespace ace::core::tools
