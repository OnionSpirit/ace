#ifndef ACE_VISUAL_BRICK_H
#define ACE_VISUAL_BRICK_H

#include "ace/core/async.h"

namespace ace::visual {

    enum completion_state {
        e_complete,    ///< Returns void after completion
        e_incomplete,  ///< Does not return void after completion
    };

    struct brick_handler {
        virtual ~brick_handler() = default;
        virtual task start() = 0;
    };

    template <completion_state completion_v>
    struct brick_traits : brick_handler {

        static constexpr completion_state status = completion_v;
        ~brick_traits() override = default;
    };

    template <typename brick_t>
    concept is_brick = requires(brick_t const& brick) {
        { brick.start() } -> std::same_as<task>;
        brick_t::status;
    };

}

#endif //ACE_VISUAL_BRICK_H
