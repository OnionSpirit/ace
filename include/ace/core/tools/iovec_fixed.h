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
        delete[] _reg_iovecs;
    }

    iovec_fixed_allocator(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator& operator=(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator(iovec_fixed_allocator&&) = delete;
    iovec_fixed_allocator& operator=(iovec_fixed_allocator&&) = delete;

    void init(uint16_t count_512, uint16_t count_2048) {
        if (_total_count or _buf_512 or _buf_2048) return; // already initialized

        _count_512  = count_512;
        _count_2048 = count_2048;
        _total_count = count_512 + count_2048;

        // ── allocate buffer arrays ────────────────────────────
        if (count_512)
            _buf_512  = new buffer_512[count_512]{};
        if (count_2048)
            _buf_2048 = new buffer_2048[count_2048]{};

        // ── set up per-buffer iovec descriptors ───────────────
        for (uint16_t i = 0; i < count_512; ++i) {
            _buf_512[i]._iov.iov_base = _buf_512[i]._data.data();
            _buf_512[i]._iov.iov_len  = kSize_512;
        }
        for (uint16_t i = 0; i < count_2048; ++i) {
            _buf_2048[i]._iov.iov_base = _buf_2048[i]._data.data();
            _buf_2048[i]._iov.iov_len  = kSize_2048;
        }

        // ── build free-list chains (stored in first 2 bytes of data) ──
        if (count_512 > 0) {
            _free_head_512 = 0;
            for (uint16_t i = 0; i < count_512 - 1; ++i)
                *reinterpret_cast<uint16_t*>(_buf_512[i]._data.data()) = i + 1;
            *reinterpret_cast<uint16_t*>(_buf_512[count_512 - 1]._data.data()) = kSentinel;
        }

        if (count_2048 > 0) {
            _free_head_2048 = 0;
            for (uint16_t i = 0; i < count_2048 - 1; ++i)
                *reinterpret_cast<uint16_t*>(_buf_2048[i]._data.data()) = i + 1;
            *reinterpret_cast<uint16_t*>(_buf_2048[count_2048 - 1]._data.data()) = kSentinel;
        }

        // ── build registration iovec array ────────────────────
        if (_total_count) {
            _reg_iovecs = new iovec[_total_count];
            for (uint16_t i = 0; i < count_512; ++i)
                _reg_iovecs[i] = _buf_512[i]._iov;
            for (uint16_t i = 0; i < count_2048; ++i)
                _reg_iovecs[count_512 + i] = _buf_2048[i]._iov;
        }
    }

    [[nodiscard]] auto allocate(size_t size) noexcept -> iovec* {
        if (_total_count == 0 and !_buf_512 and !_buf_2048) return nullptr;
        uint8_t cls = (size <= kSize_512) ? 0 : 1;
        if (cls == 0)
            return _alloc_512();
        else
            return _alloc_2048();
    }

    auto deallocate(iovec* iov) noexcept -> void {
        if (!iov) return;
        iov->iov_len = 0;
        if (iov->iov_base) {
            auto ptr = static_cast<uint8_t*>(iov->iov_base);
            if (_is_in_512(ptr)) {
                uint16_t idx = static_cast<uint16_t>((ptr - _buf_512[0]._data.data()) / sizeof(buffer_512));
                *reinterpret_cast<uint16_t*>(ptr) = _free_head_512;
                _free_head_512 = idx;
            } else if (_is_in_2048(ptr)) {
                uint16_t idx = static_cast<uint16_t>((ptr - _buf_2048[0]._data.data()) / sizeof(buffer_2048));
                *reinterpret_cast<uint16_t*>(ptr) = _free_head_2048;
                _free_head_2048 = idx;
            }
        }
    }

    [[nodiscard]] auto buf_index(const iovec* iov) const noexcept -> uint16_t {
        auto ptr = static_cast<const uint8_t*>(iov->iov_base);
        if (_is_in_512(ptr)) {
            return static_cast<uint16_t>((ptr - _buf_512[0]._data.data()) / sizeof(buffer_512));
        }
        return _count_512 + static_cast<uint16_t>((ptr - _buf_2048[0]._data.data()) / sizeof(buffer_2048));
    }

    [[nodiscard]] auto registration_iovecs() const noexcept -> const iovec* {
        return _reg_iovecs;
    }

    [[nodiscard]] auto registration_count() const noexcept -> unsigned {
        return _total_count;
    }

    [[nodiscard]] auto is_initialized() const noexcept -> bool {
        return _total_count > 0 or _buf_512 or _buf_2048;
    }

private:

    static constexpr uint16_t kSentinel = 0xFFFF;

    struct alignas(64) buffer_512  { iovec _iov{}; std::array<uint8_t, 512>  _data{}; };
    struct alignas(64) buffer_2048 { iovec _iov{}; std::array<uint8_t, 2048> _data{}; };

    buffer_512*  _buf_512  = nullptr;
    buffer_2048* _buf_2048 = nullptr;

    uint16_t _count_512  = 0;
    uint16_t _count_2048 = 0;
    uint16_t _total_count = 0;

    uint16_t _free_head_512  = kSentinel;
    uint16_t _free_head_2048 = kSentinel;

    iovec* _reg_iovecs = nullptr;

    [[nodiscard]] auto _alloc_512() noexcept -> iovec* {
        if (_free_head_512 == kSentinel) return nullptr;
        uint16_t idx = _free_head_512;
        _free_head_512 = *reinterpret_cast<uint16_t*>(_buf_512[idx]._data.data());
        _buf_512[idx]._iov.iov_len = kSize_512;
        return &_buf_512[idx]._iov;
    }

    [[nodiscard]] auto _alloc_2048() noexcept -> iovec* {
        if (_free_head_2048 == kSentinel) return nullptr;
        uint16_t idx = _free_head_2048;
        _free_head_2048 = *reinterpret_cast<uint16_t*>(_buf_2048[idx]._data.data());
        _buf_2048[idx]._iov.iov_len = kSize_2048;
        return &_buf_2048[idx]._iov;
    }

    [[nodiscard]] auto _is_in_512(const uint8_t* ptr) const noexcept -> bool {
        if (!_buf_512 or !_count_512) return false;
        auto base = _buf_512[0]._data.data();
        auto end  = base + _count_512 * sizeof(buffer_512);
        return ptr >= base && ptr < end;
    }

    [[nodiscard]] auto _is_in_2048(const uint8_t* ptr) const noexcept -> bool {
        if (!_buf_2048 or !_count_2048) return false;
        auto base = _buf_2048[0]._data.data();
        auto end  = base + _count_2048 * sizeof(buffer_2048);
        return ptr >= base && ptr < end;
    }
};

} // namespace ace::core::tools
