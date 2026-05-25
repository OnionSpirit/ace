#ifndef ACE_IO_H
#define ACE_IO_H


#include <climits>
#include <utility>

#include "ace/core/services/kernelic.h"

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace::core {

    template <typename query_t>
    concept is_query = requires(query_t q, services::kernel_observer* kwp) {
        { q.setup_query(kwp) } -> std::same_as<bool>;
    };

    template <typename entry_t>
    concept is_entity = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
    };

    template <typename entry_t>
    concept is_entry = requires(entry_t q) {
        { q.clear() } -> std::same_as<void>;
    };

    /**
     * @brief An interface to interact with the
     * @c ace::core::services::kernel_controller via @c co_await operator.
     * <br>Does not define 'resume(...)' logic.
     * @tparam query_core_t Specific query type.
     */
    template <typename query_core_t>
    struct io_query : traits::future_traits<query_core_t>, services::kernel_observer {

        IMPORT_FUTURE_ENV(query_core_t);

        explicit io_query(const int fd) : _fd(fd) {
            static_assert(is_query<query_core_t>,
                "Query object shall implement 'bool setup_query(ace::core::kernel_waiter*)' method");
        }

        struct io_socket_query_conductor : conductor_handler_t {

            io_socket_query_conductor() = delete;

            explicit io_socket_query_conductor(io_query* query_)
                : _query(query_) {};

            void forward(task&& ctx) override {
                _query->_waiter = std::move(ctx);
            }

            void cancel() override {
                // TODO: Improve cancel with pop from local submission queue
                services::kernel_controller::cancel(_query, 0);
            }

            ~io_socket_query_conductor() override = default;

            io_query* _query;
        };

        task       _waiter;               ///< Awaited task storage
        int        _res       = INT_MIN;  ///< IO_URING operation result
        const int  _fd;                   ///< FD to interact with
        bool       _is_silent = false;    ///< Mark to detach and not suspend

        bool await_ready() override { return false; };

        bool await_suspend(auto coroutine) {
            _runner_identity = coroutine.promise()._runner_pool;
            if (_fd < 0)
                throw std::logic_error("Trying to make query on failed 'io_entity' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (INT_MIN == _fd)
                throw std::logic_error("Trying to make query on idle 'io_entry' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (static_cast<query_core_t*>(this)->setup_query(this) and not _is_silent) {
                coroutine.promise()._runner_conductor = io_socket_query_conductor{this};
                return true;
            }
            return false;
        }

        void on_result(const int res) override {
            _res = res;
            if (_waiter)
                runner::reattach(std::move(_waiter));
        }

        ~io_query() override = default;
    };

#define IMPORT_IO_QUERY_ENV(class)                    \
    typedef ace::core::io_query<class> io_query_t;    \
    using io_query_t::_fd;                            \
    using io_query_t::_res;                           \
    ~class() override = default;


    struct read_query : io_query<read_query> {

        read_query() = delete;

        [[nodiscard]] explicit read_query(const int fd, void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : io_query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        bool setup_query(kernel_observer* kwp) const {
            return services::kernel_controller::read(kwp, _fd, _buf, _nbytes, _offset);
        }

        [[nodiscard]] int await_resume() const {
            // NOTE: Nul-termination for input
            if (_res > 0) static_cast<char*>(_buf)[_res] = '\0';
            return _res;
        }

        const int _fd;
        void *_buf;
        const unsigned _nbytes;
        const uint64_t _offset;
    };


    struct write_query : io_query<write_query> {

        write_query() = delete;

        explicit write_query(const int fd, const void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : io_query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        bool setup_query(kernel_observer* kwp) const {
            return services::kernel_controller::write(kwp, _fd, _buf, _nbytes, _offset);
        }

        [[nodiscard]] int await_resume() const { return _res; }

        const int _fd;
        const void *_buf;
        const unsigned _nbytes;
        const uint64_t _offset;
    };


    struct close_query : io_query<close_query> {

        IMPORT_IO_QUERY_ENV(close_query)

        close_query() = delete;

        explicit close_query(const int fd) : io_query_t(fd) {}

        bool setup_query(kernel_observer* kwp) const noexcept {
            return services::kernel_controller::close(kwp, _fd);
        }

        [[nodiscard]] int await_resume() const { return _res; }
    };

    /**
     * @brief RAII io fd guard
     */
    struct io_guard final {
        io_guard() = delete;
        explicit io_guard(const int& fd, const bool& closed)
            : _fd(fd)
            , _closed(closed) {}

        const int& _fd;
        const bool& _closed;

        static task pending_close(const int fd) noexcept {
            if (const int res = co_await close_query{fd}; res < 0)
                std::cerr << strerror(res) << std::endl;
        }

        ~io_guard() noexcept {
            if (_fd > 0 and not _closed)
                schedule(pending_close(_fd));
        }
    };

    template <typename>
    struct io_transformer { static_assert(false, "'io_transformer' is not provided for requested type"); };

    /**
     * @brief Handler for a file descriptor with RAII guard behavior.
     * The io_entity derived types shall represent FD state by providing allowed async operations depending on FD state.
     * @tparam entity_t Derived entity type
     * @tparam Params Data to propagate through transformation
     */
    template <typename entity_t>
    struct io_entity {

        io_entity()
            : _fd(-1)
            , _is_closed(true) {}

        io_entity(const int fd, const bool is_closed)
            : _fd(fd)
            , _is_closed(is_closed) { };

        template<typename entry_t>
        static entity_t transform(entry_t& io) noexcept {
            auto [fd, is_closed] = std::move(io.extract());
            if (fd < 0) is_closed = true;
            return io_transformer<entity_t>::transform(fd, is_closed, std::move(io));
        }

        io_entity(io_entity&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_closed = true;
        }

        io_entity& operator=(io_entity&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

        /**
         * @brief Checks FD state
         *
         * If FD is closed or @c io_entity is invalid returns @c true, @c false otherwise
         */
        [[nodiscard]] auto is_closed() const
            -> bool { return _is_closed; }

        /**
         * @brief Extracts all data from @c io_entity object and invalidates it
         * @return A tuple of FD, @c is_closed() result and set of underlying params
         */
        [[nodiscard]] auto extract() {
            return std::tuple {
                std::exchange(_fd, -1),
                std::exchange(_is_closed, true)
            };
        }

        /**
         * @brief Closes file descriptor asynchronously
         * @return @c close_query future object that shall be processed via @c co_await operator
         */
        [[nodiscard]] auto close()
            -> core::close_query { _is_closed = true; return core::close_query{_fd}; }

        virtual ~io_entity() = default;

    // protected:

        int  _fd;                      ///< Socket file descriptor
        bool _is_closed;               ///< Socket closed flag

    private:

        io_guard _guard {_fd, _is_closed};
    };

    /**
     * @brief Encapsulated set of global entities for link processing
     */
    struct io_link_common {

        struct command : core::services::kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override {
                if (res < 0 and fail_cb_handler)
                    fail_cb_handler(res);
                _command_pool.raw_sync(this);
            }

            ~command() override = default;
        };

        static void basic_fail_handler(const int res) {
            throw std::runtime_error(std::string("Write failed: ") + strerror(-res));
        }

        static void(*fail_cb_handler)(int);

        static thread_local nukes::dynamic::reg_freelist<command> _command_pool;
    };

    thread_local nukes::dynamic::reg_freelist<io_link_common::command> io_link_common::_command_pool {};

    inline void(*io_link_common::fail_cb_handler)(int) = basic_fail_handler;

    class any {

        void* _data = nullptr;
        void(*_deleter)(void*) = nullptr;
        // void*(*_measure)() = nullptr;

        template <typename target_t>
        static void deleter_impl(void* mem) {
            delete static_cast<target_t*>(mem);
        }

        // template <typename target_t>
        // auto take() {
        //     reinterpret_cast<>
        // }

    public:

        any() = default;

        any(const any&) = default;

        any(any&&) = default;

        any& operator=(const any&) = default;

        any& operator=(any&&) = default;

        template <typename data_t>
        any(data_t data) noexcept {
            void* mem = malloc(sizeof(data_t));
            _data = new (mem) data_t{std::forward<data_t>(data)};
            _deleter = deleter_impl<data_t>;
        }

        void release() noexcept {
            _data = nullptr;
            _deleter = nullptr;
        }

        ~any() {
            if (_data != nullptr and _deleter != nullptr)
                _deleter(_data);
        }
    };

    /**
     * @brief Common interface for io abstractions
     */
    struct io_link {

        io_link()
            : _fd(-1)
            , _is_closed(true)
            , _data() {}

        io_link(const int fd, const bool is_closed, any data)
            : _fd(fd)
            , _is_closed(is_closed)
            , _data(std::move(data)) { };

        io_link(const int fd)
            : _fd(fd)
            , _is_closed(false) { };

        template<typename io_link_t, typename entry_t>
        static io_link_t transform(entry_t& io) noexcept {
            auto [fd, is_closed] = std::move(io.extract());
            if (fd < 0) is_closed = true;
            return io_link_t {fd, is_closed, std::move(io)};
        }

        io_link(io_link&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _data = std::move(io._data);
            io._fd = -1;
            io._is_closed = true;
        }

        io_link& operator=(io_link&& io) noexcept {
            _fd = io._fd;
            _is_closed = io._is_closed;
            _data = std::move(io._data);
            io._fd = -1;
            io._is_closed = true;
            return *this;
        }

        virtual ~io_link() = default;

        static constexpr int buff_len = 256;

        /**
         * @brief Choosing way of writing
         * @param [in] file file to write to
         * @param [in] buff data to write
         */
        virtual void output_action(__FMT__::string_view buff) = 0;

        template <class... Args>
        void write(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            output_action(buff);
        }

        template <class... Args>
        void writeln(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            output_action(buff);
        }

        void write(const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str));
            output_action(buff);
        }

        void writeln(const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            output_action(buff);
        }

    protected:

        int         _fd;        ///< Socket file descriptor
        bool        _is_closed; ///< Socket closed flag
        any         _data;      ///< FD related params

    private:

        io_guard _guard {_fd, _is_closed};
    };

#define IMPORT_ERROR_HANDLING                                                               \
                                                                                            \
    operator bool() const { return _fd > -1 or INT_MIN == _fd; }                            \
                                                                                            \
    std::string_view error() const {                                                        \
        if (_fd > -1)                                                                       \
            throw std::logic_error("can not receive 'error()' on successed 'io_entity'");   \
        if (INT_MIN == _fd)                                                                 \
            throw std::logic_error("can not receive 'error()' on idle 'io_entry'");         \
        return strerror(-_fd);                                                              \
    }

#define IMPORT_IO_ENTITY_ENV(class)                                                         \
                                                                                            \
    typedef ace::core::io_entity<class> io_entity_t;                                        \
                                                                                            \
    class(const int fd, const bool is_closed)                                               \
        : io_entity_t(fd, is_closed) { };                                                   \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_entity_t::_fd;                                                                 \
    using io_entity_t::_is_closed;                                                          \
                                                                                            \
public:                                                                                     \
                                                                                            \
    class(class&& io) noexcept                                                              \
        : io_entity_t(static_cast<io_entity_t>(std::move(io))) { }                          \
                                                                                            \
    class& operator = (class&& io) noexcept {                                               \
        _fd = io._fd;                                                                       \
        _is_closed = io._is_closed;                                                         \
        io._fd = -1;                                                                        \
        io._is_closed = true;                                                               \
        return *this;                                                                       \
    }                                                                                       \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

#define IMPORT_IO_LINK_ENV(class)                                                           \
                                                                                            \
    typedef ace::core::io_link io_link_t;                                                   \
    typedef ace::core::any any_t;                                                           \
                                                                                            \
    class(const int fd, const bool is_closed, any_t data)                                   \
        : io_link_t(fd, is_closed, std::forward<any_t>(data)) { };                          \
                                                                                            \
    class(const int fd)                                                                     \
        : io_link_t(fd) { };                                                                \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_link_t::_fd;                                                                   \
    using io_link_t::_is_closed;                                                            \
    using io_link_t::_data;                                                                 \
                                                                                            \
public:                                                                                     \
                                                                                            \
    class(class&& io) noexcept {                                                            \
        _fd = io._fd;                                                                       \
        _is_closed = io._is_closed;                                                         \
        _data = std::move(io._data);                                                        \
        io._fd = -1;                                                                        \
        io._is_closed = true;                                                               \
    }                                                                                       \
                                                                                            \
    class& operator = (class&& io) noexcept {                                               \
        _fd = io._fd;                                                                       \
        _is_closed = io._is_closed;                                                         \
        _data = std::move(io._data);                                                        \
        io._fd = -1;                                                                        \
        io._is_closed = true;                                                               \
        return *this;                                                                       \
    }                                                                                       \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;


}

#endif //ACE_IO_H
