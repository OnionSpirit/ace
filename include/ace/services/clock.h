/**
 * @file clock.h
 * @brief Hierarchical time wheel and clock vortex for O(1) amortized timer management.
 *
 * @details The clock module provides:
 *
 *  - <b>@c time_slot</b> — a single slot holding all timers expiring at the
 *    same tick.  Supports batched expiry.
 *  - <b>@c time_wheel</b> — a single level of the hierarchical wheel
 *    (@c slot_count slots per wheel).  Timers of the coarser (upper) wheels
 *    cascade down into finer (lower) ones as the hand wraps around.
 *  - <b>@c hierarchical_time_wheel</b> — the full hierarchical time wheel.
 *    Selects the appropriate level by timer duration (logarithmic wheel
 *    selection).  The default configuration uses 1ms ticks and 256-slot
 *    wheels, supporting timers up to the int64 millisecond range
 *    (~292 million years).
 *  - <b>@c clock</b> — a thread-local vortex that owns a
 *    @c hierarchical_time_wheel and calls @c advance() on each @c ping() to
 *    expire timers.
 *
 * ### How timeouts work
 *
 * 1. @c co_await timeout(500ms) creates a @c timeout future.
 * 2. @c await_suspend() installs a @c timeout_router.
 * 3. The runner calls @c router.redirect(node) → @c clock::subscribe(node, 500ms).
 * 4. @c hierarchical_time_wheel::subscribe() selects a wheel level and
 *    inserts the timer.
 * 5. When 500ms elapses, @c clock::ping() → @c hierarchical_time_wheel::advance()
 *    pops the timer and calls @c runner::reattach().
 *
 * @mermaid{ graph LR; Timeout[\"timeout(dur)\"]-->Router[\"timeout_router\"]; Router-->Subscribe[\"clock::subscribe\"]; Subscribe-->Wheel[\"hierarchical_time_wheel\"]; Wheel-->Slot[\"time_slot\"]; clock_ping[\"clock::ping()\"]-->Advance[\"wheel::advance\"]; Advance-->Reattach[\"runner::reattach\"]; }
 *
 * @see ace::futures::timeout, ace::core::traits::vortex_traits
 */
#ifndef ACE_SERVICES_CLOCK_H
#define ACE_SERVICES_CLOCK_H
#include <bit>
#include <chrono>

#include "ace/core/async.h"
#include "ace/core/traits/vortex.h"
#include "ace/core/tools/queue.h"

namespace ace::services {

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

