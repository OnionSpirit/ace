/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CLOCK_H
#define ACE_CLOCK_H
#include <chrono>
#include <complex>
#include <set>

#include "service.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    using timepoint_t = decltype(std::chrono::steady_clock::now());

    using duration_t = decltype(std::chrono::steady_clock::duration());

    inline auto clock_now() { return std::chrono::steady_clock::now(); }

    struct clock_record {
        duration_t _duration {};
        async<> _context;

        clock_record() = default;

        // clock_record(const clock_record&) = delete;

        // clock_record(clock_record&& clk_rec) noexcept {
        //     this->_duration = clk_rec._duration;
        //     this->_context = std::move(clk_rec._context);
        // };

        clock_record(async<>&& ctx, const duration_t dur)
            : _duration(dur)
            , _context(std::forward<async<>>(ctx)) {}

    };


    struct time_slot {
        nukes::dynamic::mpsc_queue<clock_record> _records;

        static void release_record(clock_record&& record) {
            runner::schedule(std::move(record._context));
        }

        int release_slot(int allowed_releases) {
            int released =0;
            while (not _records.empty() and released < allowed_releases) {
                clock_record record;
                if (_records.pop(record)) [[likely]] {
                    release_record(std::move(record));
                    ++released;
                }
            }
            return released;
        }

        void release_max() {
            while (not _records.empty()) {
                clock_record record;
                if (_records.pop(record)) [[likely]] {
                    release_record(std::move(record));
                }
            }
        }


        bool empty() { return _records.empty(); }
    };

    struct time_wheel {

        const duration_t _tick_duration;
        const std::size_t _tick_count;
        const timepoint_t* _current_ts;
        const duration_t _wheel_period;

        int* _release_counter;
        time_wheel* _upper_wheel;
        std::vector<time_slot> _ticks;
        std::size_t _arrow {0};
        static constexpr std::size_t _arrow_start {0};

        time_wheel() = delete;

        template <typename rep_t, typename period_t>
        explicit time_wheel(const std::chrono::duration<rep_t, period_t> tick_duration,
                            const std::size_t tick_count,
                            const timepoint_t* current_ts,
                            int* release_counter,
                            time_wheel* upper_wheel = nullptr)
            : _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0) ? tick_count : std::bit_ceil(tick_count))
            , _current_ts(current_ts)
            , _wheel_period(_tick_duration * _tick_count)
            , _release_counter(release_counter)
            , _upper_wheel(upper_wheel)
            , _ticks(_tick_count)
            // , _arrow(_current_ts->time_since_epoch() % _wheel_period.count() / _tick_duration)
            {};

        bool subscribe(async<>&& ctx, duration_t dur) {
            if (dur > _wheel_period) [[unlikely]] return false;
            const auto arrow_offset = dur / _tick_duration;
            _ticks[_arrow + arrow_offset]._records.push(std::forward<clock_record>({std::forward<async<>>(ctx), dur}));
            return true;
        }

        // WARNING DOESNT USE RELEASE COUNTER
        void insert_upper_record(clock_record&& record) {
            record._duration -= _wheel_period;
            if (record._duration.count() < 0) [[unlikely]] {
                runner::schedule(std::forward<async<>>(record._context));
                return;
            }
            const auto arrow_offset = record._duration / _tick_duration;
            _ticks[_arrow + arrow_offset]._records.push(std::forward<clock_record>(record));
        }

        int release_ticks(std::size_t passed_ticks) {
            int arrow_offset = 0;
            while (arrow_offset < passed_ticks and *_release_counter > 0) {
                auto arrow = (_arrow + arrow_offset) % _tick_count;
                ++arrow_offset;
                // if (_ticks[arrow].empty()) continue;
                *_release_counter -= _ticks[arrow].release_slot(*_release_counter);
                // _ticks[arrow].release_max();
                if (_ticks[arrow].empty())
                    pump_time(arrow_offset);
                else break;
            }
            _arrow += arrow_offset;
            return arrow_offset;
        }

        int release(const duration_t& interval) {
            return release_ticks(interval / _tick_duration);
        }

        // NOTE: Pumps time from upper wheel by ticking its arrow
        void pump_time(std::size_t offset = 0) {
            if ((_arrow + offset) % _tick_count == 0 and _upper_wheel) {
                _upper_wheel->advice_arrow(this);
            }
        }

        void advice_arrow(time_wheel* lower_wheel) {
            auto&& records = std::move(_ticks[_arrow % _tick_count]._records);
            _arrow++;
            while(not records.empty()) {
                clock_record record;
                if (records.pop(record))
                    lower_wheel->insert_upper_record(std::forward<clock_record>(record));
            }
            pump_time();
        }
    };

    struct wheel_cascade {

    private:

        timepoint_t _current_ts;
        timepoint_t _release_bound_ts {clock_now()}; ///< Time lower bound, higher than all released timers
        std::vector<time_wheel> _wheels;
        const duration_t _tick_duration;
        const std::size_t _tick_count;
        int _release_counter {};
        int _release_limit {5000000}; // NOTE Huinya kakayata
        std::atomic_int _total_awaited {0};

        static std::size_t log_based(std::size_t base, std::size_t x) {
            return static_cast<std::size_t>(std::log(x) / std::log(base));
        }

        [[nodiscard]] std::optional<std::size_t> calc_wheel(const duration_t dur) const {
            if (dur.count() == 0 or dur < _tick_duration) [[unlikely]]
                return std::nullopt;
            std::size_t dur_ticks = dur / _tick_duration;
            return log_based(_tick_count, dur_ticks);
        }

        [[nodiscard]] auto calc_passed() const {
            return _current_ts.time_since_epoch() - _release_bound_ts.time_since_epoch();
        }

        void update_release_bound(duration_t passed_interval) {
            _release_bound_ts += passed_interval;
        }

    public:

        template <typename rep_t, typename period_t>
        explicit wheel_cascade(const std::chrono::duration<rep_t, period_t> tick_duration,
                               const std::size_t tick_count)
            : _current_ts(clock_now())
            , _release_bound_ts(_current_ts)
            ,  _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0)
                              ? tick_count
                              : std::bit_ceil(tick_count)) {
            const auto ticks_amount = UINT64_MAX / tick_duration.count();
            const auto wheels_amount = log_based(_tick_count, ticks_amount);
            _wheels.reserve(wheels_amount);
            for (int i = 0; i < wheels_amount; ++i)
                _wheels.emplace_back(_tick_duration, _tick_count, &_current_ts, &_release_counter);
            for (int i = 0; i < wheels_amount - 1; ++i)
                _wheels[i]._upper_wheel = &_wheels[i + 1];
        };

        std::size_t release() {
            _current_ts = clock_now();
            const duration_t passed = calc_passed();
            if (passed < _tick_duration) [[unlikely]]
                return 0;
            _release_counter = _release_limit;
            const auto released_ticks = _wheels[0].release(passed);
            update_release_bound(_tick_duration * released_ticks);
            const auto released = _release_limit - _release_counter;
            _total_awaited.fetch_sub(released, std::memory_order_relaxed);
            return released;
        };

        void subscribe(async<>&& ctx, duration_t dur) {
            auto idx = calc_wheel(dur);

            if (not idx) [[unlikely]] {
                runner::schedule(std::move(ctx));
                return;
            }

            _total_awaited.fetch_add(1, std::memory_order_relaxed);
            _wheels[idx.value()].subscribe(std::forward<async<>>(ctx), dur);
        };

        [[nodiscard]] auto current_time() const { return _current_ts; }

        [[nodiscard]] bool empty() const {
            return _total_awaited.load(std::memory_order_relaxed) <= 0;
        }

    };

    struct clock : singleton_service_traits<clock> {

        clock() = default;

        // TODO: Add record detach
        promise<bool> service_yank() {
            _core.release();
            co_return _core.empty();
        }

        void subscribe(async<>&& ctx, duration_t dur) {
            _core.subscribe(std::forward<async<>>(ctx), dur);
        };

        static auto current_time() {
            return get_instance()._core.current_time();
        }

        wheel_cascade _core{std::chrono::milliseconds(50), 1000};
    };

}

#endif //ACE_CLOCK_H
