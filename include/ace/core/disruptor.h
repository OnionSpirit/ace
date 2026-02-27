/**
 * @file
 * @details This file contains a @b disruptor class.
 * @b Disruptor is a vortex object that cures cutex ruptures.
 * @b Cutex rupture case:
 *  - thread @b A owns @b cutex
 *  - thread @b B tries to capture @b cutex but didn't receive success.
 *  - thread @b B going to sign up into @b cutex @b waiters queue
 *  - OS interrupts thread @b B, before it signed up.
 *  - thread @b A making @b cutex sync operation
 *  - @b cutex @b waiters queue is empty (thread @b B didn't finish signing up before interruption) notify noone
 *  - @b cutex is vacant but @b B thread isn't notified and waits inside @b cutex @b waiters queue
 *  - Got rupture (notify sequence ruptured) @b B thread is forever blocked
 */
#ifndef ACE_CORE_DISRUPTOR_H
#define ACE_CORE_DISRUPTOR_H
#include <set>

#include "vortex.h"
#include "ace/coroutines/context.h"

namespace ace::futures { class cutex; }
namespace ace::core {

    class disruptor : public vortex_traits<disruptor, vortex_spawn_mode::e_shared> {

        disruptor() = default;

        friend vortex_traits;

        typedef std::pair<futures::cutex*, int> cutex_record_t;

        static constexpr  uint16_t max_detaches = UINT16_MAX;

    public:

        promise<bool> yank() {
            if (futures::cutex* cutex_ {}; _resolve_requests.pop(cutex_)) {
                if (not resolve(cutex_))
                    _resolve_requests.push(std::forward<futures::cutex*>(cutex_));
            }
            co_return not _resolve_requests.empty();
        }

        static void request_resolve(futures::cutex* cute) {
            _resolve_requests.push(std::forward<futures::cutex*>(cute));
            touch();
        }

        static nukes::dynamic::mpsc_queue<futures::cutex*> _resolve_requests;

        static inline bool resolve(futures::cutex* cutex_) noexcept;
    };

    nukes::dynamic::mpsc_queue<futures::cutex*> disruptor::_resolve_requests {};

}

#endif // ACE_CORE_DISRUPTOR_H
