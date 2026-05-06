/**
 * @file
 * @details This file contains a future_handler, future_trait classes and its
 * dispatching concepts: is_future_accept_promise, is_future_accept_coroutine,
 * is_future. Types are intended to be used to create derived future objects,
 * that will be processed by co_await operator, and make promises waits to its
 * result
 */

#ifndef ACE_FUTURE_H
#define ACE_FUTURE_H

#include <concepts>
#include <memory>

#include "ace/core/tools/meta.h"
#include "ace/core/tools/prefetch.h"

namespace ace::core::traits {

    /**
     * @details Handler for future objects
     */
    struct future_handle {

        /**
         * @details Future value check function,
         * @b true if value is ready
         * @b false otherwise
         * @remark Specified by C++20 standard
         */
        virtual bool await_ready() = 0;

        /**
         * @details Default destructor
         */
        virtual ~future_handle() = default;
    };

    /**
     * @details Trait class for future objects
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct future_traits : future_handle {

        using derived_future_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        auto&& operator co_await() {
            return std::move(*static_cast<derived_future_t*>(this));
        }

        bool await_ready() override { return false; };

        /**
         * @details Default destructor
         */
        ~future_traits() override = default;

        /**
         * @details Prefetches memory of the derived future object
         */
        void prefetch() {
            const char* mem_ptr = reinterpret_cast<char*>(this);
            constexpr std::size_t mem_size = sizeof(derivedT);
            for (int i = 0; i <= mem_size / ACE_CACHE_LINE_SIZE; ++i) {
                const void* cacheline_ptr = mem_ptr + (i * ACE_CACHE_LINE_SIZE);
                core::tools::prefetch<tools::e_temporal>(cacheline_ptr);
            }
        }
    };

    #define IMPORT_FUTURE_ENV(future_t)                                \
        typedef ace::core::traits::future_traits<future_t> future_traits_t; \
        using typename future_traits_t::derived_future_t;


    /**
     * @details Traits class busy future objects. (Active polling of awaited tasks)
     * @tparam derivedT Derived type
     */
    template <typename derivedT>
    struct busy_future_traits : future_handle {

        using derived_busy_future_t = derivedT;

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        derived_busy_future_t& operator co_await() requires (std::copy_constructible<derived_busy_future_t>) {
            return *static_cast<derived_busy_future_t*>(this);
        }

        /**
         * @details Allows
         * use created object with @b co_await
         * @b operator inside context code,
         * and makes derived class @b awaitable
         */
        derived_busy_future_t&& operator co_await() requires (not std::copy_constructible<derived_busy_future_t>) {
            // static_assert(std::move_constructible<derived_future_t>, "At least move constructability required");
            return std::forward<derived_busy_future_t>(*static_cast<derived_busy_future_t*>(this));
        }

        /**
         * @details Default destructor
         */
        ~busy_future_traits() override = default;
    };

    #define IMPORT_BUSY_FUTURE_ENV(future_t)                                     \
        typedef ace::core::traits::busy_future_traits<future_t> busy_future_traits_t; \
        using typename busy_future_traits_t::derived_busy_future_t;

}

#endif // ACE_FUTURE_H
