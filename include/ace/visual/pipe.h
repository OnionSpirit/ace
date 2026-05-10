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
    template <typename output_t>
    struct ACE_AWAIT_NODISCARD pipe {

        pipeline_state _state = e_idle;

        output_t _output;

        pipe interrupt() { _state = e_broken; return this; }

        pipe resume() { _state = e_complete; return this; }
    };

    // NOTE: Object to interact with the pipeline from Coroutine body
    template <>
    struct ACE_AWAIT_NODISCARD pipe<void> {

        pipeline_state _state = e_idle;

        static pipe interrupt() { pipe p{}; p._state = e_broken; return p;}

        static pipe resume() { pipe p{}; p._state = e_complete; return p;}
    };

    // NOTE: Connector between sender and receiver. Intended for the branch members
    template <
        typename                    sender_output_t       ,
        typename                    receiver_raw_return_t ,
        core::is_promise_rule       receiver_rule_t       ,
        typename                ... receiver_input_ts
    >
    struct ACE_AWAIT_NODISCARD receiver final
        : core::traits::future_traits<receiver<sender_output_t, receiver_raw_return_t, receiver_rule_t, receiver_input_ts...>> {

        IMPORT_FUTURE_ENV(receiver);

        typedef receiver_raw_return_t raw_output_t;

        template <typename ... sender_output_ts>
        static consteval bool assert_compatibility(std::tuple<sender_output_ts...>) {
            if constexpr (sizeof...(receiver_input_ts) == sizeof...(sender_output_ts)) {
                typedef std::tuple<std::decay_t<sender_output_ts>...> sender_decay_tuple_t;
                typedef std::tuple<std::decay_t<receiver_input_ts>...> receiver_decay_tuple_t;
                if constexpr (std::same_as<sender_decay_tuple_t, receiver_decay_tuple_t>)
                    return true;
            }
            return false;
        }

        static consteval bool assert_compatibility(sender_output_t) requires (not core::meta::is_tuple_v<sender_output_t>) {
            if constexpr (sizeof...(receiver_input_ts) == 1) {
                typedef core::meta::at_pack<0, receiver_input_ts...> receiver_input_t;
                if constexpr (std::same_as<std::decay_t<sender_output_t>, std::decay_t<receiver_input_t>>)
                    return true;
            }
            return false;
        }

        static consteval bool assert_compatibility(void) requires (not core::meta::is_tuple_v<sender_output_t>) {
            if constexpr (sizeof...(receiver_input_ts) == 0)
                return true;
            return false;
        }

        static_assert(assert_compatibility(std::declval<sender_output_t>()), ACE_INCOMPATIBLE_COMPOSE_ERROR);

        typedef async<pipe<receiver_raw_return_t>, receiver_rule_t>(receiver_t)(receiver_input_ts...);
        typedef pipe<sender_output_t> sender_pipe_t;

        struct or_await_conductor;
        friend or_await_conductor;

        receiver(sender_pipe_t& sender_pipe, receiver_t)
            : _sender_pipe(sender_pipe) {};

        sender_pipe_t _sender_pipe;

        promise<pipe<receiver_raw_return_t>> start() {
            if constexpr (std::same_as<sender_output_t, void>) {
                co_return co_await receiver_t();
            } else if constexpr (core::meta::is_tuple_v<sender_output_t>) {
                co_return co_await std::make_from_tuple<receiver_t>(std::forward<sender_output_t>(_sender_pipe._output));
            } else {
                co_return co_await receiver_t(_sender_pipe._output);
            }
        }
    };

}
#endif //ACE_VISUAL_PIPE_H
