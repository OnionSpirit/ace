#ifndef ACE_VISUAL_H
#define ACE_VISUAL_H

#include "visual/details/pipe.h"
#include "visual/units/chain.h"
#include "visual/units/graph.h"

#include "visual/connectors/some.h"

namespace ace {

    template <typename type = void>
    using pipe = visual::details::pipe<type>;

    using cancel = visual::details::cancel;

    using resume = visual::details::resume;

    using finish = visual::details::finish;

    template <typename return_type = void>
    using nexus = async<pipe<return_type>, core::permanent>;

    using some = visual::connectors::some;

}

#endif //ACE_VISUAL_H
