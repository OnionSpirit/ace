#ifndef ACE_VISUAL_PIPE_H
#define ACE_VISUAL_PIPE_H

#include <optional>

#include "ace/core/tools/macro.h"
#include "ace/core/traits/future.h"
#include "ace/core/traits/promise.h"
#include "ace/core/async_handle.h"

namespace ace::visual {

    enum class pipeline_state {
        e_idle,
        e_complete,
        e_broken,
    };

    // NOTE: Object to interact with the pipeline from Coroutine body
    template <typename output_t = void>
    struct pipe {

        pipeline_state _state = pipeline_state::e_idle;

        output_t _output;

        pipe interrupt() { _state = pipeline_state::e_broken; return this; }

        pipe resume() { _state = pipeline_state::e_complete; return this; }
    };

    // NOTE: Object to interact with the pipeline from Coroutine body
    template <>
    struct pipe<void> {

        pipeline_state _state = pipeline_state::e_idle;

        static pipe interrupt() { pipe p{}; p._state = pipeline_state::e_broken; return p;}

        static pipe resume() { pipe p{}; p._state = pipeline_state::e_complete; return p;}
    };

    // NOTE: Connector between sender and receiver. Intended for the branch members
    template <
        typename                    sender_output_t       ,
        typename                    receiver_return_t ,
        core::is_promise_rule       receiver_rule_t       ,
        typename                ... receiver_input_ts
    >
    struct ACE_AWAIT_NODISCARD receiver_base final
        : core::traits::future_traits<receiver_base<sender_output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...>> {

        IMPORT_FUTURE_ENV(receiver_base);

        template <typename raw_output>
        static consteval raw_output get_raw_output(pipe<raw_output>) {
            if constexpr (not std::same_as<raw_output, void>) return std::declval<raw_output>();
            else return;
        }

        receiver_base() = default;

        typedef decltype(get_raw_output(std::declval<receiver_return_t>())) raw_output_t;

        typedef receiver_return_t pipe_output_t;

        // template <typename ... sender_output_ts>
        // static consteval bool assert_compatibility(std::tuple<sender_output_ts...>) requires (not std::same_as<sender_output_t, void>){
        //     if constexpr (sizeof...(receiver_input_ts) == sizeof...(sender_output_ts)) {
        //         typedef std::tuple<std::decay_t<sender_output_ts>...> sender_decay_tuple_t;
        //         typedef std::tuple<std::decay_t<receiver_input_ts>...> receiver_decay_tuple_t;
        //         if constexpr (std::same_as<sender_decay_tuple_t, receiver_decay_tuple_t>)
        //             return true;
        //     }
        //     return false;
        // }
        //
        // static consteval bool assert_compatibility(void) requires (std::same_as<sender_output_t, void>) {
        //     if constexpr (sizeof...(receiver_input_ts) == 0)
        //         return true;
        //     return false;
        // }
        //
        // static consteval bool assert_compatibility(sender_output_t) requires (not core::meta::is_tuple_v<sender_output_t> and not std::same_as<sender_output_t, void>) {
        //     if constexpr (sizeof...(receiver_input_ts) == 1) {
        //         typedef core::meta::at_pack<0, receiver_input_ts...> receiver_input_t;
        //         if constexpr (std::same_as<std::decay_t<sender_output_t>, std::decay_t<receiver_input_t>>)
        //             return true;
        //     }
        //     return false;
        // }
        //
        // static_assert(assert_compatibility(std::declval<sender_output_t>()), ACE_INCOMPATIBLE_COMPOSE_ERROR);

        typedef async<receiver_return_t, receiver_rule_t>(*async_receiver_t)(receiver_input_ts...);
        typedef pipe<sender_output_t> sender_pipe_t;

        explicit receiver_base(async_receiver_t&& r) : _receiver(r){};
        async_receiver_t _receiver;

        std::conditional_t<std::same_as<receiver_return_t, void>, std::monostate, receiver_return_t> _pipe {};

        promise<> start(sender_pipe_t&& sender_pipe) {
            if constexpr (std::same_as<sender_output_t, void>) {
                co_await _receiver();
            } else if constexpr (core::meta::is_tuple_v<sender_output_t>) {
                _pipe = co_await std::make_from_tuple<async_receiver_t>(std::forward<sender_output_t>(sender_pipe._output));
            } else {
                _pipe = co_await _receiver(sender_pipe._output);
            }
            co_return;
        }
    };

    template <
        typename                    sender_output_t    ,
        typename                    receiver_return_t  ,
        core::is_promise_rule       receiver_rule_t    ,
        typename                ... receiver_input_ts
    >
    auto receiver(async<receiver_return_t, receiver_rule_t>(&&nexus_obj)(receiver_input_ts...)) {
        return std::move(receiver_base<sender_output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...>(std::move(nexus_obj)));
    }

}
#endif //ACE_VISUAL_PIPE_H
