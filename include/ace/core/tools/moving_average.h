#ifndef ACE_MOVING_AVERAGE_H
#define ACE_MOVING_AVERAGE_H

#include <array>
#include <cstdint>

#include "ace/core/tools/meta.h"

namespace ace::core::tools {

    struct moving_average {
        static constexpr int window_size = 4;
        long                          _total_sum    { };
        std::array<int, window_size>  _members      { };
        std::uint32_t                 _curr_member  { };
        int                           _zeros        { window_size - 1 };

        moving_average() = default;

        moving_average(const moving_average& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
        }

        moving_average& operator=(const moving_average& aq) = default;

        moving_average(moving_average&& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
            aq.clear();
        }

        moving_average& operator=(moving_average&& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
            aq.clear();
            return *this;
        }

        [[nodiscard]] long value() const { return _total_sum / (window_size - _zeros); }

        [[nodiscard]] long add(const int& new_one) {
            _total_sum = _total_sum + new_one - _members[_curr_member % (window_size - _zeros)];
            _members[_curr_member % (window_size - _zeros)] = new_one;
            _zeros == 0 ? : --_zeros;
            ++_curr_member;
            return value();
        }

        void clear() {
            _total_sum = 0;
            _curr_member = 0;
            _members.fill(0);
            _zeros = window_size - 1;
        }
    };

}

#endif //ACE_MOVING_AVERAGE_H
