/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CORE_FIXER_H
#define ACE_CORE_FIXER_H
#include <set>

#include "service.h"
#include "ace/coroutines/context.h"
#include "ace/futures/timer.h"


namespace ace::futures { class cutex; }
namespace ace::core {

    enum class cutx_state : uint8_t{
        e_free,
        e_captured,
        e_pending,
    };

    class fixer : public service_traits<fixer> {

        fixer() = default;

        friend service_traits;

    public:

        promise<bool> service_yank() {
            futures::cutex* cutex_;
            while (not _detache.pop(cutex_))
                _pool.erase(cutex_);
            while (not _attache.pop(cutex_))
                _pool.insert(cutex_);
            for (auto* cutex__ : _pool)
                resolve(cutex__);
            co_await futures::timer(15ms);
            co_return _pool.empty();
        }

        static void attach_cutex(futures::cutex* cutx) {
            get_instance()._attache.push(std::move(cutx));
        }

        static void detach_cutex(futures::cutex* cutx) {
            get_instance()._detache.push(std::move(cutx));
        }

        std::set<futures::cutex*> _pool;
        nukes::dynamic::mpsc_queue<futures::cutex*> _attache;
        nukes::dynamic::mpsc_queue<futures::cutex*> _detache;

        static inline bool resolve(futures::cutex* cutex_) noexcept;
    };

}


inline bool ace::core::fixer::resolve(futures::cutex* cutex_) noexcept {
    uint8_t* v_cutex = reinterpret_cast<uint8_t*>(cutex_);
    const auto c_state = reinterpret_cast<std::atomic<cutx_state>*>(v_cutex);
    const auto c_waiters = reinterpret_cast<nukes::dynamic::mpmc_queue<async<>>*>(v_cutex + sizeof(cutx_state));

    bool state_changed { false };
    auto state = c_state->load(std::memory_order_acquire);

    // NOTE: Checking if state is 'free'
    if (state == cutx_state::e_free) [[unlikely]] {
        // NOTE: Using 'weak' version because 'strong' option consumes more time
        // NOTE: and logic wont break if haven't captured 'free' state
        state_changed = c_state->compare_exchange_weak(state, cutx_state::e_pending,
            std::memory_order_release, std::memory_order_relaxed);
        state = cutx_state::e_pending;
    } else return false;

    // NOTE: If we have changed the state then trying to pull and reattach next waiter
    if (async<> _waiter; state_changed and c_waiters->pop(_waiter)) [[unlikely]] {
        core::runner::reattach(std::move(_waiter));
        return true;
    }

    // NOTE: If we didn't pull waiter successfully but state changed,
    // NOTE: then restoring state to 'free' if it wasn't changed
    if (state_changed) [[unlikely]] {
        // NOTE: Using 'strong' version because 'weak' one may skip equality (state == e_free)
        c_state->compare_exchange_strong(state, cutx_state::e_free,
            std::memory_order_release, std::memory_order_relaxed);
    }
    return false;
}

#endif // ACE_CORE_FIXER_H
