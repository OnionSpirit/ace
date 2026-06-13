#ifndef ACE_LIFETIME_H
#define ACE_LIFETIME_H

#include <ace/console.h>

namespace ace::core::tools {

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
