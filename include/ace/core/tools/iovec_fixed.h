#pragma once
/// @file iovec_fixed.h
/// @brief Thread-local fixed-size iovec buffer allocator for io_uring
/// zero-copy sendmsg with pre-registered buffers.
///
/// Pre-allocates pools of power-of-2 sized buffers configured via
/// the ace::cfg::iovec_fixed_profile tag (a list of {power, count}
/// pairs).  Powers not explicitly listed alias to the nearest larger
/// configured power.
///
/// All metadata resides in a single _mem_arena; the iovec array is a
/// separate allocation to satisfy io_uring alignment requirements.
/// All internal lookups are O(1):
///   allocate() → _power_to_pool[bit_width(size-1) - _min_power]
///   deallocate() → _buf_pool_idx[iov - _iovec_pool]
///   buf_index()  → iov - _iovec_pool
///
/// The registration iovec array is built once during init() and can
/// be passed directly to io_uring_register_buffers().

#include <bit>
#include <cstdint>
#include <cstddef>
#include <span>

#include "ace/core/config.h"

namespace ace::core::tools {

struct iovec_fixed_allocator {

    iovec_fixed_allocator() = default;

    ~iovec_fixed_allocator() { delete[] _mem_arena; delete[] _iovec_pool; }

    iovec_fixed_allocator(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator& operator=(const iovec_fixed_allocator&) = delete;
    iovec_fixed_allocator(iovec_fixed_allocator&&) = delete;
    iovec_fixed_allocator& operator=(iovec_fixed_allocator&&) = delete;

    void init(std::span<const ace::cfg::iovec_fixed_pool_spec> specs) {
        if (_mem_arena or _iovec_pool) return;
        if (specs.empty()) return;

        // ── pass 1: compute dimensions ──────────────────────────
        _min_power = _max_power = specs[0].power;
        _pool_count = specs.size();
        _total_count = 0;
        for (const auto& spec : specs) {
            if (spec.power < _min_power) _min_power = spec.power;
            if (spec.power > _max_power) _max_power = spec.power;
            _total_count += spec.count;
        }
        if (_total_count == 0) return;

        const std::size_t power_range = _max_power - _min_power + 1;

        auto align = [](std::size_t value, std::size_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        };

        std::size_t offset = 0;

        offset = align(offset, alignof(pool));
        const std::size_t pools_offset = offset;
        offset += _pool_count * sizeof(pool);

        const std::size_t power_to_pool_offset = offset;
        offset += power_range;

        const std::size_t buf_pool_idx_offset = offset;
        offset += _total_count;

        offset = align(offset, alignof(std::max_align_t));
        const std::size_t buffers_offset = offset;
        for (const auto& spec : specs)
            offset += spec.count * (1ull << spec.power);

        // ── allocations ─────────────────────────────────────────
        _mem_arena  = new uint8_t[offset];
        _iovec_pool = new iovec[_total_count];

        _pools         = reinterpret_cast<pool*>(_mem_arena + pools_offset);
        _power_to_pool = _mem_arena + power_to_pool_offset;
        _buf_pool_idx  = _mem_arena + buf_pool_idx_offset;

        // ── power → pool alias table ────────────────────────────
        for (std::size_t p = _min_power, pool_i = 0; p <= _max_power; ++p) {
            while (pool_i < _pool_count && specs[pool_i].power < p) ++pool_i;
            _power_to_pool[p - _min_power] = static_cast<uint8_t>(
                pool_i < _pool_count ? pool_i : _pool_count - 1);
        }

        // ── pass 2: init pools, free-lists, iovec & buf_pool_idx
        std::size_t buf_cursor   = buffers_offset;
        std::size_t iovec_cursor = 0;
        for (std::size_t pool_i = 0; pool_i < _pool_count; ++pool_i) {
            const auto chunk_size  = 1ull << specs[pool_i].power;
            const auto chunk_count = specs[pool_i].count;

            auto& pool_desc       = _pools[pool_i];
            pool_desc.data        = _mem_arena + buf_cursor;
            pool_desc.chunk_size  = chunk_size;
            pool_desc.count       = chunk_count;
            pool_desc.power       = specs[pool_i].power;
            pool_desc.iovec_start = iovec_cursor;

            if (chunk_count == 0) {
                pool_desc.free_head = usage_sentinel;
                continue;
            }

            for (std::size_t i = 0; i < chunk_count; ++i) {
                const auto global_idx = iovec_cursor + i;
                const auto buf_ptr    = pool_desc.data + i * chunk_size;
                const auto next       = (i + 1 < chunk_count) ? (global_idx + 1) : usage_sentinel;
                *reinterpret_cast<std::size_t*>(buf_ptr) = next;
                _iovec_pool[global_idx].iov_base = buf_ptr;
                _iovec_pool[global_idx].iov_len  = chunk_size;
                _buf_pool_idx[global_idx] = static_cast<uint8_t>(pool_i);
            }
            pool_desc.free_head = iovec_cursor;
            iovec_cursor += chunk_count;
            buf_cursor   += chunk_count * chunk_size;
        }
    }

    [[nodiscard]] auto allocate(size_t size) noexcept -> iovec* {
        if (_total_count == 0) return nullptr;

        auto power = std::bit_width(size > 0 ? size - 1 : 0);
        if (power < _min_power) power = _min_power;
        if (power > _max_power) return nullptr;

        const auto start_pool = _power_to_pool[power - _min_power];
        for (auto pool_i = start_pool; pool_i < _pool_count; ++pool_i) {
            auto& pool_desc = _pools[pool_i];
            if (pool_desc.free_head == usage_sentinel) continue;
            const auto global_idx  = pool_desc.free_head;
            pool_desc.free_head = *static_cast<std::size_t*>(_iovec_pool[global_idx].iov_base);
            _iovec_pool[global_idx].iov_len = pool_desc.chunk_size;
            return &_iovec_pool[global_idx];
        }
        return nullptr;
    }

    auto deallocate(iovec* iov) noexcept -> void {
        if (not iov or not iov->iov_base) return;
        iov->iov_len = 0;

        const auto global_idx = static_cast<std::size_t>(iov - _iovec_pool);
        if (global_idx >= _total_count) return;

        const auto pool_i = _buf_pool_idx[global_idx];
        auto& pool_desc = _pools[pool_i];

        *static_cast<std::size_t*>(iov->iov_base) = pool_desc.free_head;
        pool_desc.free_head = global_idx;
    }

    [[nodiscard]] auto buf_index(const iovec* iov) const noexcept -> std::size_t {
        if (not iov) return usage_sentinel;
        return static_cast<std::size_t>(iov - _iovec_pool);
    }

    [[nodiscard]] auto registration_iovecs() const noexcept -> const iovec* {
        return _iovec_pool;
    }

    [[nodiscard]] auto registration_count() const noexcept -> unsigned {
        return static_cast<unsigned>(_total_count);
    }

    [[nodiscard]] auto is_initialized() const noexcept -> bool {
        return _total_count > 0;
    }

private:

    static constexpr std::size_t usage_sentinel = std::numeric_limits<std::size_t>::max();

    struct pool {
        uint8_t*    data        = nullptr;
        std::size_t chunk_size  = 0;
        std::size_t count       = 0;
        std::size_t power       = 0;
        std::size_t iovec_start = 0;
        std::size_t free_head   = usage_sentinel;
    };

    uint8_t*   _mem_arena      = nullptr;
    pool*      _pools          = nullptr;
    iovec*     _iovec_pool     = nullptr;
    uint8_t*   _power_to_pool  = nullptr;
    uint8_t*   _buf_pool_idx   = nullptr;

    std::size_t _total_count  = 0;
    std::size_t _pool_count   = 0;
    std::size_t _min_power    = 0;
    std::size_t _max_power    = 0;
};

} // namespace ace::core::tools
