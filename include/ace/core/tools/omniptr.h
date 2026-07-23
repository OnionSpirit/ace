#ifndef ACE_OMNIPTR_H
#define ACE_OMNIPTR_H

namespace ace::core::tools {

    template <typename type, typename... args>
    concept is_contained = (std::same_as<type, args> or ...);

    template <typename option_t, typename... option_ts>
    struct omniptr {

        omniptr() = default;

        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(void* p) { _ptr = p; }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(const omniptr& p) { _ptr = p._ptr; }

        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(omniptr&& p)  noexcept { _ptr = p._ptr; p._ptr = nullptr; }

        omniptr& operator=(const omniptr& p) { _ptr = p._ptr; return *this; }

        omniptr& operator=(omniptr&& p)  noexcept {
            _ptr = p._ptr;
            p._ptr = nullptr;
            return *this;
        }

        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConvertingConstructor
        omniptr(expected_t* p) requires is_contained<expected_t, option_t, option_ts...> {
            _ptr = p;
        }

        explicit operator bool () const { return _ptr != nullptr; }

        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConversionOperator
        operator expected_t* () requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr);
        }

        template <typename expected_t>
        // ReSharper disable once CppNonExplicitConversionOperator
        operator const expected_t* () const requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<const expected_t*>(_ptr);
        }

        // ReSharper disable once CppNonExplicitConversionOperator
        operator void* () const { return _ptr; }

        // ReSharper disable once CppNonExplicitConversionOperator
        operator const void* () const { return _ptr; }

        template <typename expected_t>
        expected_t* as () requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr);
        }

        template <typename expected_t>
        bool operator== (const expected_t* rhs) const requires is_contained<expected_t, option_t, option_ts...> {
            return static_cast<expected_t*>(_ptr) == rhs;
        }

        bool operator== (const omniptr& rhs) const = default;

        auto* operator->() const {
            return static_cast<option_t*>(_ptr);
        }

        auto operator&() const { // NOLINT(*-runtime-operator)
            return reinterpret_cast<option_t**>(&_ptr);
        }

        void reset() {
            _ptr = nullptr;
        }

    private:

        void* _ptr {nullptr};

    };

}

#endif //ACE_OMNIPTR_H
