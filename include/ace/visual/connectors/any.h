#ifndef ACE_VISUAL_CONNECTORS_ANY_H
#define ACE_VISUAL_CONNECTORS_ANY_H

#include <tuple>
#include <vector>

#include "ace/core/async_handle.h"
#include "ace/core/runner.h"
#include "ace/visual/units/unit.h"

namespace ace::visual::connectors {

    // NOTE: Marks graph to await any branch without canceling others on the single branch completion.
    // NOTE: Results of the other branches will be dropped
    struct [[deprecated("working on it")]] any {

        std::vector<std::optional<core::async_handle> > _observers;

        template<std::size_t len_v>
        auto connect(std::array<std::optional<core::async_handle>, len_v> &&units, core::runner *runner_ptr) {
            // NOTE: Creating observers for each unit
            _observers.resize(len_v);
            [&] <std::size_t ... index> (std::index_sequence<index...>) {
                (...,[&]{
                    task observer_inst = observer<index, len_v>(units[index].value());
                    // NOTE: Creating Handlers for observation tasks
                    _observers[index] = core::async_handle{observer_inst.observe()};
                    // NOTE: Posting observer
                    observer_inst._coroutine.promise()._roaming = false;
                    runner_ptr->attach_front(std::forward<task>(observer_inst));
                }());
            }(std::make_index_sequence<len_v>{});
            return _observers[len_v - 1]->join();
        }

        template<size_t observer_idx, size_t top_observer_idx>
        task observer(core::async_handle unit_processor) {
            co_await unit_processor.join();

            // NOTE: Only last observer joins and reattaches
            for (int i = 0; i < top_observer_idx; ++i) {
                if (i not_eq observer_idx)
                    _observers[i]->cancel();
            }
        };

    };

}

#endif //ACE_VISUAL_CONNECTORS_ANY_H
