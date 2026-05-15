#ifndef ACE_VISUAL_GRAPH_H
#define ACE_VISUAL_GRAPH_H

#include <variant>

#include "ace/core/tools/macro.h"
#include "ace/core/async.h"
#include "ace/visual/details/pipe.h"

#include "unit.h"

namespace ace::visual {

    // NOTE: Connect few branch into a single awaitable unit.
    template <
        completion_state completion_v = e_incomplete,
        typename connection_mode_t = void,
        typename ... unit_ts
    >
    struct ACE_AWAIT_NODISCARD graph_unit final : unit_traits<completion_v> {

        static constexpr int pipelines_amount = sizeof...(unit_ts);
        static constexpr int top_pipeline_idx = pipelines_amount - 1;

        graph_unit() = default;

        ~graph_unit() override = default;

        explicit graph_unit(std::tuple<unit_ts...>&& pipelines)
            : _pipelines(std::forward<std::tuple<unit_ts...>>(pipelines)) {};

        std::tuple<unit_ts...> _pipelines;

        template <is_unit pipeline_t>
        auto operator () (pipeline_t &&pipeline) {
            using namespace details;
            if constexpr (sizeof...(unit_ts) > 0) {
                if constexpr (pipeline_t::status == e_complete) {
                    return graph_unit<e_complete, connection_mode_t, unit_ts..., pipeline_t> {
                        std::forward<std::tuple<unit_ts..., pipeline_t>>( std::tuple_cat(
                            std::forward<std::tuple<unit_ts...>>(_pipelines),
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))))
                    };
                } else {
                    return graph_unit<e_incomplete, connection_mode_t, unit_ts..., pipeline_t> {
                        std::forward<std::tuple<unit_ts..., pipeline_t>>( std::tuple_cat(
                            std::forward<std::tuple<unit_ts...>>(_pipelines),
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))))
                    };
                }
            } else {
                if constexpr (pipeline_t::status == e_complete)
                    return graph_unit<e_complete, connection_mode_t, pipeline_t> {
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))
                    };
                else
                    return graph_unit<e_incomplete, connection_mode_t, pipeline_t> {
                        std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))
                    };
            }
        }

        task start() override {
            co_await [&] <std::size_t ... index> (std::index_sequence<index...>) -> task {
                (..., co_await [&] -> task {
                    co_await post(std::get<index>(_pipelines).start());
                }());
            }(std::make_index_sequence<sizeof...(unit_ts)>{});
            co_return;
        }
    };

}

namespace ace {

    inline auto graph() { return visual::graph_unit{}; }

}

#endif //ACE_VISUAL_GRAPH_H
