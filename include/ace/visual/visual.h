#ifndef ACE_VISUAL_H
#define ACE_VISUAL_H

#include "details/pipe.h"
#include "bricks/chain.h"
#include "bricks/graph.h"

namespace visual {

    template <typename type = void>
    using pipe = ace::visual::details::pipe<type>;

    using cancel = ace::visual::details::cancel;

    using resume = ace::visual::details::resume;

    using finish = ace::visual::details::finish;

    template <typename return_type = void>
    using nexus = ace::async<pipe<return_type>, ace::core::permanent>;

}

#endif //ACE_VISUAL_H
