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
            while (not _detache.empty()) {
                _pool.erase(_detache.front());
                _detache.pop();
            }
            while (_attache.pop(cutex_) and not _pool.contains(cutex_))
                _pool.insert(cutex_);

            for (auto* ctx : _pool)
                if (is_detached(ctx)) _detache.push(ctx);
                else resolve(ctx);

            co_return not _pool.empty();
        }

        static void attach_cutex(futures::cutex* cutx) {
            _attache.push(std::forward<futures::cutex*>(cutx));
            attach();
        }

        std::set<futures::cutex*> _pool;
        std::queue<futures::cutex*> _detache;

        static nukes::dynamic::mpsc_queue<futures::cutex*> _attache;

        static inline void resolve(futures::cutex* cutex_) noexcept;

        static inline bool is_detached(futures::cutex* cutex_) noexcept;

        static inline bool is_empty_cutex(const futures::cutex* cutex_) noexcept;
    };

    nukes::dynamic::mpsc_queue<futures::cutex*> fixer::_attache {};

}

#endif // ACE_CORE_FIXER_H
