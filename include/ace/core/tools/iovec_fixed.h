#pragma once
/// @file iovec_fixed.h
/// @brief Thread-local fixed-size iovec buffer allocator for io_uring
/// zero-copy sendmsg with pre-registered buffers.
///
/// Pre-allocates a configurable number of 1024B and 4096B buffers.
/// All buffers are registered with io_uring as fixed buffers once,
/// and each allocation returns an iovec* that can be mapped to its
/// corresponding fixed-buffer index via buf_index().
///
/// The registration iovec array is built once during init() and can
/// be passed directly to io_uring_register_buffers().

#include <array>
#include <cstdint>
#include <cstddef>

namespace ace::core::tools {

struct iovec_fixed_allocator {

    static constexpr size_t quarter_len = 1024;
    static constexpr size_t frame_len   = 4096;

    iovec_fixed_allocator() = default;

    ~iovec_fixed_allocator() {
        delete[] _quarter_pool;
        delete[] _frame_pool;
        delete[] _iovec_pool;
    }

    iovec_fixed_allocator(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator& operator=(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator(iovec_fixed_allocator&&) = delete;
    iovec_fixed_allocator& operator=(iovec_fixed_allocator&&) = delete;

    void init(std::size_t quarter_count, std::size_t frame_count) {
        if (_total_count or _quarter_pool or _frame_pool) return; // already initialized

        _quarter_count  = quarter_count;
        _frame_count = frame_count;
        _total_count = quarter_count + frame_count;

        if (_total_count == 0) return;
        _iovec_pool = new iovec[_total_count];

        // NOTE: Allocate and assign 1024 chunks
        if (quarter_count > 0) {
            _quarter_pool  = new quarter[quarter_count]{};
            for (std::size_t i = 0; i < quarter_count; ++i) {
                *reinterpret_cast<std::size_t*>(_quarter_pool[i].data()) = i + 1;
                _iovec_pool[i].iov_base = _quarter_pool[i].data();
                _iovec_pool[i].iov_len  = quarter_len;
            }
            *reinterpret_cast<std::size_t*>(_quarter_pool[quarter_count - 1].data()) = usage_sentinel;
            _free_head_quarter = 0;
        }

        // NOTE: Allocate and assign 4096 chunks
        if (frame_count > 0) {
            _frame_pool = new frame[frame_count]{};
            for (std::size_t i = 0; i < frame_count; ++i) {
                *reinterpret_cast<std::size_t*>(_frame_pool[i].data()) = quarter_count + i + 1;
                _iovec_pool[i].iov_base = _frame_pool[i].data();
                _iovec_pool[i].iov_len  = frame_len;
            }
            *reinterpret_cast<std::size_t*>(_frame_pool[frame_count - 1].data()) = usage_sentinel;
            _free_head_frame = quarter_count;
        }

    }

    [[nodiscard]] auto allocate(size_t size) noexcept -> iovec* {
        if (_total_count == 0 and !_quarter_pool and !_frame_pool) return nullptr;
        if (size <= quarter_len)
            return _alloc_quarter();
        if (size <= frame_len)
            return _alloc_frame();
        return nullptr;
    }

    auto deallocate(iovec* iov) noexcept -> void {
        if (not iov) return;
        iov->iov_len = 0;
        if (iov->iov_base) {
            auto ptr = static_cast<uint8_t*>(iov->iov_base);
            if (_is_in_quarter_pool(ptr)) {
                const std::size_t idx = (ptr - _quarter_pool[0].data()) / sizeof(quarter);
                *reinterpret_cast<std::size_t*>(ptr) = _free_head_quarter;
                _free_head_quarter = idx;
            } else if (_is_in_frame_pool(ptr)) {
                const std::size_t idx = (ptr - _frame_pool[0].data()) / sizeof(frame);
                *reinterpret_cast<std::size_t*>(ptr) = _free_head_frame;
                _free_head_frame = idx;
            }
        }
    }

    [[nodiscard]] auto buf_index(const iovec* iov) const noexcept -> std::size_t {
        if (not iov) return usage_sentinel;
        const auto ptr = static_cast<const uint8_t*>(iov->iov_base);
        if (_is_in_quarter_pool(ptr)) {
            return (ptr - _quarter_pool[0].data()) / sizeof(quarter);
        }
        return _quarter_count + ((ptr - _frame_pool[0].data()) / sizeof(frame));
    }

    [[nodiscard]] auto registration_iovecs() const noexcept -> const iovec* {
        return _iovec_pool;
    }

    [[nodiscard]] auto registration_count() const noexcept -> unsigned {
        return _total_count;
    }

    [[nodiscard]] auto is_initialized() const noexcept -> bool {
        return _total_count > 0 or _quarter_pool or _frame_pool;
    }

private:

    static constexpr std::size_t usage_sentinel = std::numeric_limits<std::size_t>::max();

    using quarter = std::array<uint8_t, 1024>;
    using frame   = std::array<uint8_t, 4096>;

    quarter* _quarter_pool = nullptr;
    frame*   _frame_pool   = nullptr;

    std::size_t _quarter_count  = 0;
    std::size_t _frame_count = 0;
    std::size_t _total_count = 0;

    std::size_t _free_head_quarter = usage_sentinel;
    std::size_t _free_head_frame   = usage_sentinel;

    iovec* _iovec_pool = nullptr;

    [[nodiscard]] auto _alloc_quarter() noexcept -> iovec* {
        if (_free_head_quarter == usage_sentinel) return nullptr;
        const std::size_t idx = _free_head_quarter;
        _free_head_quarter = *static_cast<std::size_t*>(_iovec_pool[idx].iov_base);
        _iovec_pool[idx].iov_len = quarter_len;
        return &_iovec_pool[idx];
    }

    [[nodiscard]] auto _alloc_frame() noexcept -> iovec* {
        if (_free_head_frame == usage_sentinel) return nullptr;
        const std::size_t idx = _free_head_frame;
        _free_head_frame = *static_cast<std::size_t*>(_iovec_pool[idx].iov_base);
        _iovec_pool[idx].iov_len = frame_len;
        return &_iovec_pool[idx];
    }

    [[nodiscard]] auto _is_in_quarter_pool(const uint8_t* ptr) const noexcept -> bool {
        if (!_quarter_pool or !_quarter_count) return false;
        const auto base = _quarter_pool[0].data();
        const auto end  = base + ( _quarter_count * sizeof(quarter) );
        return ptr >= base && ptr < end;
    }

    [[nodiscard]] auto _is_in_frame_pool(const uint8_t* ptr) const noexcept -> bool {
        if (!_frame_pool or !_frame_count) return false;
        const auto base = _frame_pool[0].data();
        const auto end  = base + ( _frame_count * sizeof(frame) );
        return ptr >= base && ptr < end;
    }
};

} // namespace ace::core::tools
