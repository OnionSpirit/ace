#ifndef ACE_VISUAL_GRAPH_H
#define ACE_VISUAL_GRAPH_H

namespace ace::visual {

    // NOTE: Handler type for async graph
    struct graph {};

    // template<meta::is_future sender_t, typename async_return, is_promise_rule async_promise_rule_t =differed>
    // requires (not std::same_as<meta::resume_type<sender_t>, void>)
    // [[deprecated("feature moved to 'visual' module")]] async<async_return, async_promise_rule_t> receiver(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)(meta::resume_type<sender_t>)) {
    //     co_await responder(co_await sender);
    // }
    //
    // template<meta::is_future sender_t, typename async_return, is_promise_rule async_promise_rule_t =differed>
    // requires std::same_as<meta::resume_type<sender_t>, void>
    // [[deprecated("feature moved to 'visual' module")]] async<async_return, async_promise_rule_t> receiver(sender_t&& sender, async<async_return, async_promise_rule_t>(responder)()) {
    //     co_await sender;
    //     co_await responder();
    // }
    //
    //
    // template<ace::core::meta::is_future sender_t, typename async_return, ace::core::is_promise_rule async_promise_rule_t =ace::core::differed>
    // requires (not std::same_as<ace::core::meta::resume_type<sender_t>, void>)
    // ace::core::async<async_return, async_promise_rule_t> operator | (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)(ace::core::meta::resume_type<sender_t>) ) {
    //     return std::move(receiver(std::forward<decltype(sender)>(sender), responder));
    // }
    //
    // template<ace::core::meta::is_future sender_t, typename async_return, ace::core::is_promise_rule async_promise_rule_t =ace::core::differed>
    // requires std::same_as<ace::core::meta::resume_type<sender_t>, void>
    // ace::core::async<async_return, async_promise_rule_t> operator | (sender_t&& sender, ace::core::async<async_return, async_promise_rule_t>(responder)() ) {
    //     return std::move(receiver(std::forward<decltype(sender)>(sender), responder));
    // }
}

#endif //ACE_VISUAL_GRAPH_H
