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

    // TODO: FIX THIS SHIT
    class fixer : public service_traits<fixer> {

        fixer() = default;

        friend service_traits;

    public:

        promise<bool> yank() {
            futures::cutex* cutex_;
            while (_detache.pop(cutex_)) _pool.erase(cutex_);
            while (_attache.pop(cutex_)) _pool.insert(cutex_);
            for (auto* cutex__ : _pool) {
                co_await futures::timer(1ms);
                resolve(cutex__);
            }
            co_return true; // not _pool.empty();
        }

        static void attach_cutex(futures::cutex* cutx) {
            if (not get_instance()._pool.contains(cutx))
                get_instance()._attache.push(std::forward<futures::cutex*>(cutx));
        }

        static void detach_cutex(futures::cutex* cutx) {
            // if (not get_instance()._pool.contains(cutx))
            get_instance()._detache.push(std::forward<futures::cutex*>(cutx));
        }

        std::set<futures::cutex*> _pool;
        nukes::dynamic::mpsc_queue<futures::cutex*> _attache;
        nukes::dynamic::mpsc_queue<futures::cutex*> _detache;

        static inline bool resolve(futures::cutex* cutex_) noexcept;

        static inline bool is_empty_cutex(const futures::cutex* cutex_) noexcept;
    };

}

#endif // ACE_CORE_FIXER_H
