/**
 * @file
 * @details This file contains async::flag declaration
 */
#ifndef RIOT_AWAITABLE_FLAG_H
#define RIOT_AWAITABLE_FLAG_H

#include "riot.h"


namespace riot::async {

/**
 * @details falg with async call operator @b(co_await) support
 * @tparam Strategy of flag toggling. @b AutoSwitch if required to clear flag
 * after it's awaiting or @b ManualSwitch if required to toggle it manually
 */
template
<
    template <typename>
    typename Strategy = riot::component_modes::async::flag::auto_switch
>
requires riot::component_modes::async::flag::ModeRequirement<Strategy<void>>
class flag :
        public awaitable<flag<Strategy>, riot::component_modes::async::awaitable::management_strategy::ordering>,
        public Strategy<flag<Strategy>> {

public:

    auto resume() {}

    /**
     * @details Method that turns flag into @b enabled position
     */
    void notice() const noexcept;

private:

    friend Strategy<flag<Strategy>>;
    mutable std::atomic_flag notified ={true}; ///< Instance of flag
};

} // end namespace riot::async


//==============================DEFINITIONS==================================


RIOT_AWAITABLE_FLAG_META
void RIOT_FLAG_SPACE notice() const noexcept {
    notified.clear(std::memory_order_release);
    this->reset();
}

#undef RIOT_AWAITABLE_FLAG_META
#endif // RIOT_AWAITABLE_FLAG_H
