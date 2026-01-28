/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CLOCK_H
#define ACE_CLOCK_H
#include <chrono>
#include <set>

#include "service.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    using timepoint_t = std::chrono::time_point<std::chrono::steady_clock, std::chrono::milliseconds>;

    inline auto clock_now() {
        return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
    }

    struct clock_record {
        std::chrono::time_point<std::chrono::steady_clock> _expiration_ts;
        std::chrono::duration<uint64_t, std::milli> _duration;
        async<> _context;

        // NOTE: Clock record is less if its completion time is earlier
        bool operator < (const clock_record& p) const {
            return this->_expiration_ts < p._expiration_ts;
        }
    };


    struct time_slot {
        nukes::dynamic::mpsc_queue<clock_record> _records;

        int release_slot(int allowed_releases) {
            int released =0;
            while (not _records.empty() and released < allowed_releases) {
                clock_record record;
                if (_records.pop(record)) [[likely]] {
                    runner::schedule(std::move(record._context));
                    ++released;
                }
            }
            return released;
        }

        bool empty() { return _records.empty(); }
    };

    /**
     *
     * @tparam tick_arg_v @b Tick @b argument value
     * @tparam tick_ratio_t @b Ratio (relative to std::chrono::seconds) of the tick argument
     * @tparam ticks_per_dial_v Tick @b quantity of the dial period
     */
    template <
        size_t tick_arg_v,
        typename tick_ratio_t,
        size_t ticks_per_dial_v
    > requires std::__is_ratio_v<tick_ratio_t>
    struct dial {
        using tick_t = std::chrono::duration<uint64_t, tick_ratio_t>;

        static constexpr auto tick_duration_v = tick_t() * tick_arg_v;
        static constexpr auto dial_period_v = tick_duration_v * ticks_per_dial_v;

        time_slot _slots[ticks_per_dial_v] {};
        const timepoint_t* _current_ts { nullptr };

        dial() = delete;

        explicit dial(const timepoint_t* current_ts)
            : _current_ts(current_ts) {};

    };

    struct dyn_dial {
        using duration_t = std::chrono::duration<uint64_t, std::milli>;

        const duration_t _tick_duration;
        const std::size_t _tick_count;
        const timepoint_t* _current_ts;
        const duration_t _dial_period;

        int* _release_counter;
        std::vector<time_slot> _slots;
        std::size_t _arrow;

        dyn_dial() = delete;

        template <typename rep_t, typename period_t>
        explicit dyn_dial(const std::chrono::duration<rep_t, period_t> tick_duration,
                          const std::size_t tick_count,
                          const timepoint_t* current_ts,
                          int* release_counter)
            : _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0) ? tick_count : std::bit_ceil(tick_count))
            , _current_ts(current_ts)
            , _dial_period(_tick_duration * _tick_count)
            , _release_counter(release_counter)
            , _slots(_tick_count)
            , _arrow(_current_ts->time_since_epoch() % _dial_period.count() / _tick_duration) {};

        void subscribe(clock_record&& record) {
            const auto dur = record._duration % _dial_period;
            const auto arrow_offset = dur / _tick_duration;
            _slots[_arrow + arrow_offset]._records.push(std::forward<clock_record>(record));
        }

        int release_ticks(uint passed_ticks) {
            int arrow_offset = 0;
            while (arrow_offset < passed_ticks
                and (arrow_offset == 0 or _slots[_arrow % _tick_count + --arrow_offset].empty())
                and *_release_counter > 0) {
                *_release_counter -= _slots[_arrow % _tick_count + arrow_offset].release_slot(*_release_counter);
                ++arrow_offset;
            }
            _arrow += arrow_offset;
            return arrow_offset;
            // for (int arrow_offset = 0; arrow_offset < passed_ticks; ++arrow_offset) {
            //     *_release_counter -= _slots[_arrow + arrow_offset].release_slot(*_release_counter);
            //     if (not _slots[_arrow + arrow_offset].empty() or *_release_counter == 0)
            //         break;
            //     ++_arrow;
            // }
        }

        void release_round() { release_ticks(_tick_count); }

        auto count_interval(timepoint_t& from) const {
            return _current_ts->time_since_epoch() - from.time_since_epoch();
        }

        [[nodiscard]] std::size_t count_rounds(const duration_t& interval) const {
            return interval / _dial_period;
        }

        [[nodiscard]] std::size_t count_ticks_on_reminder(const duration_t& interval) const {
            return interval % _dial_period / _tick_duration;
        }

        // NOTE: Later for cascade entity
        // int roll() {
        //     const auto passed_interval = _current_ts->time_since_epoch() - _last_timestamp.time_since_epoch();
        //     const auto rounds = passed_interval / _dial_period;
        //     const auto dur = passed_interval % _dial_period;
        //     _current_ts->time_since_epoch() % _dial_period.count() / _tick_duration
        //     const auto released = _slots[_arrow + arrow_offset].release_slot(*_release_counter);
        // }

    };

    struct clock : singleton_service_traits<clock> {

        clock() = default;

        void setup_releases_per_yank(const int releases_per_yank) {
            _releases_per_yank = releases_per_yank;
        }

        // TODO: Add record detach
        promise<bool> service_yank() {
            _current_ts = std::chrono::steady_clock::now();

            // NOTE: Fetching time requests to inner records pool
            {
                clock_record record;
                while (_requests.pop(record)) _records.insert(std::move(record));
            }

            // // TODO: FIX BATCH!!! After fix replace while block
            // for (auto&& record : _requests.pop_batch()) {
            //     _records.insert(std::forward<clock_record>(record));
            // }

            for (int i =0;
                not _records.empty()
                and _records.begin()->_expiration_ts <= _current_ts
                and i < _releases_per_yank;
            ++i) {
                auto [_release_ts, _, _context]
                    = std::forward<clock_record>(_records.extract(_records.begin()).value());
                runner::schedule(std::move(_context));
            }

            co_return _records.empty() and _requests.empty();
        }

        void subscribe(clock_record&& record) { _requests.push(std::move(record)); }

        static auto current_time() {
            return get_instance()._current_ts;
        }

        nukes::dynamic::mpsc_queue<clock_record> _requests;
        std::multiset<clock_record> _records;
        std::chrono::time_point<std::chrono::steady_clock> _current_ts = std::chrono::steady_clock::now();
        int _releases_per_yank {128};
    };

}

#endif //ACE_CLOCK_H
