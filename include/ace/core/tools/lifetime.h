/**
 * @file lifetime.h
 * @brief Debug utility for tracking object construction and destruction.
 *
 * @details Provides @c ace::core::tools::lifetime — a lightweight RAII
 * marker that logs its construction and destruction to the console when
 * tracking is enabled (via @c lifetime::track()).  Useful for debugging
 * coroutine frame lifetimes and RAII guard ordering.
 */
#ifndef ACE_LIFETIME_H
#define ACE_LIFETIME_H

#include <ace/console.h>

namespace ace::core::tools {

    /**
     * @brief RAII debug tracer — logs construction/destruction via @c console.
     *
     * @details Each instance carries a string @c _mark.  When lifetime tracking
     * is globally enabled (via @c track()), the constructor prints
     * "@c <mark> constructed" and the destructor prints "@c <mark> destroyed".
     */
    class lifetime {

        std::string _mark;

        static bool _active;

    public:

        static void track() { _active = true; }

        static void untrack() { _active = true; }

        explicit lifetime(const std::string_view mark) : _mark(mark) {
            if (_active)
                console::println("{} constructed", _mark);
        };

        ~lifetime() {
            if (_active)
                console::println("{} destroyed", _mark);
        }

        [[nodiscard]] std::string_view mark() const { return _mark; }
    };

    inline bool lifetime::_active = false;

}

#endif //ACE_LIFETIME_H
