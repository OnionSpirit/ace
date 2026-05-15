#ifndef ACE_VISUAL_UNIT_H
#define ACE_VISUAL_UNIT_H

#include "ace/core/async.h"

namespace ace::visual {

    enum completion_state {
        e_complete,    ///< Returns void after completion
        e_incomplete,  ///< Does not return void after completion
    };

    struct unit_handler {
        virtual ~unit_handler() = default;
        virtual task start() = 0;
    };

    template <completion_state completion_v>
    struct unit_traits : unit_handler {

        static constexpr completion_state status = completion_v;
        ~unit_traits() override = default;
    };

    template <typename unit_t>
    concept is_unit = requires(unit_t unit) {
        { unit.start() } -> std::same_as<task>;
        unit_t::status;
    };

}

#endif //ACE_VISUAL_UNIT_H
