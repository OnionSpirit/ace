#ifndef ACE_VISUAL_BRANCH_H
#define ACE_VISUAL_BRANCH_H

#include "pipe.h"

namespace ace::visual {

    enum class branch_status {
        e_complete,         ///< Branch returns void after completion
        e_incomplete        ///< Branch does not returns void after completion
    };

    // NOTE: Defines graph branch
    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Branch status
     * @tparam input_t Initial capture variables
     * @tparam receiver_ts Contained pipes
     */
    template <branch_status status_v, typename input_t, typename ... receiver_ts>
    struct branch_base {

        pipeline_state _completion_status { pipeline_state::e_idle };
        pipe<input_t> _initial_sender;
        std::tuple<receiver_ts...> _pipeline {};

        branch_base() = default;

        explicit branch_base(input_t&& input) {
            auto p = pipe<input_t>(std::forward<input_t>(input));
            _initial_sender = std::forward<pipe<input_t>>(p);
        }

        branch_base(std::tuple<receiver_ts...>&& receivers, pipe<input_t>&& initial_sender) {
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
            typedef receiver_base<output_t, receiver_return_t, receiver_rule_t, receiver_input_ts...> extra_receiver_t;
            auto recv = std::move(receiver<output_t>(std::move(r)));
            if constexpr (std::is_void_v<typename extra_receiver_t::raw_output_t>) {
                return branch_base<branch_status::e_complete, input_t, receiver_ts..., extra_receiver_t> {
                    std::tuple_cat(std::move(_pipeline), std::move(std::tie(recv))), std::move(_initial_sender)
                };
            } else {
                return branch_base<branch_status::e_incomplete, input_t, receiver_ts..., extra_receiver_t> {
                    std::tuple_cat(std::move(_pipeline), std::move(std::tie(recv))), std::move(_initial_sender)
                };
            }
        }

        task start() {
            co_await [&] <std::size_t ... index> (std::index_sequence<index...>) -> task {
                 (..., co_await [&] -> task {
                     if constexpr (index == 0)
                         co_await std::get<index>(_pipeline).start(std::move(_initial_sender));
                     else
                         co_await std::get<index>(_pipeline).start(std::move(std::get<index - 1>(_pipeline)._pipe));
                }());
            }(std::make_index_sequence<sizeof...(receiver_ts)>{});
            co_return;
        }
    };

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Branch status
     */
    template <branch_status status_v = branch_status::e_incomplete>
    auto branch() {
        return branch_base<status_v, void>();
    }

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Branch status
     * @tparam input_t Initial capture variables
     */
    template <typename input_t, branch_status status_v = branch_status::e_incomplete>
    auto branch(input_t&& in) {
        return branch_base<status_v, input_t>(std::forward<input_t>(in));
    }

    /**
     * @brief Header object. Declares interfaces of the pipeline beginning
     * @tparam status_v Branch status
     * @tparam input_t Initial capture variables
     */
    template <branch_status status_v = branch_status::e_incomplete, typename ... input_ts>
    auto branch(input_ts&& ... in) {
        return branch_base<status_v, std::tuple<input_ts...>>(std::forward<std::tuple<input_ts...>>(std::tie(in...)));
    }

}

#endif //ACE_VISUAL_BRANCH_H
