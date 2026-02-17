/**
 * @file
 * @details This file contains a @b clock class, that provides service for time-dependent operations.
 * Clock handles global timestamp and releases passed tasks to those runners when time comes.
 */
#ifndef ACE_CORE_FIXER_H
#define ACE_CORE_FIXER_H
#include <set>

#include "vortex.h"
#include "ace/coroutines/context.h"
#include "ace/futures/cutex.h"
#include "ace/futures/timeout.h"

namespace ace::futures { class cutex; }
namespace ace::core {

    // TODO: FIX THIS SHIT
    class fixer : public vortex_traits<fixer, shared_spawn_mode> {

        fixer() = default;

        friend vortex_traits;

    public:

        promise<bool> yank() {
            futures::cutex* cutex_;
            while (_detache.pop(cutex_) and _pool.contains(cutex_)) {
                // std::cout << "Cutex " << cutex_ << " detached\n";
                _pool.erase(cutex_);
            }
            while (_attache.pop(cutex_) and not _pool.contains(cutex_)) {
                // std::cout << "Cutex " << cutex_ << " attached\n";
                _pool.insert(cutex_);
            }
            for (auto* ctx : _pool) {
                if (not resolve(ctx)) detach(ctx);
            }
            co_return not _pool.empty();
        }

        static void attach_cutex(futures::cutex* cutx) {
            // if (not get_instance()._pool.contains(cutx))
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

        static inline void detach(futures::cutex* cutex_) noexcept;

        static inline bool is_empty_cutex(const futures::cutex* cutex_) noexcept;
    };

}

#endif // ACE_CORE_FIXER_H
