/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CORE_CLOCK_H
#define ACE_CORE_CLOCK_H
#include <chrono>
#include <complex>

#include "service.h"
#include "ace/coroutines/context.h"

namespace ace::core {

    using timepoint_t = decltype(
        std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
        )
    );

    using duration_t = decltype(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::duration()
        )
    );

    inline auto clock_now() {
        return
        std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
        );
    }

    /**
     * @brief Type of stored context record. Contains context and it's awaiting duration
     */
    struct clock_record {
        duration_t _duration {};
        async<> _context;

        clock_record() = default;

        clock_record(const clock_record&) = delete;

        clock_record operator= (const clock_record&) = delete;

        clock_record(clock_record&& clk_rec) noexcept {
            this->_duration = clk_rec._duration;
            this->_context = std::move(clk_rec._context);
        };

        clock_record& operator=(clock_record&& clk_rec) noexcept {
            this->_duration = clk_rec._duration;
            this->_context = std::move(clk_rec._context);
            return *this;
        };

        clock_record(async<>&& ctx, const duration_t dur)
            : _duration(dur)
            , _context(std::forward<async<>>(ctx)) {}

    };


    /**
     * @brief Represents time wheel slot. Storing records with same expiration time and provides release functionality
     */
    struct time_slot {

        time_slot() = default;

        time_slot(const time_slot& x) =delete;

        time_slot& operator=(const time_slot& x) =delete;

        time_slot(time_slot&& x) noexcept {
            this->_records = std::move(x._records);
        }

        time_slot& operator=(time_slot&& x) noexcept {
            this->_records = std::move(x._records);
            return *this;
        }

        /**
         * @brief Releases passed record to scheduler
         * @param [in] record Clock record to release
         * @warning May cause cross-runner roaming in future updates
         */
        static void release_record(clock_record&& record) {
            runner::reattach(std::move(record._context));
        }

        /**
         * @brief Releases not more than @b allowed_releases count of clock records
         * @param [in] allowed_releases Max allowed releases
         * @return Amount of released records
         */
        int release_slot(int allowed_releases) {
            int released =0;
            while (not _records.empty() and released < allowed_releases) {
                release_record(std::forward<clock_record>(_records.front()));
                _records.pop();
                ++released;
            }
            return released;
        }

        /**
         * @brief Releases all stored records
         */
        void release_slot() {
            while (not _records.empty()) {
                release_record(std::forward<clock_record>(_records.front()));
                _records.pop();
            }
        }

        /**
         * @brief @b ARE @b YOU @b DUMB @b ?!
         * @return Result of emptiness check operation
         */
        [[nodiscard]] bool empty() const { return _records.empty(); }

        std::queue<clock_record> _records; ///< Queue of stored records
    };

    /**
     * @brief Abstraction of the Dial (Time Wheel)
     * @details
     */
    struct time_wheel {

        const duration_t _tick_duration;
        const std::size_t _tick_count;
        const timepoint_t* _current_ts;
        const duration_t _wheel_period;

        int* _release_counter;
        time_wheel* _upper_wheel;
        std::vector<time_slot> _dial;
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
            , _dial(_tick_count)
            {};

        /**
         * @brief Injects context to the wheel. Slot will be selected by duration
         * @param [in] ctx Context to await
         * @param [in] dur Duration of awaiting
         */
        void inject_raw(async<>&& ctx, duration_t dur) {
            const auto arrow_offset = (dur / _tick_duration) % _tick_count;
            const auto arrow = (_arrow + arrow_offset) % _tick_count;
            _dial.at(arrow)._records.push(std::forward<clock_record>({std::forward<async<>>(ctx), dur}));
        }

        /**
         * @brief Injects record to the wheel. Slot will be selected by duration
         * @param [in] record Record to inject
         */
        void inject_record(clock_record&& record) {
            const auto arrow_offset = (record._duration / _tick_duration + 1) % _tick_count;
            const auto arrow = (_arrow + arrow_offset) % _tick_count;
            _dial.at(arrow)._records.push(std::forward<clock_record>(record));
        }

        /**
         * @brief Releases all slots pointed to by the arrow on its way to completing the specified number of steps (passed_ticks).
         * @warning Has two side effects:
         * 1 - Dependent on the current position of the arrow;
         * 2 - Constrained by the _release_counter class attribute.
         * This attribute represents the number of released entries and can prevent the arrow from passing.
         * @param [in] passed_ticks — The number of ticks passed. Also, the target number of arrow steps.
         * @return The number of completed arrow steps.
         */
        std::size_t release_ticks(std::size_t passed_ticks) {
            std::size_t arrow_offset = 0;
            while (arrow_offset < passed_ticks and *_release_counter > 0) {
                auto arrow = (_arrow + arrow_offset) % _tick_count;
                *_release_counter -= _dial[arrow].release_slot(*_release_counter);
                if (not _dial[arrow].empty()) [[unlikely]] break;
                migrate(arrow_offset);
                ++arrow_offset;
            }
            _arrow += arrow_offset;
            return arrow_offset;
        }

        /**
         * @brief Releases all slots inside passed @b interval which means time duration from @b past to @b now
         * @param interval A time interval that is treated as passed
         * @return The number of completed arrow steps.
         */
        auto release(const duration_t& interval) {
            return release_ticks(interval / _tick_duration);
        }

        // NOTE: Pumps time from upper wheel by ticking its arrow if current wheel finished its round
        void migrate(const std::size_t offset = 0) {
            if ((_arrow + offset) % _tick_count == 0 and _upper_wheel) {
                _upper_wheel->advance_arrow(this);
            }
        }

        void advance_arrow(time_wheel* lower_wheel) {
            auto&& records = std::move(_dial[_arrow % _tick_count]._records);
            while(not records.empty()) {
                lower_wheel->inject_record(std::forward<clock_record>(records.front()));
                records.pop();
            }
            migrate();
            ++_arrow;
        }
    };

    struct wheel_cascade {

    private:

        typedef std::tuple<async<>, duration_t> input_record_t;

        timepoint_t _current_ts;
        timepoint_t _release_bound_ts { clock_now() }; ///< Time lower bound, higher than all released timers
        std::vector<time_wheel> _wheels;
        const duration_t _tick_duration;
        const std::size_t _tick_count;
        int _release_counter {};
        int _release_limit {1024};
        std::size_t _total_records {0};
        nukes::dynamic::mpsc_queue<input_record_t> _threadsafe_input;

        static std::size_t fast_log2(std::size_t x) {
            if (x == 0) [[unlikely]] throw std::runtime_error("can't calculate <log> from 0");
            return 63 - std::countl_zero(x);
        }

        // NOTE: Base replaces with the less power of 2
        static std::size_t fast_log(std::size_t x, std::size_t base = 2) {
            return (fast_log2(x) / fast_log2(base));
        }

        [[nodiscard]] std::optional<std::size_t> calc_wheel(const duration_t dur) const {
            if (dur.count() == 0) [[unlikely]]
                return std::nullopt;
            if (dur < _tick_duration) [[unlikely]] return 0;
            std::size_t dur_ticks = dur / _tick_duration;
            auto res = fast_log(dur_ticks, _tick_count);
            return res;
        }

        [[nodiscard]] auto calc_passed() const {
            return _current_ts.time_since_epoch() - _release_bound_ts.time_since_epoch();
        }

        void update_release_bound(duration_t passed_interval) {
            _release_bound_ts += passed_interval;
        }

        void fetch(const duration_t& passed) {
            input_record_t input_record;
            for (int i = 0; i < _release_limit and _threadsafe_input.pop(input_record); ++i) {
                auto [ctx, dur] = std::forward<input_record_t>(input_record);
                if (passed > dur) [[unlikely]] {
                    runner::reattach(std::forward<async<>>(ctx));
                    --_release_counter;
                    continue;
                }
                subscribe(std::move(ctx), dur);
            }
        }

    public:

        template <typename rep_t, typename period_t>
        explicit wheel_cascade(const std::chrono::duration<rep_t, period_t> tick_duration,
                               const std::size_t tick_count)
            : _current_ts(clock_now())
            , _release_bound_ts(_current_ts)
            , _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0)
                              ? tick_count
                              : std::bit_ceil(tick_count)) {
            const auto ticks_amount = UINT64_MAX / tick_duration.count();
            // NOTE: Needs to increment because log function cuts off float reminder of log. We must handle all records
            const auto wheels_amount = fast_log(ticks_amount, _tick_count) + 1;
            _wheels.reserve(wheels_amount);
            auto dur = _tick_duration;
            for (std::size_t i = 0; i < wheels_amount; ++i, dur *= static_cast<long>(_tick_count))
                _wheels.emplace_back(dur, _tick_count, &_current_ts, &_release_counter);
            for (std::size_t i = 0; i < (wheels_amount - 1); ++i)
                _wheels[i]._upper_wheel = &_wheels[i + 1];
        };

        std::size_t release() {
            _current_ts = clock_now();
            const duration_t passed = calc_passed();
            _release_counter = _release_limit;
            if (passed < _tick_duration) [[unlikely]] {
                fetch(passed);
                return 0;
            }
            const auto released_ticks = _wheels[0].release(passed);
            update_release_bound(_tick_duration * released_ticks);
            const auto released = _release_limit - _release_counter;
            _total_records -= released;
            return released;
        };

        void subscribe(async<>&& ctx, duration_t dur) {
            const auto idx = calc_wheel(dur);
            if (not idx) [[unlikely]] {
                runner::reattach(std::move(ctx));
                return;
            }
            ++_total_records;
            _wheels[idx.value()].inject_raw(std::forward<async<>>(ctx), dur);
        };

        [[nodiscard]] auto current_time() const { return _current_ts; }

        /**
         * @brief Adjusting the cascade after non-polling period
         * @param last_touch Timestamp of the last cascade polling period
         */
        void adjust(timepoint_t last_touch) {
            update_release_bound(clock_now() - last_touch);
        }

        [[nodiscard]] bool empty() const {
            return _total_records == 0;
        }

    };

    struct clock : service_traits<clock> {

        clock() = default;

        std::optional<timepoint_t> _last_touch {};

        // TODO: Add record detach
        promise<bool> yank() {
            if (_last_touch) {
                _core.adjust(_last_touch.value());
                _last_touch.reset();
            }
            _core.release();
            if (_core.empty()) {
                _last_touch = _core.current_time();
                co_return false;
            } co_return true;
        }

        static void subscribe(async<>&& ctx, const duration_t dur) {
            attach(ctx._coroutine.promise()._runner_pool)._core.subscribe(std::forward<async<>>(ctx), dur);
        };

        static auto current_time() {
            return get_instance()._core.current_time();
        }

        static thread_local wheel_cascade _core;
    };

    thread_local wheel_cascade clock::_core = wheel_cascade{std::chrono::milliseconds(1), 256};

}

#endif //ACE_CORE_CLOCK_H
