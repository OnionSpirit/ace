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
     * @tparam capture Initial capture variables
     * @tparam receiver_ts Contained pipes
     */
    template <branch_status status_v, typename capture, typename ... receiver_ts>
    struct branch {

        pipeline_state _completion_status { pipeline_state::e_idle };
        pipe<capture> _initial_sender;
        std::tuple<receiver_ts...> _pipeline {};

        template <typename receiver_t>
        auto operator | (receiver_t&& r) {
            if constexpr (std::is_void_v<typename receiver_t::raw_output>) {
                return branch<branch_status::e_complete, capture, receiver_ts..., receiver_t> {_pipeline, std::move(r)};
            } else {
                return branch<branch_status::e_incomplete, capture, receiver_ts..., receiver_t> {_pipeline, std::move(r)};
            }
        }
    };

}

#endif //ACE_VISUAL_BRANCH_H
