#ifndef ACE_VISUAL_GRAPH_H
#define ACE_VISUAL_GRAPH_H

#include <variant>
#include <optional>

#include "ace/core/tools/macro.h"
#include "ace/core/traits/future.h"
#include "ace/core/async.h"

#include "ace/visual/details/pipe.h"
#include "chain.h"

namespace ace::visual {

    // NOTE: Connect few branch into a single awaitable unit.
    template <
        completion_state completion_v = e_incomplete,
        typename connection_mode_t = void,
        typename ... brick_ts
    >
    struct ACE_AWAIT_NODISCARD graph_base final : brick_traits<completion_v> {

        static constexpr int pipelines_amount = sizeof...(brick_ts);
        static constexpr int top_pipeline_idx = pipelines_amount - 1;

        graph_base() = default;

        ~graph_base() override = default;

        explicit graph_base(std::tuple<brick_ts...>&& pipelines)
            : _pipelines(std::forward<std::tuple<brick_ts...>>(pipelines)) {};

        std::tuple<brick_ts...> _pipelines;

        template <is_brick>
        auto operator () (auto &&pipeline) {
            using namespace details;
            typedef std::decay_t<decltype(pipeline)> pipeline_t;
            if constexpr (sizeof...(brick_ts) > 0) {
                if constexpr (pipeline_t::status == e_complete) {
                    return graph_base<e_complete, connection_mode_t, brick_ts..., pipeline_t> {
                        std::forward<std::tuple<brick_ts..., pipeline_t>>( std::tuple_cat(
                            std::forward<std::tuple<brick_ts...>>(_pipelines),
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))))
                    };
                } else {
                    return graph_base<e_incomplete, connection_mode_t, brick_ts..., pipeline_t> {
                        std::forward<std::tuple<brick_ts..., pipeline_t>>( std::tuple_cat(
                            std::forward<std::tuple<brick_ts...>>(_pipelines),
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))))
                    };
                }
            } else {
                if constexpr (pipeline_t::status == e_complete)
                    return graph_base<e_complete, connection_mode_t, pipeline_t> {
                            std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))
                    };
                else
                    return graph_base<e_incomplete, connection_mode_t, pipeline_t> {
                        std::forward<std::tuple<pipeline_t>>(std::tie(pipeline))
                    };
            }
        }

        task start() override {
            co_await [&] <std::size_t ... index> (std::index_sequence<index...>) -> task {
                (..., co_await [&] -> task {
                    co_await post(std::get<index>(_pipelines).start());
                }());
            }(std::make_index_sequence<sizeof...(brick_ts)>{});
            co_return;
        }
    };

}

namespace visual {

    inline auto graph() { return ace::visual::graph_base{}; }

}

#endif //ACE_VISUAL_GRAPH_H
