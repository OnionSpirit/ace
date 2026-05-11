#ifndef ACE_VISUAL_DETAILS_ACTOR_H
#define ACE_VISUAL_DETAILS_ACTOR_H

#include <optional>

#include "ace/core/tools/macro.h"
#include "ace/core/traits/future.h"
#include "ace/core/traits/promise.h"
#include "ace/core/async_handle.h"
#include "ace/visual/details/pipe.h"

namespace ace::visual::details {

    template <
        typename                    sender_output_t  ,
        typename                    nexus_return_t   ,
        core::is_promise_rule       nexus_rule_t     ,
        typename                ... nexus_input_ts
    >
    struct ACE_AWAIT_NODISCARD nexus_actor final
        : core::traits::future_traits<nexus_actor<sender_output_t, nexus_return_t, nexus_rule_t, nexus_input_ts...>> {

        IMPORT_FUTURE_ENV(nexus_actor);

        template <typename raw_output>
        static consteval raw_output get_raw_output(pipe<raw_output>) {
            if constexpr (not std::same_as<raw_output, void>) return std::declval<raw_output>();
            else return;
        }

        nexus_actor() = default;

        typedef decltype(get_raw_output(std::declval<nexus_return_t>())) raw_output_t;

        typedef async<nexus_return_t, nexus_rule_t>(*async_nexus_t)(nexus_input_ts...);
        typedef pipe<sender_output_t> input_pipe_t;
        typedef nexus_return_t output_pipe_t;


        template <typename ... sender_output_ts>
        requires (sizeof...(sender_output_ts) > 0)
        static consteval bool assert_compatibility(pipe<std::tuple<sender_output_ts...>>) {
            if constexpr (sizeof...(nexus_input_ts) == 1) {
                typedef core::meta::at_pack<0, nexus_input_ts...> nexus_input_t;
                static_assert(std::constructible_from<nexus_input_t, sender_output_ts...>,
                    "<Right Operand> input type can not be constructed from <Left Operand> output param set");
                return true;
            } else {
                static_assert(sizeof...(nexus_input_ts) == sizeof...(sender_output_ts),
                    "<Right Operand> param set and <Left Operand> param set have different length");
                if constexpr (sizeof...(nexus_input_ts) == sizeof...(sender_output_ts)) {
                    typedef std::tuple<std::decay_t<sender_output_ts>...> sender_decay_tuple_t;
                    typedef std::tuple<std::decay_t<nexus_input_ts>...> nexus_decay_tuple_t;
                    static_assert(std::same_as<sender_decay_tuple_t, nexus_decay_tuple_t>,
                        "<Right Operand> param set has the same size as <Left Operand> param set, "
                        "but contained types are not compatible");
                    if constexpr (std::same_as<sender_decay_tuple_t, nexus_decay_tuple_t>)
                        return true;
                }
            }
            return false;
        }

        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and not std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(nexus_input_ts) == 1,
                "<Right Operand> requests few params, but <Left Operand> passes one");
            if constexpr (sizeof...(nexus_input_ts) == 1) {
                typedef core::meta::at_pack<0, nexus_input_ts...> nexus_input_t;
                static_assert(std::same_as<std::decay_t<sender_output_t>, std::decay_t<nexus_input_t>>
                           or std::constructible_from<nexus_input_t, sender_output_t>,
                    "<Right Operand> input param is not compatible <Left Operand> output type");
                if constexpr (std::same_as<std::decay_t<sender_output_t>, std::decay_t<nexus_input_t>>)
                    return true;
            }
            return false;
        }


        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(nexus_input_ts) == 0,
                "<Right Operand> requires input params but <Left Operand> returns nothing");
            if constexpr (sizeof...(nexus_input_ts) == 0)
                return true;
            return false;
        }

        static_assert(assert_compatibility(input_pipe_t()), ACE_INCOMPATIBLE_COMPOSE_ERROR);

        explicit nexus_actor(async_nexus_t&& r) : _nexus(r){};

        async_nexus_t _nexus;
        nexus_return_t _pipe {};

        promise<> start(input_pipe_t&& sender_pipe) {
            if constexpr (std::same_as<sender_output_t, void>) {
                _pipe = co_await _nexus();
            } else if constexpr (core::meta::is_tuple_v<sender_output_t>) {
                _pipe = co_await std::apply(_nexus, std::move(sender_pipe._output));
            } else {
                _pipe = co_await _nexus(std::move(sender_pipe._output));
            }
            co_return;
        }
    };


    template <
        typename     sender_output_t     ,
        typename     callback_return_t   ,
        typename ... callback_input_ts
    >
    struct ACE_AWAIT_NODISCARD callback_actor final
        : core::traits::future_traits<callback_actor<sender_output_t, callback_return_t, callback_input_ts...>> {

        IMPORT_FUTURE_ENV(callback_actor);

        template <typename raw_output>
        static consteval raw_output get_raw_output(pipe<raw_output>) {
            if constexpr (not std::same_as<raw_output, void>) return std::declval<raw_output>();
            else return;
        }

        callback_actor() = default;

        typedef decltype(get_raw_output(std::declval<callback_return_t>())) raw_output_t;
        typedef pipe<sender_output_t> input_pipe_t;
        typedef callback_return_t output_pipe_t;

        typedef callback_return_t(*callback_t)(callback_input_ts...);


        template <typename ... sender_output_ts>
        requires (sizeof...(sender_output_ts) > 0)
        static consteval bool assert_compatibility(pipe<std::tuple<sender_output_ts...>>) {
            if constexpr (sizeof...(callback_input_ts) == 1) {
                typedef core::meta::at_pack<0, callback_input_ts...> callback_input_t;
                static_assert(std::constructible_from<callback_input_t, sender_output_ts...>,
                    "<Right Operand> input type can not be constructed from <Left Operand> output param set");
                return true;
            } else {
                static_assert(sizeof...(callback_input_ts) == sizeof...(sender_output_ts),
                    "<Right Operand> param set and <Left Operand> param set have different length");
                if constexpr (sizeof...(callback_input_ts) == sizeof...(sender_output_ts)) {
                    typedef std::tuple<std::decay_t<sender_output_ts>...> sender_decay_tuple_t;
                    typedef std::tuple<std::decay_t<callback_input_ts>...> callback_input_t;
                    static_assert(std::same_as<sender_decay_tuple_t, callback_input_t>,
                        "<Right Operand> param set has the same size as <Left Operand> param set, "
                        "but contained types are not compatible");
                    if constexpr (std::same_as<sender_decay_tuple_t, callback_input_t>)
                        return true;
                }
            }
            return false;
        }

        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and not std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(callback_input_ts) == 1,
                "<Right Operand> requests few params, but <Left Operand> passes one");
            if constexpr (sizeof...(callback_input_ts) == 1) {
                typedef core::meta::at_pack<0, callback_input_ts...> callback_input_t;
                static_assert(std::same_as<std::decay_t<sender_output_t>, std::decay_t<callback_input_t>>
                           or std::constructible_from<callback_input_t, sender_output_t>,
                    "<Right Operand> input param is not compatible <Left Operand> output type");
                if constexpr (std::same_as<std::decay_t<sender_output_t>, std::decay_t<callback_input_t>>)
                    return true;
            }
            return false;
        }


        static consteval bool assert_compatibility(pipe<sender_output_t>)
        requires (not core::meta::is_tuple_v<sender_output_t> and std::is_void_v<sender_output_t>) {
            static_assert(sizeof...(callback_input_ts) == 0,
                "<Right Operand> requires input params but <Left Operand> returns nothing");
            if constexpr (sizeof...(callback_input_ts) == 0)
                return true;
            return false;
        }

        static_assert(assert_compatibility(input_pipe_t()), ACE_INCOMPATIBLE_COMPOSE_ERROR);

        explicit callback_actor(callback_t&& r) : _callback(std::forward<callback_t>(r)){};

        callback_t _callback;
        callback_return_t _pipe {};

        void start(input_pipe_t&& sender_pipe) {
            if constexpr (std::same_as<sender_output_t, void>) {
                _pipe = _callback();
            } else if constexpr (core::meta::is_tuple_v<sender_output_t>) {
                _pipe = std::apply(_callback, std::move(sender_pipe._output));
            } else {
                _pipe = _callback(std::move(sender_pipe._output));
            }
        }
    };


    template <
        typename                    sender_output_t    ,
        typename                    nexus_return_t  ,
        core::is_promise_rule       nexus_rule_t    ,
        typename                ... nexus_input_ts
    >
    auto actor(async<nexus_return_t, nexus_rule_t>(&&nexus_obj)(nexus_input_ts...)) {
        typedef async<nexus_return_t, nexus_rule_t>(&&nexus_t)(nexus_input_ts...);
        return nexus_actor<sender_output_t, nexus_return_t, nexus_rule_t, nexus_input_ts...>
            (std::forward<nexus_t>(nexus_obj));
    }


    template <
        typename     sender_output_t     ,
        typename     callback_return_t   ,
        typename ... callback_input_ts
    >
    auto actor(callback_return_t(&&callback_obj)(callback_input_ts...)) {
        typedef callback_return_t(&&callback_t)(callback_input_ts...);
        return callback_actor<sender_output_t, callback_return_t, callback_input_ts...>
            (std::forward<callback_t>(callback_obj));
    }

}

#endif //ACE_VISUAL_DETAILS_ACTOR_H