    inline auto cached_now() {
        // NOTE: thread_local — the clock vortex is per-runner (per-thread), so the cached
        // timestamp and refresh counter must not be shared across threads (data race otherwise).
        thread_local auto cached_ts = std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
        );
        thread_local uint32_t refresh_counter = 0;
        ++refresh_counter;
        if (refresh_counter % 16 == 0) {
            cached_ts = std::chrono::time_point_cast<std::chrono::milliseconds, std::chrono::steady_clock, std::chrono::nanoseconds>(
            std::chrono::steady_clock::now());
        }
        return cached_ts;
    }

    /**
     * @brief A stored timer — holds a task and its absolute expiry time.
     *
     * @details @c _expires is the absolute deadline expressed on the wheel's
     * release-bound time scale.  Keeping the deadline (instead of the original
     * relative duration) lets every cascade recompute the exact remaining time,
     * independent of the insertion phase and of wheel stalls.
     */
    struct timer_record {
        timepoint_t _expires {};
        omni_node _context {};

        timer_record() = default;

        timer_record(const timer_record&) = delete;

        timer_record& operator=(const timer_record&) = delete;

        timer_record(timer_record&& other) noexcept {
            _expires = other._expires;
            _context = other._context;
        }

        timer_record& operator=(timer_record&& other) noexcept {
            _expires = other._expires;
            _context = other._context;
            return *this;
        }

        timer_record(const omni_node node, const timepoint_t expires)
            : _expires(expires)
            , _context(node) {}

        static thread_local core::tools::slab_mempool<timer_record> _timer_mempool;
    };

    using timer_node = core::tools::q_node<timer_record>;

    /**
     * @brief A single slot in the time wheel holding timers with the same expiry.
     */
    struct time_slot {

        time_slot() = default;

        time_slot(const time_slot&) = delete;

        time_slot& operator=(const time_slot&) = delete;

        /**
         * @brief Expires the passed timer, returning it to the scheduler.
         * @param [in] record Timer record to expire
         * @warning May cause cross-runner roaming in future updates
         */
        static void expire_record(timer_record&& record) {
            core::runner::reattach(record._context);
        }

        /**
         * @brief Expires not more than @b max_count stored timers
         * @param [in] max_count Max allowed expirations
         * @return Amount of expired timers
         */
        int expire_up_to(const int max_count) {

            int expired = 0;

            while (not _timers.empty() and expired < max_count) {
                expire_record(std::forward<timer_record>(_timers.dequeue()));
                ++expired;
            }
            return expired;
        }

        /**
         * @brief Expires all stored timers
         */
        void expire_all() {
            while (not _timers.empty())
                expire_record(std::forward<timer_record>(_timers.dequeue()));
        }

        /**
         * @return Whether the slot holds no timers
         */
        [[nodiscard]] bool empty() const { return _timers.empty(); }

        core::tools::queue<timer_record> _timers { timer_record::_timer_mempool }; ///< Queue of stored timers
    };

    struct hierarchical_time_wheel;

    /**
     * @brief A single level of the hierarchical time wheel.
     *
     * @details Each wheel contains @c _slot_count slots (power of 2, default 256).
     * When the hand wraps around, timers of the upper (coarser) wheel cascade
     * down into this one via @c cascade_slot().  An upper wheel pointer
     * (@c _upper_time_wheel) links the levels together.
     */
    struct time_wheel {

        ACE_CACHE_LINE(0)

        const std::size_t   _slot_count;
        std::vector<time_slot> _slots;
        const duration_t    _tick_duration;
        int*                _release_budget_ptr;
        std::size_t         _hand {0};

        ACE_CACHE_LINE(1)

        time_wheel*              _upper_time_wheel;
        hierarchical_time_wheel* _hierarchical;

        time_wheel() = delete;

        template <typename rep_t, typename period_t>
        explicit time_wheel(const std::chrono::duration<rep_t, period_t> tick_duration,
                            const std::size_t slot_count,
                            int* release_budget_ptr,
                            hierarchical_time_wheel* hierarchical,
                            time_wheel* upper_time_wheel = nullptr)
            : _slot_count((slot_count > 0) && ((slot_count & (slot_count - 1)) == 0) ? slot_count : std::bit_ceil(slot_count))
            , _slots(_slot_count)
            , _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _release_budget_ptr(release_budget_ptr)
            , _upper_time_wheel(upper_time_wheel)
            , _hierarchical(hierarchical)
            {};

        /**
         * @brief Inserts a new timer into the slot selected by an explicit hand offset.
         * @param [in] node Context to await
         * @param [in] expires Absolute expiry of the timer (release-bound scale)
         * @param [in] hand_offset Slot offset relative to the current hand
         * @return Inserted node ptr
         */
        timer_node* insert(omni_node node, const timepoint_t expires, const std::size_t hand_offset) {
            const auto slot = (_hand + hand_offset) % _slot_count;
            return _slots.at(slot)._timers.enqueue(std::forward<timer_record>({node, expires}));
        }

        /**
         * @brief Re-inserts an existing timer node into the slot selected by an explicit hand offset.
         * @param [in] node Timer node to re-insert
         * @param [in] hand_offset Slot offset relative to the current hand
         * @return Inserted node ptr
         */
        timer_node* insert(timer_node&& node, const std::size_t hand_offset) {
            const auto slot = (_hand + hand_offset) % _slot_count;
            return _slots.at(slot)._timers.enqueue(std::forward<timer_node>(node));
        }

        /**
         * @brief Expires all slots pointed to by the hand on its way to completing the specified number of steps (ticks).
         * @warning Has two side effects:
         * 1 - Dependent on the current position of the hand;
         * 2 - Constrained by the shared release budget.
         * The budget represents the number of expirations allowed per ping
         * and can prevent the hand from passing.
         * @param [in] ticks — The number of ticks passed. Also, the target number of hand steps.
         * @return The number of completed hand steps.
         */
        std::size_t advance_hand(const std::size_t ticks) {

            std::size_t hand_offset = 0;

            while (hand_offset < ticks and *_release_budget_ptr > 0) {
                const auto slot = (_hand + hand_offset) % _slot_count;
                *_release_budget_ptr -= _slots[slot].expire_up_to(*_release_budget_ptr);

                if (not _slots[slot].empty()) [[unlikely]]
                    break;

                cascade_on_wrap(hand_offset, hand_offset);
                ++hand_offset;
            }
            _hand += hand_offset;
            return hand_offset;
        }

        /**
         * @brief Expires all slots inside the passed @b interval, i.e. the time duration from @b past to @b now
         * @param interval A time interval that is treated as passed
         * @return The number of completed hand steps.
         */
        std::size_t advance(const duration_t& interval) {
            return advance_hand(interval / _tick_duration);
        }

        // NOTE: Pumps time from the upper wheel by ticking its hand if the current wheel finished its round
        // NOTE: release_progress is the amount of base ticks the advance loop has already
        // NOTE: moved in the current advance() call — needed by cascades to compute the
        // NOTE: exact remaining time from the timer's absolute deadline.
        void cascade_on_wrap(const std::size_t offset = 0, const std::size_t release_progress = 0) {
            if ((_hand + offset) % _slot_count == 0 and _upper_time_wheel)
                _upper_time_wheel->cascade_slot(this, release_progress);
        }

        void cascade_slot(time_wheel* lower_wheel, const std::size_t release_progress);
    };

    /**
     * @brief Hierarchical multi-level time wheel with O(1) amortized insert and advance.
     *
     * @details Composed of multiple @c time_wheel instances arranged in increasing
     * tick duration.  Timers are placed into the level that best matches their
     * duration (logarithmic wheel selection).  Each call to @c advance() moves
     * the finest wheel's hand by the number of ticks that have passed since the
     * last advance.
     *
     * The default configuration uses 1ms ticks and 256-slot wheels, supporting
     * timers up to the int64 millisecond range (~292 million years).
     */
    struct hierarchical_time_wheel {

    private:

        ACE_CACHE_LINE(0)

        std::vector<time_wheel> _time_wheels;
        timepoint_t             _current_ts;
        timepoint_t             _release_bound { cached_now() }; ///< Time lower bound, higher than all expired timers
        const duration_t        _tick_duration;
        const std::size_t       _slot_count;
        int                     _release_budget { };
        int                     _release_limit  { 1024 };

        ACE_CACHE_LINE(1)

        std::size_t             _pending_timer_count { 0 };
        bool                    _stopped            { false };


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
         * @brief Selects wheel level depending on required duration
         * @param [in] duration Required wait time interval
         * @return Wheel level index
         */
        [[nodiscard]] std::optional<std::size_t> select_time_wheel(const duration_t duration) const {

            if (duration.count() == 0) [[unlikely]]
                return std::nullopt;

            if (duration < _tick_duration) [[unlikely]]
                return 0;

            const std::size_t duration_ticks = duration / _tick_duration;
            auto level = fast_log(duration_ticks, _slot_count);
            // NOTE: Clamp to the actual wheel range (the top wheel's round fits int64)
            return std::min(level, _time_wheels.size() - 1);
        }

        /**
         * @brief Position of the wheels below @b wheel_index expressed in time, i.e. how
         * much of the current round of wheel @b wheel_index has already elapsed.
         *
         * @details O(1): every lower wheel's hand position is derived from the finest
         * wheel's hand, so the combined phase equals the finest hand modulo the
         * target wheel's tick.
         * @param [in] wheel_index Target wheel index
         * @return Elapsed part of the target wheel's round
         */
        [[nodiscard]] duration_t lower_wheel_phase(const std::size_t wheel_index) const {
            const auto base_tick = _time_wheels[0]._tick_duration.count();
            const auto ticks_per_tick = _time_wheels[wheel_index]._tick_duration.count() / base_tick;
            return duration_t(static_cast<long>(_time_wheels[0]._hand % ticks_per_tick) * base_tick);
        }

        /**
         * @brief Calculates passed time
         * @return Passed time interval
         */
        [[nodiscard]] duration_t elapsed() const {
            return _current_ts.time_since_epoch() - _release_bound.time_since_epoch();
        }

        /**
         * @brief Updates release bound
         * @param [in] interval Passed time interval to increase released bound timestamp
         */
        void advance_release_bound(duration_t interval) {
            _release_bound += interval;
        }

    public:

        template <typename rep_t, typename period_t>
        explicit hierarchical_time_wheel(const std::chrono::duration<rep_t, period_t> tick_duration,
                                         const std::size_t slot_count)
            : _current_ts(cached_now())
            , _release_bound(_current_ts)
            , _tick_duration(std::chrono::duration_cast<duration_t>(tick_duration))
            , _slot_count((slot_count > 0) && ((slot_count & (slot_count - 1)) == 0)
                              ? slot_count
                              : std::bit_ceil(slot_count)) {

            const auto ticks_amount = UINT64_MAX / tick_duration.count();
            // NOTE: Cap the amount of wheels so the top wheel's round (tick * count^n)
            // cannot overflow the int64 duration_t (signed overflow is UB).
            const auto max_round_ticks = INT64_MAX / tick_duration.count();
            const auto wheels_amount = std::min(fast_log(ticks_amount, _slot_count) + 1,
                                                fast_log(max_round_ticks, _slot_count));
            _time_wheels.reserve(wheels_amount);

            auto tick = _tick_duration;
            for (std::size_t i = 0; i < wheels_amount; ++i, tick *= static_cast<long>(_slot_count))
                _time_wheels.emplace_back(tick, _slot_count, &_release_budget, this);

            for (std::size_t i = 0; i < (wheels_amount - 1); ++i)
                _time_wheels[i]._upper_time_wheel = &_time_wheels[i + 1];
        }

        /**
         * @brief Advances the wheel by the elapsed time, expiring due timers
         * @return Amount of expired timers
         */
        std::size_t advance() {

            adjust();
            const duration_t passed = elapsed();

            if (passed < _tick_duration) [[unlikely]]
                return 0;

            const auto advanced_ticks = _time_wheels[0].advance(passed);
            const auto expired = _release_limit - _release_budget;
            _pending_timer_count -= expired;
            advance_release_bound(_tick_duration * advanced_ticks);
            return expired;
        }

        /**
         * @brief Subscribes a task to the wheel by the passed duration
         * @param [in] node Task to subscribe
         * @param [in] duration Subscription duration
         * @return Inserted node ptr
         */
        timer_node* subscribe(omni_node node, duration_t duration) {

            if (duration < duration_t::zero()) [[unlikely]]
                duration = duration_t::zero();

            const auto idx = select_time_wheel(duration);

            if (not idx) [[unlikely]] {
                core::runner::reattach(node);
                return nullptr;
            }
            ++_pending_timer_count;

            const timepoint_t expires = _release_bound + duration;
            const auto wheel_index = idx.value();

            if (wheel_index == 0) {
                const auto hand_offset = (duration / _time_wheels[0]._tick_duration) % _slot_count;
                return _time_wheels[0].insert(node, expires, hand_offset);
            }

            // NOTE: The upper wheel's hand advances once per round of the wheel below.
            // The first advance happens not exactly one round after insertion but after
            // (round - phase) where phase is the lower wheels' current position.  The hand
            // offset must therefore be computed from the next wrap, otherwise timers in
            // upper wheels expire up to one wheel tick late.
            const auto to_next_wrap = _time_wheels[wheel_index]._tick_duration - lower_wheel_phase(wheel_index);
            const auto hand_offset = static_cast<std::size_t>((duration - to_next_wrap) / _time_wheels[wheel_index]._tick_duration);
            return _time_wheels[wheel_index].insert(node, expires, hand_offset);
        }

        /**
         * @brief Re-inserts a cascaded timer into the wheel using its absolute deadline.
         * @param [in] node Timer node being cascaded from an upper wheel
         * @param [in] release_progress Base ticks already advanced in the current advance() call
         */
        void cascade(timer_node&& node, const std::size_t release_progress) {

            const timepoint_t now = _release_bound + duration_t(static_cast<long>(release_progress) * _tick_duration.count());
            duration_t remaining = node.data()->_expires - now;

            if (remaining < duration_t::zero()) [[unlikely]]
                remaining = duration_t::zero();

            const auto idx = select_time_wheel(remaining);
            const auto wheel_index = idx ? idx.value() : 0;

            if (wheel_index == 0) {
                // NOTE: +1 tick safety margin — the current slot has just been expired,
                // so a timer must never be placed into the position the hand is at now.
                const auto hand_offset = static_cast<std::size_t>((remaining / _time_wheels[0]._tick_duration + 1) % _slot_count);
                _time_wheels[0].insert(std::forward<timer_node>(node), hand_offset);
                return;
            }

            // NOTE: The lower wheel's hand is exactly at position 0 at the cascade moment,
            // and a timer placed at slot s is processed at the (s+1)-th advance, hence -1.
            // NOTE: The hand offset must be clamped to at least 1 — the slot the hand points
            // at right now is being drained by the ongoing cascade, so a timer inserted back
            // into it would be re-processed immediately (infinite loop) or, worse, never.
            const auto hand_offset = std::max<std::size_t>((remaining - _time_wheels[wheel_index]._tick_duration) / _time_wheels[wheel_index]._tick_duration, 1);
            _time_wheels[wheel_index].insert(std::forward<timer_node>(node), hand_offset);
        }

        /**
         * @brief Gets current timepoint
         * @return current timepoint
         */
        [[nodiscard]] auto current_time() const { return _current_ts; }

        /**
         * @brief Adjusting the wheel before the next advance
         */
        void adjust() {
            _current_ts = cached_now();
            _release_budget = _release_limit;
            // NOTE: If the wheel didn't reach empty state and become stopped, then no effect.
            // NOTE: Else increasing with inactivity time
            _release_bound += ((_current_ts - _release_bound) * (_stopped & 0b1));
            _stopped = false;
        }

        /**
         * @return Whether the wheel holds no pending timers
         */
        [[nodiscard]] bool empty() {
            return _stopped = _pending_timer_count == 0;
        }

        void detach(timer_node* node) {
            // NOTE: Pushing context back to the runner. It is already marked as canceled
            core::runner::reattach(node->data()->_context);
            _pending_timer_count -= (node->remove() & 0b1);
        }

    };

    inline void time_wheel::cascade_slot(time_wheel* lower_wheel, const std::size_t release_progress) {

        auto&& timers =
            std::move(_slots[_hand % _slot_count]._timers);

        while (not timers.empty())
            _hierarchical->cascade(std::forward<timer_node>(timers.pop()), release_progress);

        // NOTE: If this wheel finished its round, pump time from the upper wheel.
        // The wrap is detected on the pre-increment hand position, which is exactly
        // position 0 — the phase every cascade relies on.
        if (_hand % _slot_count == 0 and _upper_time_wheel)
            _upper_time_wheel->cascade_slot(this, release_progress);

        ++_hand;
    }

    /**
     * @brief Thread-local vortex that manages a @c hierarchical_time_wheel.
     *
     * @details On each @c ping(), calls @c hierarchical_time_wheel::advance() to
     * expire due timers.  Provides @c subscribe() (used by @c timeout future)
     * and @c detach() (for timer cancellation).  The @c current_time()
     * static method returns the cached timepoint, updated every 16 calls
     * for performance.
     */
    struct clock : core::traits::vortex_traits<clock, core::vortex_spawn_mode::e_thread_local> {

        clock() = default;

        static thread_local hierarchical_time_wheel _wheel;

        static auto current_time() { return inspect()._wheel.current_time(); }

        static auto detach(timer_node* node) { inspect()._wheel.detach(node); }

        [[nodiscard]] static timer_node* subscribe(omni_node node, const duration_t duration) {
            return touch(node->_data._coroutine.promise()._runner.as<runner_pool_t>())._wheel.subscribe(node, duration);
        }

        static bool ping() {
            _wheel.advance();
            return not _wheel.empty();
        }
    };

    inline thread_local core::tools::slab_mempool<timer_record> timer_record::_timer_mempool =
        core::tools::slab_mempool<timer_record>();

    inline thread_local hierarchical_time_wheel clock::_wheel =
        hierarchical_time_wheel { std::chrono::milliseconds(1), 256 };

}

#endif //ACE_SERVICES_CLOCK_H
