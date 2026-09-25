/**
 * @file testing_toolkit.h
 * @brief Shared CRTP selection of debug instrumentation bases.
 */
#ifndef ACE_CORE_TOOLS_TESTING_TOOLKIT_H
#define ACE_CORE_TOOLS_TESTING_TOOLKIT_H

#include <type_traits>

#include "ace/core/tools/macro.h"

namespace ace::core::tools {

    /**
     * @brief Exposes a toolkit as a debug base, or a distinct empty release base.
     * @tparam derived_t Toolkit inheriting this CRTP base; may still be incomplete.
     * @details Accesses to optional members must depend on a template parameter
     * inside an if constexpr (is_debug) branch, for example in a generic lambda.
     * A discarded branch alone does not suppress non-dependent name lookup.
     * @warning All translation units in an executable must agree on NDEBUG.
     */
    template <typename derived_t>
    class testing_mixin {
        /**
         * @brief Selects a type without constructing the incomplete CRTP toolkit.
         * @return Type identity of derived_t in debug, or of an empty type in release.
         */
        static consteval auto define_tools() noexcept {
            if constexpr (is_debug) {
                return std::type_identity<derived_t> {};
            } else {
                struct empty {};
                return std::type_identity<empty> {};
            }
        }

    public:
        /// @brief Base inherited by the owner; does not require a constructible toolkit.
        using debug_tools = typename decltype(define_tools())::type;
    };

}

#endif // ACE_CORE_TOOLS_TESTING_TOOLKIT_H
