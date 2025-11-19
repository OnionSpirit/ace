#ifndef ACE_HUB_H
#define ACE_HUB_H

namespace ace::hubs {

    template <typename promiseT>
    struct hub_traits {

        virtual bool release(void* =nullptr) = 0;

        virtual bool emplace(promiseT&&) = 0;

        virtual ~hub_traits() = default;
    };
}

#endif //ACE_HUB_H
