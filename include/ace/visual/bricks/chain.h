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

        template <
            typename                    receiver_return_t ,
            core::is_promise_rule       receiver_rule_t   ,
            typename                ... receiver_input_ts
        >
        auto operator | (async<receiver_return_t, receiver_rule_t>(&&r)(receiver_input_ts...)) {
            typedef decltype(define_output_type()) output_t;
            typedef details::nexus_actor<output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...> extra_receiver_t;
            auto recv = std::move(actor<output_t>(std::move(r)));
            if constexpr (std::is_void_v<typename extra_receiver_t::raw_output_t>) {
                return chain_base<chain_status::e_complete, input_t, receiver_ts..., extra_receiver_t> {
                    std::tuple_cat(std::move(_pipeline), std::move(std::tie(recv))), std::move(_initial_sender)
                };
            } else {
                return chain_base<chain_status::e_incomplete, input_t, receiver_ts..., extra_receiver_t> {
                    std::tuple_cat(std::move(_pipeline), std::move(std::tie(recv))), std::move(_initial_sender)
                };
            }
        }

        task start() {
            co_await [&] <std::size_t ... index> (std::index_sequence<index...>) -> task {
                 (... and co_await [&] -> promise<bool> {
                     if constexpr (index == 0) {
                         _initial_sender._state = details::pipeline_state::e_in_progress;
                         co_await std::get<index>(_pipeline).start(std::move(_initial_sender));
                     } else
                         co_await std::get<index>(_pipeline).start(std::move(std::get<index - 1>(_pipeline)._pipe));
                     _completion_status = std::get<index>(_pipeline)._pipe._state;
                     // TODO: Debug log
                     co_await console::async::println("Chain current state: {}", _completion_status);
                     switch (_completion_status) {
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
