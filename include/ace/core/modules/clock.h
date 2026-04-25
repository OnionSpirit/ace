/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CORE_CLOCK_H
#define ACE_CORE_CLOCK_H
#include <chrono>
#include <complex>
#include <list>

#include "ace/core/traits/vortex.h"
#include "ace/core/context.h"
#include "ace/core/tools/queue.h"

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
        static auto curr_ts = std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
        );
        static uint32_t counter = 0;
        ++counter;
        if (counter % 16 == 0) {
            curr_ts = std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now());
        }
        return curr_ts;

    }

}

namespace ace::core::modules {

    /**
     * @brief Type of stored context record. Contains context and it's awaiting duration
     */
    struct clock_record {
        duration_t _duration {};
        task _context;

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

        clock_record(task&& ctx, const duration_t dur)
            : _duration(dur)
            , _context(std::forward<task>(ctx)) {}

        static thread_local tools::slab_mempool<clock_record> _clock_record_mempool;
    };

    using clock_node = tools::q_node<clock_record>;

    /**
     * @brief Represents time dial slot. Storing records with same expiration time and provides release functionality
     */
    struct time_slot {

        time_slot() = default;

        time_slot(const time_slot& x) =delete;

        time_slot& operator=(const time_slot& x) =delete;

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
                release_record(std::forward<clock_record>(_records.dequeue()));
                ++released;
            }
            return released;
        }

        /**
         * @brief Releases all stored records
         */
        void release_slot() {
            while (not _records.empty())
                release_record(std::forward<clock_record>(_records.dequeue()));
        }

        /**
         * @brief @b ARE @b YOU @b DUMB @b ?!
         * @return Result of emptiness check operation
         */
        [[nodiscard]] bool empty() const { return _records.empty(); }

        tools::queue<clock_record> _records{clock_record::_clock_record_mempool}; ///< Queue of stored records
    };

    /**
     * @brief Abstraction of the Dial (Time Wheel)
     * @details
     */
    struct dial {

        ACE_CACHE_LINE(0)

        const std::size_t       _tick_count;
        std::vector<time_slot>  _dial;
        const duration_t        _tick_duration;
        const timepoint_t*      _current_ts;
        int*                    _release_counter;
        std::size_t             _arrow {0};

        ACE_CACHE_LINE(1)

        const duration_t        _dial_round;
        dial*                   _upper_dial;

        dial() = delete;

        template <typename rep_t, typename period_t>
        explicit dial(const std::chrono::duration<rep_t, period_t> tick_duration,
                            const std::size_t tick_count,
                            const timepoint_t* current_ts,
                            int* release_counter,
                            dial* upper_dial = nullptr)
            : _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0) ? tick_count : std::bit_ceil(tick_count))
            , _dial(_tick_count)
            , _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _current_ts(current_ts)
            , _release_counter(release_counter)
            , _dial_round(_tick_duration * _tick_count)
            , _upper_dial(upper_dial)
            {};

        /**
         * @brief Injects context to the dial. Slot will be selected by duration
         * @param [in] ctx Context to await
         * @param [in] dur Duration of awaiting
         * @return Injected node ptr
         */
        clock_node* inject_raw(task&& ctx, duration_t dur) {
            const auto arrow_offset = (dur / _tick_duration) % _tick_count;
            const auto arrow = (_arrow + arrow_offset) % _tick_count;
            return _dial.at(arrow)._records.enqueue(std::forward<clock_record>({std::forward<task>(ctx), dur}));
        }

        /**
         * @brief Injects record to the dial. Slot will be selected by duration
         * @param [in] node Record node to inject
         * @return Injected node ptr
         */
        clock_node* inject_node(clock_node&& node) {
            const auto arrow_offset = (node.data()->_duration / _tick_duration + 1) % _tick_count;
            const auto arrow = (_arrow + arrow_offset) % _tick_count;
            return _dial.at(arrow)._records.enqueue(std::forward<clock_node>(node));
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

                if (not _dial[arrow].empty()) [[unlikely]]
                    break;

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

        // NOTE: Pumps time from upper dial by ticking its arrow if current dial finished its round
        void migrate(const std::size_t offset = 0) {
            if ((_arrow + offset) % _tick_count == 0 and _upper_dial)
                _upper_dial->advance_arrow(this);
        }

        void advance_arrow(dial* lower_dial) {

            auto&& records =
                std::move(_dial[_arrow % _tick_count]._records);

            while(not records.empty())
                lower_dial->inject_node(std::forward<clock_node>(records.pop()));

            migrate();
            ++_arrow;
        }
    };

    struct multi_dial {

    private:

        typedef std::tuple<task, duration_t> input_record_t;

        ACE_CACHE_LINE(0)

        std::vector<dial>  _dials;
        timepoint_t        _current_ts;
        timepoint_t        _release_bound_ts { clock_now() }; ///< Time lower bound, higher than all released timers
        const duration_t   _tick_duration;
        const std::size_t  _tick_count;
        int                _release_counter  { };
        int                _release_limit    { 1024 };

        ACE_CACHE_LINE(1)

        std::size_t        _total_records    { 0 };
        bool               _stopped          { false };


        static std::size_t fast_log2(std::size_t x) {

            if (x == 0) [[unlikely]]
                throw std::runtime_error("can't calculate <log> from 0");

            return 63 - std::countl_zero(x);
        }

        // NOTE: Base replaces with the less power of 2
        static std::size_t fast_log(std::size_t x, std::size_t base = 2) {
            return (fast_log2(x) / fast_log2(base));
        }

        /**
         * @brief Selects dial depending on required duration
         * @param [in] dur Required wait time interval
         * @return dial id
         */
        [[nodiscard]] std::optional<std::size_t> select_dial(const duration_t dur) const {

            if (dur.count() == 0) [[unlikely]]
                return std::nullopt;

            if (dur < _tick_duration) [[unlikely]]
                return 0;

            const std::size_t dur_ticks = dur / _tick_duration;
            auto res = fast_log(dur_ticks, _tick_count);
            return res;
        }

        /**
         * @brief Calculates passed time
         * @return Passed time interval
         */
        [[nodiscard]] auto calc_passed() const {
            return _current_ts.time_since_epoch() - _release_bound_ts.time_since_epoch();
        }

        /**
         * @brief Updates release bound
         * @param [in] passed_interval Passed time interval to increase released bound timestamp
         */
        void update_release_bound(duration_t passed_interval) {
            _release_bound_ts += passed_interval;
        }

    public:

        template <typename rep_t, typename period_t>
        explicit multi_dial(const std::chrono::duration<rep_t, period_t> tick_duration,
                               const std::size_t tick_count)
            : _current_ts(clock_now())
            , _release_bound_ts(_current_ts)
            , _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _tick_count((tick_count > 0) && ((tick_count & (tick_count - 1)) == 0)
                              ? tick_count
                              : std::bit_ceil(tick_count)) {

            const auto ticks_amount = UINT64_MAX / tick_duration.count();
            // NOTE: Needs to increment because log function cuts off float reminder of log. We must handle all records
            const auto dials_amount = fast_log(ticks_amount, _tick_count) + 1;
            _dials.reserve(dials_amount);

            auto dur = _tick_duration;
            for (std::size_t i = 0; i < dials_amount; ++i, dur *= static_cast<long>(_tick_count))
                _dials.emplace_back(dur, _tick_count, &_current_ts, &_release_counter);

            for (std::size_t i = 0; i < (dials_amount - 1); ++i)
                _dials[i]._upper_dial = &_dials[i + 1];
        };


        /**
         * @brief Releases portion of subscribers depending on those expiration time
         * @return amount of the released subscribers
         */
        std::size_t release() {

            adjust();
            const duration_t passed = calc_passed();

            if (passed < _tick_duration) [[unlikely]]
                return 0;

            const auto released_ticks = _dials[0].release(passed);
            const auto released = _release_limit - _release_counter;
            _total_records -= released;
            update_release_bound(_tick_duration * released_ticks);
            return released;
        };

        /**
         * @brief Subscribes context to dial by passed current duration
         * @param [in] ctx context to subscribe
         * @param [in] dur subscription duration
         * @return Injected node ptr
         */
        clock_node* subscribe(task&& ctx, duration_t dur) {

            const auto idx = select_dial(dur);

            if (not idx) [[unlikely]] {
                runner::reattach(std::move(ctx));
                return nullptr;
            }
            ++_total_records;
            return _dials[idx.value()].inject_raw(std::forward<task>(ctx), dur);
        };

        /**
         * @brief Gets current timepoint
         * @return current timepoint
         */
        [[nodiscard]] auto current_time() const { return _current_ts; }

        /**
         * @brief Adjusting the multi-dial before next release
         */
        void adjust() {
            _current_ts = clock_now();
            _release_counter = _release_limit;
            // NOTE: If multi-dial didn't reach empty state and become stopped, then no effect.
            // NOTE: Else increasing with inactivity time
            _release_bound_ts += ((_current_ts - _release_bound_ts) * (_stopped & 0b1));
            _stopped = false;
        }

        /**
         * @brief @b ARE @b YOU @b DUMB @b ?!
         * @return Result of emptiness check operation
         */
        [[nodiscard]] bool empty() {
            return _stopped = _total_records == 0;
        }

        void detach_record(clock_node* node) { _total_records -= (node->remove() & 0b1); }

    };

    struct clock : traits::vortex_traits<clock, vortex_spawn_mode::e_thread_local> {

        clock() = default;

        static thread_local multi_dial _multi_dial;

        static auto current_time() { return inspect()._multi_dial.current_time(); }

        static auto detach(clock_node* node) { inspect()._multi_dial.detach_record(node); }

        [[nodiscard]] static clock_node* subscribe(task&& ctx, const duration_t dur) {
            return touch(ctx._coroutine.promise()._runner_pool)._multi_dial.subscribe(std::forward<task>(ctx), dur);
        };

        static bool ping() {
            _multi_dial.release();
            return not _multi_dial.empty();
        }
    };

    thread_local tools::slab_mempool<clock_record> clock_record::_clock_record_mempool =
        tools::slab_mempool<clock_record>();

    thread_local multi_dial clock::_multi_dial =
        multi_dial{std::chrono::milliseconds(1), 256};

}

#endif //ACE_CORE_CLOCK_H
