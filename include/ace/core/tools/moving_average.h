/**
 * @file moving_average.h
 * @brief Simple moving-average calculator for runner velocity tracking.
 *
 * @details Used by @c ace::core::runner to compute task throughput (tasks
 * per unit time) over a sliding window.  The balancer uses this metric for
 * weighted task distribution across runners.
 */
#ifndef ACE_MOVING_AVERAGE_H
#define ACE_MOVING_AVERAGE_H

#include <array>
#include <cstdint>

#include "ace/core/tools/macro.h"

namespace ace::core::tools {

    /**
     * @brief Sliding-window moving average calculator.
     *
     * @details Maintains a fixed-size window (4 elements) of recent values.
     * @c add() pushes a new value, evicts the oldest, and returns the updated
     * average.  @c value() returns the current average (total / window_size).
     * The window initially contains zeros until filled.
     */
    struct moving_average {
        static constexpr int window_size = 4; ///< Fixed number of samples in the window.
        long                          _total_sum    { }; ///< Sum of all samples currently in the window.
        std::array<int, window_size>  _members      { }; ///< Ring buffer of the last samples.
        std::uint32_t                 _curr_member  { }; ///< Index of the next sample slot (wraps at window size).
        int                           _zeros        { window_size - 1 }; ///< Number of unfilled slots; shrinks as the window fills.

        /// @brief Default constructor — empty window.
        moving_average() = default;

        /**
         * @brief Copy constructor — copies the whole window state.
         * @param aq Source average to copy.
         */
        moving_average(const moving_average& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
        }

        /// @brief Copy assignment.
        moving_average& operator=(const moving_average& aq) = default;

        /**
         * @brief Move constructor — copies state and clears the source.
         * @param aq Source average to move from.
         */
        moving_average(moving_average&& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
            aq.clear();
        }

        /**
         * @brief Move assignment — copies state and clears the source.
         * @param aq Source average to move from.
         * @return Reference to this average.
         */
        moving_average& operator=(moving_average&& aq) noexcept {
            _total_sum = aq._total_sum;
            _curr_member = aq._curr_member;
            _members = aq._members;
            _zeros = aq._zeros;
            aq.clear();
            return *this;
        }

        /**
         * @brief Current average over the filled window slots.
         * @return Sum of samples divided by the number of filled slots; 0 when empty.
         */
        [[nodiscard]] long value() const { return _total_sum / (window_size - _zeros); }

        /**
         * @brief Adds a sample and returns the updated average.
         * @param new_one Sample to add.
         * @return Updated average value.
         */
        [[nodiscard]] long add(const int& new_one) {
            _total_sum = _total_sum + new_one - _members[_curr_member % (window_size - _zeros)];
            _members[_curr_member % (window_size - _zeros)] = new_one;
            (_zeros == 0) ? 1 : (--_zeros);
            ++_curr_member;
            return value();
        }

        /// @brief Resets the window to its empty state.
        void clear() {
            _total_sum = 0;
            _curr_member = 0;
            _members.fill(0);
            _zeros = window_size - 1;
        }
    };

}

#endif //ACE_MOVING_AVERAGE_H
