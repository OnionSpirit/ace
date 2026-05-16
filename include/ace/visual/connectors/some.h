#ifndef ACE_VISUAL_CONNECTORS_SOME_H
#define ACE_VISUAL_CONNECTORS_SOME_H

#include <tuple>
#include <vector>

#include "ace/core/async_handle.h"
#include "ace/core/runner.h"
#include "ace/visual/units/unit.h"

namespace ace::visual::connectors {

    // NOTE: Marks graph to await all branches and ignores failed branches
    struct some {
        std::vector<std::optional<core::async_handle> > _observers;

        template<std::size_t len_v>
        auto connect(std::array<std::optional<core::async_handle>, len_v> &&units, core::runner *runner_ptr) {
            // NOTE: Creating observers for each unit
            _observers.resize(len_v);
            [&] <std::size_t ... index>(std::index_sequence<index...>) {
                (..., [&] {
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

            // NOTE: Only last observer joins
            if constexpr (observer_idx == top_observer_idx) {
                for (auto &opposite_observer: _observers | std::views::take(top_observer_idx)) {
                    if (not opposite_observer.value().done())
                        co_await opposite_observer->join();
                }
            }

            co_return;
        };
    };
}

#endif //ACE_VISUAL_CONNECTORS_SOME_H
