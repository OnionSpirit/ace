#ifndef ACE_VISUAL_GRAPH_H
#define ACE_VISUAL_GRAPH_H

#include <variant>
#include <optional>

#include "ace/core/tools/macro.h"
#include "ace/core/traits/future.h"
#include "ace/core/async.h"

#include "pipe.h"

namespace ace::visual {

    enum graph_state {
        e_complete,
        e_incomplete,
    };

    // NOTE: Connect few pipelines into a single awaitable unit. Actually a set of parallel pipelines. Graph is also a pipeline itself
    template <
        graph_state completion_v = e_incomplete,
        typename merge_mode_t = void,
        core::meta::is_future ... pipeline_ts
    >
    struct ACE_AWAIT_NODISCARD graph final : core::traits::future_traits<graph<completion_v, merge_mode_t, pipeline_ts...>> {

        IMPORT_FUTURE_ENV(graph);

        struct graph_conductor;
        friend graph_conductor;

        static constexpr int pipelines_amount = sizeof...(pipeline_ts);
        static constexpr int top_pipeline_idx = pipelines_amount - 1;

        explicit graph(pipeline_ts&... futures)
            : _pipelines(futures...) {};

        task _waiter;
        std::tuple<pipeline_ts&...> _pipelines;

        bool await_suspend(auto);

        pipe await_resume();
    };

    template<core::meta::is_future sender_t, typename async_return, core::is_promise_rule async_promise_rule_t =core::differed>
    requires (not std::same_as<core::meta::resume_type<sender_t>, void>)
    async<async_return, async_promise_rule_t> receiver(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)(core::meta::resume_type<sender_t>)) {
        co_await responder(co_await sender);
    }

    template<core::meta::is_future sender_t, typename async_return, core::is_promise_rule async_promise_rule_t =core::differed>
    requires std::same_as<core::meta::resume_type<sender_t>, void>
    async<async_return, async_promise_rule_t> receiver(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)()) {
        co_await sender;
        co_await responder();
    }


    template<ace::core::meta::is_future sender_t, typename async_return, ace::core::is_promise_rule async_promise_rule_t =ace::core::differed>
    requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
    ace::core::async<async_return, async_promise_rule_t> operator | (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)(ace::core::meta::resume_type<sender_t>) ) {
        return std::move(receiver(std::forward<decltype(sender)>(sender), responder));
    }

    template<ace::core::meta::is_future sender_t, typename async_return, ace::core::is_promise_rule async_promise_rule_t =ace::core::differed>
    requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
    ace::core::async<async_return, async_promise_rule_t> operator | (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)() ) {
        return std::move(receiver(std::forward<decltype(sender)>(sender), responder));
    }
}

#endif //ACE_VISUAL_GRAPH_H
