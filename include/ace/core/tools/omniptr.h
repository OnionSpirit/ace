/**
 * @file omniptr.h
 * @brief Type-agnostic pointer storing one of several allowed pointer types.
 *
 * @details Used across ACE as @c omni_node (pool node types) and
 * @c omni_runner (runner pool types) to carry pointers of different concrete
 * types through shared machinery without templates.  Type safety is enforced
 * at the point of conversion via the @c is_contained constraint.
 */
#ifndef ACE_OMNIPTR_H
#define ACE_OMNIPTR_H

namespace ace::core::tools {

    /**
     * @brief Concept: @c type is one of @c args.
     * @tparam type Type to check.
     * @tparam args Allowed types.
     */
    template <typename type, typename... args>
    concept is_contained = (std::same_as<type, args> or ...);

    /**
     * @brief Type-agnostic pointer holding one of @c option_ts.
     *
     * @details Implicitly convertible from/to any of the allowed types,
     * from/to @c void*, and exposes @c as<T>() for explicit casts.
     *
     * @tparam option_t   First allowed type (used by @c operator->).
     * @tparam option_ts  Remaining allowed types.
     */
    template <typename option_t, typename... option_ts>
    struct omniptr {

        /// @brief Default constructor — null pointer.
        omniptr() = default;

        /// @brief Constructs from a raw pointer.
        /// @param p Raw pointer to store.
        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(void* p) { _ptr = p; }

        /// @brief Copy constructor — shares the stored pointer.
        /// @param p Source omniptr to copy.
        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(const omniptr& p) { _ptr = p._ptr; }

        /// @brief Move constructor — nulls the source.
        /// @param p Source omniptr to move from.
        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(omniptr&& p) noexcept { _ptr = p._ptr; p._ptr = nullptr; }

        /// @brief Copy assignment.
        omniptr& operator=(const omniptr& p) = default;

        /// @brief Move assignment — nulls the source.
        /// @param p Source omniptr to move from.
        /// @return Reference to this omniptr.
        omniptr& operator=(omniptr&& p)  noexcept {
            _ptr = p._ptr;
            p._ptr = nullptr;
            return *this;
        }

        /**
         * @brief Constructs from a typed pointer (must be one of the allowed types).
         * @tparam expected_t Pointer type to store.
         * @param p Typed pointer to store.
         */
        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(expected_t* p) requires is_contained<expected_t, option_t, option_ts...> {
            _ptr = p;
        }

        /**
         * @brief Boolean check — non-null.
         * @return @c true when a pointer is stored.
         */
        explicit operator bool () const { return _ptr != nullptr; }

        /**
         * @brief Implicit conversion to a typed pointer (must be one of the allowed types).
         * @tparam expected_t Pointer type to convert to.
         */
        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConversionOperator
        operator expected_t* () requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr);
        }

        /**
         * @brief Implicit conversion to a const typed pointer.
         * @tparam expected_t Pointer type to convert to.
         */
        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConversionOperator
        operator const expected_t* () const requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<const expected_t*>(_ptr);
        }

        /// @brief Implicit conversion to @c void*.
        // ReSharper disable once CppNonExplicitConversionOperator
        operator void* () const { return _ptr; }

        /// @brief Implicit conversion to @c const void*.
        // ReSharper disable once CppNonExplicitConversionOperator
        operator const void* () const { return _ptr; }

        /**
         * @brief Explicit cast to a typed pointer.
         * @tparam expected_t Pointer type to cast to.
         * @return The stored pointer cast to @c expected_t*.
         */
        template <typename expected_t>
        expected_t* as () requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr);
        }

        /**
         * @brief Compares the stored pointer with a typed pointer.
         * @tparam expected_t Pointer type to compare with.
         * @param rhs Pointer to compare against.
         * @return @c true when both pointers are equal.
         */
        template <typename expected_t>
        bool operator== (const expected_t* rhs) const requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr) == rhs;
        }

        /// @brief Default equality comparison between two omniptrs.
        bool operator== (const omniptr& rhs) const = default;

        /**
         * @brief Member access through the first allowed type.
         * @return The stored pointer cast to @c option_t*.
         */
        auto* operator->() const {
            return static_cast<option_t*>(_ptr);
        }

        /**
         * @brief Address-of the stored pointer slot.
         * @return Pointer to the internal pointer, typed as @c option_t**.
         */
        auto operator&() { // NOLINT(*-runtime-operator)
            return reinterpret_cast<option_t**>(&_ptr);
        }

        /// @brief Clears the stored pointer.
        void reset() {
            _ptr = nullptr;
        }

    private:

        void* _ptr {nullptr}; ///< Stored raw pointer.

    };

}

#endif //ACE_OMNIPTR_H
