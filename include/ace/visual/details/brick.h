#ifndef ACE_VISUAL_DETAILS_BRICK_H
#define ACE_VISUAL_DETAILS_BRICK_H

#include "ace/core/async.h"

namespace ace::visual::details {

    struct brick_handler {
        virtual ~brick_handler() = default;
        virtual task start() = 0;
    };

}

#endif //ACE_VISUAL_DETAILS_BRICK_H
