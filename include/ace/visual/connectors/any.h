#ifndef ACE_VISUAL_CONNECTORS_ANY_H
#define ACE_VISUAL_CONNECTORS_ANY_H

namespace ace::visual::connectors {

    // NOTE: Marks graph to await any branch without canceling others on the single branch completion.
    // NOTE: Results of the other branches will be dropped
    struct any {};

}

#endif //ACE_VISUAL_CONNECTORS_ANY_H
