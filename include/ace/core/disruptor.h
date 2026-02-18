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

    public:

        promise<bool> yank() {
            futures::cutex* cutex_ {};
            std::queue<futures::cutex*> _detache {};
            while (_attache.pop(cutex_) and not _pool.contains(cutex_))
                _pool.insert(cutex_);

            for (auto* ctx : _pool) {
                resolve(ctx);
                if (is_detached(ctx)) _detache.push(ctx);
            }
            while (not _detache.empty()) {
                _pool.erase(_detache.front());
                _detache.pop();
            }
            co_return not _pool.empty();
        }

        static void attach_cutex(futures::cutex* cute) {
            _attache.push(std::forward<futures::cutex*>(cute));
            touch();
        }

        std::set<futures::cutex*> _pool;

        static nukes::dynamic::mpsc_queue<futures::cutex*> _attache;

        static inline void resolve(futures::cutex* cutex_) noexcept;

        static inline bool is_detached(const futures::cutex* cutex_) noexcept;

        static inline bool is_empty_cutex(const futures::cutex* cutex_) noexcept;
    };

    nukes::dynamic::mpsc_queue<futures::cutex*> disruptor::_attache {};

}

#endif // ACE_CORE_DISRUPTOR_H
