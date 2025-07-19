/**
 * @file
 * @details This file contains AwaitableLock declaration
 */
#ifndef RIOT_AWAITABLE_LOCK_H
#define RIOT_AWAITABLE_LOCK_H

#include "riot.h"


namespace riot::async {

/**
 * @details Synchronization primitive that supports async call operator @b(co_await)
 * @tparam PrimitiveStrategy Synchronization primitive @b Strategy
 * that current class bases on
 * @tparam ManagementStrategy coroutine processing @b Strategy.
 * @b Ordered will hide blocked coroutines, and release them to execution
 * when lock will be freed, @b Unordered will make @b Scheduler spin-poll all
 * suspended coroutines on this object, until it would let them continue
 */
template
<
    typename PrimitiveStrategy = riot::component_modes::async::lock::primitive_strategy::atomic,

    template <typename>
    typename ManagementStrategy = riot::component_modes::async::lock::management_strategy::ordered
>
requires riot::component_modes::async::lock::primitive_strategy::ModeRequirements<PrimitiveStrategy>
    and riot::component_modes::async::lock::management_strategy::ModeRequirements< ManagementStrategy<void> >
class lock :
        public PrimitiveStrategy,
        public awaitable<lock<PrimitiveStrategy, ManagementStrategy>, ManagementStrategy> {

public:

    bool ready() { return PrimitiveStrategy::impl_try_capture(); }

    bool suspend(auto) { return not PrimitiveStrategy::impl_try_capture(); }

    void resume() {}

    /**
     * @details Async lock capture @b(co_await required)
     */
    lock &capture() noexcept { return *this; }

    /**
     * @details Basic blocking lock capture
     */
    void force_capture() noexcept { PrimitiveStrategy::impl_force_capture(); }

    explicit operator bool () { return PrimitiveStrategy::impl_check_capture(); }
};

} // end namespace riot::async


/**
 * @details Synchronization primitive that supports async call operator @b(co_await).
 * Specialization of @b lock that based on @b atomic primitive,
 * and uses @b ordered strategy
 */
typedef riot::async::lock<riot::component_modes::async::lock::primitive_strategy::atomic,
        riot::component_modes::async::lock::management_strategy::ordered> lock_atomic_ordered;

/**
 * @details Synchronization primitive that supports async call operator @b(co_await)
 * Specialization of @b lock that based on @b atomic primitive,
 * and uses @b unordered strategy
 */
typedef riot::async::lock<riot::component_modes::async::lock::primitive_strategy::atomic,
        riot::component_modes::async::lock::management_strategy::unordered> lock_atomic_unordered;

/**
 * @details Synchronization primitive that supports async call operator @b(co_await)
 * Specialization of @b lock that based on @b mutex primitive,
 * and uses @b ordered strategy
 */
typedef riot::async::lock<riot::component_modes::async::lock::primitive_strategy::mutex,
        riot::component_modes::async::lock::management_strategy::ordered> lock_mutex_ordered;

/**
 * @details Synchronization primitive that supports async call operator @b(co_await)
 * Specialization of @b lock that based on @b mutex primitive,
 * and uses @b unordered strategy
 */
typedef riot::async::lock<riot::component_modes::async::lock::primitive_strategy::mutex,
        riot::component_modes::async::lock::management_strategy::unordered> lock_mutex_unordered;

#endif // RIOT_AWAITABLE_LOCK_H
