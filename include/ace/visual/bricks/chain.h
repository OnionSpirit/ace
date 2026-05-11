#ifndef ACE_VISUAL_CHAIN_H
#define ACE_VISUAL_CHAIN_H

#include "ace/visual/details/pipe.h"
#include "ace/visual/details/actor.h"

namespace ace::visual {

    enum class chain_status {
        e_complete,         ///< Chain returns void after completion
        e_incomplete        ///< Chain does not returns void after completion
    };

    // NOTE: Defines graph chain
    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Chain status
     * @tparam input_t Initial capture variables
     * @tparam receiver_ts Contained pipes
     */
    template <chain_status status_v, typename input_t, typename ... receiver_ts>
    struct chain_base {

        details::pipeline_state _completion_status {details::pipeline_state::e_idle };
        details::pipe<input_t> _initial_sender;
        std::tuple<receiver_ts...> _pipeline {};

        chain_base() = default;

        explicit chain_base(input_t&& input) {
            auto p = details::pipe<input_t>(std::forward<input_t>(input));
            _initial_sender = std::forward<details::pipe<input_t>>(p);
        }

        chain_base(std::tuple<receiver_ts...>&& receivers, details::pipe<input_t>&& initial_sender) {
            _pipeline = std::move(receivers);
            _initial_sender = std::move(initial_sender);
        }

        static constexpr auto define_output_type() {
            if constexpr (sizeof...(receiver_ts) > 0) {
                using tail_receiver_t = core::meta::at_pack<sizeof...(receiver_ts) - 1, receiver_ts...>;
                using output_t = tail_receiver_t::raw_output_t;
                if constexpr (not std::is_void_v<output_t>) {
                    return std::decay_t<output_t>{};
                } else return;
            } else if constexpr (not std::same_as<input_t, void>)
                return std::decay_t<input_t>{};
            else return;
        }

        auto operator | (auto &&r) {
            // TODO: Need to add extra assertion for actor type
            typedef decltype(define_output_type()) output_t;
            auto recv = std::move(actor<output_t>(std::forward<decltype(r)>(r)));
            typedef decltype(recv) extra_actor_t;
            if constexpr (std::is_void_v<typename extra_actor_t::raw_output_t>) {
                return chain_base<chain_status::e_complete, input_t, receiver_ts..., extra_actor_t> {
                    std::tuple_cat(
                        std::forward<std::tuple<receiver_ts...>>(_pipeline),
                        std::forward<std::tuple<extra_actor_t&>>(std::tie(recv)))
                    , std::forward<details::pipe<input_t>>(_initial_sender)
                };
            } else {
                return chain_base<chain_status::e_incomplete, input_t, receiver_ts..., extra_actor_t> {
                    std::tuple_cat(
                        std::forward<std::tuple<receiver_ts...>>(_pipeline),
                        std::forward<std::tuple<extra_actor_t&>>(std::tie(recv)))
                    , std::forward<details::pipe<input_t>>(_initial_sender)
                };
            }
        }

        static promise<bool> describe_status(details::pipeline_state status) {
            switch (status) {
                case details::pipeline_state::e_broken : {
                    co_return false;
                }
                case details::pipeline_state::e_complete: {
                    co_return false;
                }
                case details::pipeline_state::e_idle:
                    break;
                case details::pipeline_state::e_in_progress:
                    break;
            }
            co_return true;
        }

        task start() {
            co_await [&] <std::size_t ... index> (std::index_sequence<index...>) -> task {
                 (... and co_await [&] -> promise<bool> {
                     // NOTE: At the beginning of chain
                     if constexpr (index == 0) {
                         typedef decltype(std::get<index>(_pipeline).start(std::move(_initial_sender))) actor_t;
                         _completion_status = _initial_sender._state = details::pipeline_state::e_in_progress;

                         // NOTE: Coroutine / Routine selection
                         if constexpr (core::meta::is_awaitable<actor_t, task::promise_type>)
                            co_await std::get<index>(_pipeline).start(std::move(_initial_sender));
                         else
                             std::get<index>(_pipeline).start(std::move(_initial_sender));

                     // NOTE: For other members
                     } else {
                         typedef decltype(std::get<index>(_pipeline).start(std::move(std::get<index - 1>(_pipeline)._pipe))) actor_t;

                         // NOTE: Coroutine / Routine selection
                         if constexpr (core::meta::is_awaitable<actor_t, task::promise_type>)
                            co_await std::get<index>(_pipeline).start(std::move(std::get<index - 1>(_pipeline)._pipe));
                         else
                             std::get<index>(_pipeline).start(std::move(std::get<index - 1>(_pipeline)._pipe));
                     }

                     _completion_status = std::get<index>(_pipeline)._pipe._state;
                     // TODO: Debug log
                     co_await console::async::println("Chain current state: {}", _completion_status);
                     co_return co_await describe_status(_completion_status);
                }());
            }(std::make_index_sequence<sizeof...(receiver_ts)>{});
            co_return;
        }
    };

}

namespace visual {

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Chain status
     */
    template <ace::visual::chain_status status_v = ace::visual::chain_status::e_incomplete>
    auto chain() {
        return ace::visual::chain_base<status_v, void>();
    }

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Chain status
     * @tparam input_t Initial capture variables
     */
    template <typename input_t, ace::visual::chain_status status_v = ace::visual::chain_status::e_incomplete>
    auto chain(input_t&& in) {
        return ace::visual::chain_base<status_v, input_t>(std::forward<input_t>(in));
    }

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Chain status
     * @tparam input_ts Initial capture variables
     */
    template <ace::visual::chain_status status_v = ace::visual::chain_status::e_incomplete, typename ... input_ts>
    auto chain(input_ts&& ... in) {
        return ace::visual::chain_base<status_v, std::tuple<input_ts...>>(std::forward<std::tuple<input_ts...>>(std::tie(in...)));
    }

}

#endif //ACE_VISUAL_CHAIN_H
