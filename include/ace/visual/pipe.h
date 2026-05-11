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

        pipe() = default;

        explicit pipe(const output_t& out) { _output = out; }

        explicit pipe(output_t&& out) { _output = std::forward<output_t>(out); }

        template <typename input_t>
        explicit pipe(input_t out) { _output = input_t{out}; }

        output_t _output;

        pipe interrupt() { _state = pipeline_state::e_broken; return this; }

        pipe resume() { _state = pipeline_state::e_complete; return this; }
    };

    // NOTE: Object to interact with the pipeline from Coroutine body
    template <>
    struct pipe<void> {

        pipeline_state _state = pipeline_state::e_idle;

        pipe() = default;

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
    struct ACE_AWAIT_NODISCARD nexus_receiver final
        : core::traits::future_traits<nexus_receiver<sender_output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...>> {

        IMPORT_FUTURE_ENV(nexus_receiver);

        template <typename raw_output>
        static consteval raw_output get_raw_output(pipe<raw_output>) {
            if constexpr (not std::same_as<raw_output, void>) return std::declval<raw_output>();
            else return;
        }

        nexus_receiver() = default;

        typedef decltype(get_raw_output(std::declval<receiver_return_t>())) raw_output_t;

        typedef async<receiver_return_t, receiver_rule_t>(*async_receiver_t)(receiver_input_ts...);
        typedef pipe<sender_output_t> input_pipe_t;
        typedef receiver_return_t output_pipe_t;


        template <typename ... sender_output_ts>
        requires (sizeof...(sender_output_ts) > 0)
        static consteval bool assert_compatibility(pipe<std::tuple<sender_output_ts...>>) {
            if constexpr (sizeof...(receiver_input_ts) == 1) {
                typedef core::meta::at_pack<0, receiver_input_ts...> receiver_input_t;
                static_assert(std::constructible_from<receiver_input_t, sender_output_ts...>,
                    "<Right Operand> input type can not be constructed from <Left Operand> output param set");
                return true;
            } else {
                static_assert(sizeof...(receiver_input_ts) == sizeof...(sender_output_ts),
                    "<Right Operand> param set and <Left Operand> param set have different length");
                if constexpr (sizeof...(receiver_input_ts) == sizeof...(sender_output_ts)) {
                    typedef std::tuple<std::decay_t<sender_output_ts>...> sender_decay_tuple_t;
                    typedef std::tuple<std::decay_t<receiver_input_ts>...> receiver_decay_tuple_t;
                    static_assert(std::same_as<sender_decay_tuple_t, receiver_decay_tuple_t>,
                        "<Right Operand> param set has the same size as <Left Operand> param set, "
                        "but contained types are not compatible");
                    if constexpr (std::same_as<sender_decay_tuple_t, receiver_decay_tuple_t>)
                        return true;
                }
            }
            return false;
        }

        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and not std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(receiver_input_ts) == 1,
                "<Right Operand> requests few params, but <Left Operand> passes one");
            if constexpr (sizeof...(receiver_input_ts) == 1) {
                typedef core::meta::at_pack<0, receiver_input_ts...> receiver_input_t;
                static_assert(std::same_as<std::decay_t<sender_output_t>, std::decay_t<receiver_input_t>>
                           or std::constructible_from<receiver_input_t, sender_output_t>,
                    "<Right Operand> input param is not compatible <Left Operand> output type");
                if constexpr (std::same_as<std::decay_t<sender_output_t>, std::decay_t<receiver_input_t>>)
                    return true;
            }
            return false;
        }


        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(receiver_input_ts) == 0,
                "<Right Operand> requires input params but <Left Operand> returns nothing");
            if constexpr (sizeof...(receiver_input_ts) == 0)
                return true;
            return false;
        }

        static_assert(assert_compatibility(input_pipe_t()), ACE_INCOMPATIBLE_COMPOSE_ERROR);

        explicit nexus_receiver(async_receiver_t&& r) : _receiver(r){};
        async_receiver_t _receiver;

        std::conditional_t<std::same_as<receiver_return_t, void>, std::monostate, receiver_return_t> _pipe {};

        promise<> start(input_pipe_t&& sender_pipe) {
            if constexpr (std::same_as<sender_output_t, void>) {
                co_await _receiver();
            } else if constexpr (core::meta::is_tuple_v<sender_output_t>) {
                _pipe = co_await std::apply(_receiver, std::move(sender_pipe._output));
            } else {
                _pipe = co_await _receiver(std::move(sender_pipe._output));
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
        return std::move(nexus_receiver<sender_output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...>(std::move(nexus_obj)));
    }

}
#endif //ACE_VISUAL_PIPE_H
