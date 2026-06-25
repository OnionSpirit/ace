/**
 * @file io.h
 * @brief Asynchronous I/O abstraction layer built on top of @c io_uring.
 *
 * @details This header defines a type-safe, coroutine-friendly I/O framework
 * that wraps Linux @c io_uring operations.  The key building blocks are:
 *
 *  - <b>@c io_entity<T></b> — CRTP base for file-descriptor owners.  Provides
 *    RAII FD lifecycle (via @c io_guard), move semantics, and async @c close().
 *  - <b>@c io_query<T></b> — CRTP base for individual I/O requests (read,
 *    write, close, etc.).  Each query is an awaitable that suspends the caller
 *    until @c kernel_controller delivers the @c io_uring completion.
 *  - <b>@c io_link</b> — Higher-level abstraction that combines an FD with
 *    @c write() / @c read() methods and a polymorphic @c any data payload.
 *  - <b>@c io_guard</b> — RAII guard that asynchronously closes the FD on
 *    destruction if still open.
 *  - <b>@c io_hanged</b> — Fire-and-forget command queue for I/O operations
 *    that must run even outside a coroutine context (used internally by guards).
 *  - <b>@c any</b> — Minimal type-erased value holder for carrying custom data
 *    alongside an FD.
 *
 * ### Entity state machine
 *
 * @mermaid{ graph LR; Idle[\"invalid (fd=-1)\"]-->Open[\"open\"]; Open-->Closed[\"closed\"]; Closed-->Idle; Open-->Idle; }
 *
 * @see ace::core::services::kernel_controller, ace::core::io_entity,
 *      ace::core::io_link
 */
#ifndef ACE_IO_H
#define ACE_IO_H


#include <climits>
#include <utility>
#include <format>

#include "io.h"
#include "ace/core/services/kernelic.h"

namespace ace::core {
    /**
     * @brief Concept that checks whether a type implements the I/O query interface.
     *
     * @details Satisfied by types that provide a @c setup_query(kernel_observer*)
     * method, which is called by the @c io_query CRTP base to submit the
     * operation to @c kernel_controller.
     *
     * @tparam query_t  Type to check.
     */
    template <typename query_t>
    concept is_query = requires(query_t q, services::kernel_observer* kwp) {
        { q.setup_query(kwp) } -> std::same_as<bool>;
    };

    /**
     * @brief Concept that checks whether a type carries an FD and closed flag.
     *
     * @details Satisfied by types that have @c _fd (int) and @c _is_closed
     * (bool) members — the minimal requirements for an I/O entity.
     *
     * @tparam entry_t  Type to check.
     */
    template <typename entry_t>
    concept is_entity = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
    };

    /**
     * @brief CRTP base for I/O query types — wraps a single @c io_uring operation.
     *
     * @details Derived types (@c read_query, @c write_query, etc.) provide
     * a @c setup_query() method that submits the operation to
     * @c kernel_controller.  The query is an awaitable: @c await_suspend()
     * registers the caller as a waiter and submits the I/O, while
     * @c on_result() (called from the kernelic vortex on CQE arrival) stores
     * the result and re-attaches the waiter.
     *
     * @tparam query_core_t  The concrete query type (CRTP).
     *
     * @warning Does not define @c await_resume() — derived types must provide it.
     */
    template <typename query_core_t>
    struct io_query;

    /** @brief Awaitable for reading from a file descriptor. @see io_query */
    struct read_query;

    /** @brief Awaitable for writing to a file descriptor. @see io_query */
    struct write_query;

    /** @brief Awaitable for closing a file descriptor. @see io_query */
    struct close_query;

    /**
     * @brief RAII guard that asynchronously closes an FD on destruction.
     *
     * @details When the guard goes out of scope, it submits an async close
     * request to @c kernel_controller via the @c io_hanged mechanism.  If
     * the current thread is not running a runner, it falls back to scheduling
     * a close task on the dispatcher.
     */
    struct io_guard;

    /**
     * @brief Customisation point for converting between I/O entity types.
     *
     * @details Specialisations of @c io_caster define how to:
     *  - @c from_entity() — create the target entity from a source entity
     *    (extracting FD and closed flag).
     *  - @c as_link() — cast the entity to a derived @c io_link type.
     *
     * @tparam T  Target entity type (specialised by derived types).
     */
    template <typename>
    struct io_caster {

        // NOTE: Defines how to create current entity from another entity
        static auto from_entity(const int, const bool, auto&&) {
            static_assert(false, "Can not cast from another <io_entity>");
        }

        // NOTE: Defines how to cast current entity to io_link derived type
        static auto as_link(const int, const bool, auto&&) {
            static_assert(false, "Can not cast to <io_link>");
        }
    };

    /**
     * @brief CRTP base for file-descriptor owners with RAII lifecycle.
     *
     * @details Derived types represent entities that hold an open file
     * descriptor.  The base provides:
     *  - Move semantics (transferring FD ownership).
     *  - @c extract() — extract FD + closed flag and invalidate the entity.
     *  - @c close() — async close via @c close_query.
     *  - @c consume() — static factory that moves FD from a source entity.
     *  - @c io_guard member — ensures FD is closed on destruction via RAII.
     *
     * @tparam entity_t  The concrete derived entity type (CRTP).
     */
    template <typename entity_t>
    struct io_entity;

    /**
     * @brief Encapsulated set of global entities for fire-and-forget I/O.
     *
     * @details @c io_hanged provides a thread-local pool of @c command objects
     * that can submit I/O operations without a coroutine context.  Used by
     * @c io_guard to issue async @c close() even when the current thread is
     * not running inside @c runner::run().
     */
    struct io_hanged;

    /**
     * @brief Minimal type-erased value holder for custom data associated with an FD.
     *
     * @details Stores a heap-allocated copy of an arbitrary-typed value and
     * calls the appropriate destructor when released.  Used by @c io_link to
     * carry user-defined context (e.g., connection metadata) alongside the
     * file descriptor.
     */
    class any;

    /**
     * @brief Common base for higher-level I/O abstractions.
     *
     * @details @c io_link combines an FD with a polymorphic @c output_action()
     * (write) / @c input_action() (read) interface and a set of convenience
     * @c write() / @c read() / @c read_vec() / @c read_str() methods.
     *
     * Derived types (@c ace::fs::file_link, @c ace::net::io_connection_link)
     * implement the @c output_action and @c input_action to perform the actual
     * I/O via @c io_uring or a fallback blocking call.
     */
    class io_link;

    /**
     * @brief Thin wrapper for msghdr handling and processing
     */
    class io_buff;

}

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
    using io_entity_t = ace::core::io_entity<class>;                                        \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_entity_t::_fd;                                                                 \
    using io_entity_t::_is_closed;                                                          \
                                                                                            \
public:                                                                                     \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

#define IMPORT_IO_ENTITY_FABRICATION using io_entity_t::io_entity_t;

#define IMPORT_IO_LINK_ENV(class)                                                           \
                                                                                            \
    typedef ace::core::io_link io_link_t;                                                   \
    typedef ace::core::any any_t;                                                           \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_link_t::_fd;                                                                   \
    using io_link_t::_is_closed;                                                            \
    using io_link_t::_data;                                                                 \
                                                                                            \
public:                                                                                     \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

#define IMPORT_IO_LINK_FABRICATION using io_link_t::io_link_t;

#define IMPORT_IO_QUERY_ENV(class)                    \
    typedef ace::core::io_query<class> io_query_t;    \
    using io_query_t::_fd;                            \
    using io_query_t::_res;                           \
    ~class() override = default;


    /**
     * @brief Fire-and-forget I/O command system.
     *
     * @details Provides a thread-local pool of @c command objects for
     * dispatching @c io_uring operations outside of coroutine context.
     * This is used by @c io_guard to close FDs asynchronously even
     * when the destructor runs outside @c runner::run().
     */
    struct ace::core::io_hanged {

        /**
         * @brief A single fire-and-forget I/O command.
         *
         * @details Each @c command wraps an @c io_uring operation.  On
         * completion, @c on_result() calls @c raw_sync() to return the
         * command to the pool.  Errors are handled by the global
         * @c fail_cb_handler.
         */
        struct command : services::kernel_observer {

            std::vector<uint8_t> _buffer {};
            // std::span<char> _user_data {};

            void on_result(const int res) override {
                if (res < 0 and fail_cb_handler)
                    // fail_cb_handler(res, _user_data);
                        fail_cb_handler(res);
                _command_pool.raw_sync(this);
            }

            ~command() override = default;
        };

        // static void basic_fail_handler(const int res, const std::span<char>& user_data) {
        //     throw std::runtime_error(std::format("io operation failed: {}\nuser data: {}", strerror(-res), user_data));
        // }

        static void basic_fail_handler(const int res) {
            throw std::runtime_error(std::format("io operation failed: {}", strerror(-res)));
        }

        // static void(*fail_cb_handler)(int, const std::span<char>&); ///< Fail handler for commands errors handling

        static void(*fail_cb_handler)(int); ///< Fail handler for commands errors handling

        static thread_local nukes::dynamic::reg_freelist<command> _command_pool; ///< Pool of command to start hanged processing wo @c co_await usage
    };

    inline thread_local nukes::dynamic::reg_freelist<ace::core::io_hanged::command> ace::core::io_hanged::_command_pool {};

    // inline void(*ace::core::io_hanged::fail_cb_handler)(int, const std::span<char>&) = basic_fail_handler;

    inline void(*ace::core::io_hanged::fail_cb_handler)(int) = basic_fail_handler;


    template <typename query_core_t>
    struct ace::core::io_query : traits::future_traits<query_core_t>, services::kernel_observer {

        IMPORT_FUTURE_ENV(query_core_t);

        explicit io_query(const int fd) : _fd(fd) {
            static_assert(is_query<query_core_t>,
                "Query object shall implement 'bool setup_query(ace::core::kernel_waiter*)' method");
        }

        /**
         * @brief Conductor that stores the waiting task until the I/O operation completes.
         *
         * @details Installed into the awaiting coroutine's promise by
         * @c await_suspend().  @c forward() saves the task, which is later
         * re-attached by @c on_result().  @c cancel() submits a cancellation
         * request to @c kernel_controller.
         */
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
            _runner_identity = coroutine.promise()._runner.template as<runner_pool_t>();
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


    /**
     * @brief Awaitable @c io_uring read query.
     *
     * @details Submits @c io_uring_prep_read via @c kernel_controller.
     * On completion, null-terminates the read buffer.
     */
    struct ace::core::read_query : io_query<read_query> {

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


    /**
     * @brief Awaitable @c io_uring write query.
     *
     * @details Submits @c io_uring_prep_write via @c kernel_controller.
     */
    struct ace::core::write_query : io_query<write_query> {

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

    /**
     * @brief Awaitable @c io_uring close query.
     *
     * @details Submits @c io_uring_prep_close via @c kernel_controller.
     */
    struct ace::core::close_query : io_query<close_query> {

        IMPORT_IO_QUERY_ENV(close_query)

        close_query() = delete;

        explicit close_query(const int fd) : io_query_t(fd) {}

        bool setup_query(kernel_observer* kwp) const noexcept {
            return services::kernel_controller::close(kwp, _fd);
        }

        [[nodiscard]] int await_resume() const { return _res; }
    };


    /**
     * @brief Simple RAII guard that ensures an FD is asynchronously closed.
     *
     * @details Constructed with a reference to the FD and closed flag.  On
     * destruction, if the FD is still valid and not already closed, it
     * submits an async @c close() via @c io_hanged or falls back to
     * scheduling a close task on the dispatcher.
     */
    struct ace::core::io_guard final {
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
            if (_fd < 0 or _closed) return;
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (core::io_hanged::command* cmd; runner_identity and core::io_hanged::_command_pool.capture(cmd)) [[likely]]
            {
                cmd->_runner_identity = runner_identity;
                if (not services::kernel_controller::close(cmd, _fd) and io_hanged::fail_cb_handler)
                    io_hanged::fail_cb_handler(EAGAIN); // Maybe EIO?
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            else schedule(pending_close(_fd));
        }
    };


    /**
     * @brief CRTP base for I/O entities — owners of a file descriptor with RAII lifecycle.
     *
     * @details Provides move semantics, @c extract(), async @c close(), and
     * the @c consume() static factory.  An @c io_guard member ensures the FD
     * is closed on destruction.
     *
     * @tparam entity_t  Derived entity type.
     */
    template <typename entity_t>
    struct ace::core::io_entity {

        io_entity()
            : _fd(-1)
            , _is_closed(true) {}

        io_entity(const int fd, const bool is_closed)
            : _fd(fd)
            , _is_closed(is_closed) { };

        // NOTE: This method is made to never forget to move ownership
        template<typename entry_t>
        static entity_t consume(entry_t& io) noexcept {
            auto [fd, is_closed] = io.extract();
            if (fd < 0) is_closed = true;
            return io_caster<entity_t>::from_entity(fd, is_closed, std::move(io));
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

    protected:

        int  _fd;                      ///< Socket file descriptor
        bool _is_closed;               ///< Socket closed flag

    private:

        io_guard _guard {_fd, _is_closed};
    };


    /**
     * @brief Type-erased heap-allocated value holder.
     *
     * @details Stores an arbitrary copy-constructible value on the heap and
     * destroys it when the @c any goes out of scope.  Supports move semantics
     * and explicit @c release() to drop the managed value without destroying
     * the @c any itself.
     */
    class ace::core::any {

        void* _data = nullptr;
        void(*_deleter)(void*) = nullptr;

        template <typename target_t>
        static void deleter_impl(void* mem) {
            delete static_cast<target_t*>(mem);
        }

    public:

        any() = default;

        any(const any&) = default;

        any(any&&) = default;

        any& operator=(const any&) = default;

        any& operator=(any&&) = default;

        template <typename data_t>
        any(data_t&& data) noexcept {
            void* mem = malloc(sizeof(data_t));
            *static_cast<data_t*>(mem) = std::forward<data_t>(data);
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
     * @brief Common base for higher-level I/O abstractions.
     *
     * @details Owns an FD and an optional @c any data payload.  Provides
     * @c writeln(), @c write(), @c read() (and variants like @c read_vec(),
     * @c read_str()) as convenience methods.  Derived types implement
     * @c output_action() and @c input_action() for the actual I/O.
     */
    class ace::core::io_link {

    protected:


        static constexpr int buff_len = 256;

        /**
         * @brief Writing function
         * @param [in] buff data to write
         */
        virtual void output_action(std::span<const char> buff) = 0;

        /**
         * @brief Reading function
         * @param [out] buff buffer to read to
         * @param [in] len size of read buffer
         */
        virtual promise<int> input_action(void *buff, std::size_t len) = 0;

    public:

        io_link()
            : _fd(-1)
            , _is_closed(true)
            , _data() {}

        io_link(const int fd, const bool is_closed)
            : _fd(fd)
            , _is_closed(is_closed) { };

        io_link(const int fd, const bool is_closed, any data)
            : _fd(fd)
            , _is_closed(is_closed)
            , _data(std::move(data)) { };

        // NOTE: This method is made to never forget to move ownership
        template<typename entity_t>
        static auto consume(entity_t& io) noexcept {
            auto [fd, is_closed] = io.extract();
            if (fd < 0) is_closed = true;
            return io_caster<entity_t>::as_link(fd, is_closed, std::move(io));
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

        template <class... Args>
        void writeln(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            output_action(buff);
        }

        void writeln(const std::string_view&& str) {
            const std::string buff = std::string(std::forward<const std::string_view>(str)) + '\n';
            output_action(buff);
        }

        template <class... Args>
        void write(std::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = std::format(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            output_action(buff);
        }

        void write(const std::string_view&& str) {
            output_action(std::forward<const std::string_view>(str));
        }

        void write(const void *buf, const size_t len) {
            const auto buff = std::span<const char>(static_cast<const char*>(buf), len);
            output_action(buff);
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        auto write(const std::vector<data_t>& buf) {
            const auto buff = std::span<const char>(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
            output_action(buff);
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        auto write(const std::array<data_t, len_v>& buf) {
            const auto buff = std::span<const char>(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
            output_action(buff);
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        auto write(const std::span<data_t, len_v>& buf) {
            const auto buff = std::span<const char>(buf.data(), buf.size_bytes());
            output_action(buff);
        }

        ACE_AWAIT_NODISCARD async<int> read(void *buf, const size_t len, const int flags = 0) {
            co_return co_await input_action(buf, len);
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::vector<data_t>& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
        }

        ACE_AWAIT_NODISCARD async<int> read(std::string& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size());
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto read_vec(const int flags = 0)
        -> async<std::expected<std::vector<data_t>, int>> {
            static constexpr int buff_len_bytes = buff_len * (sizeof(data_t) / sizeof(char));

            std::deque<std::array<data_t, buff_len>> acc;
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await input_action(reinterpret_cast<void*>(buff.data()), buff_len_bytes);
            if (bytes_read < 1) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await input_action(reinterpret_cast<void*>(buff.data()), buff_len_bytes);
                if (bytes_read < 1) co_return std::unexpected(-bytes_read);
                total += bytes_read;
            }

            // NOTE: Cast to data object size
            total /= (sizeof(data_t) / sizeof(char));
            std::vector<data_t> res {};
            res.reserve(total);
            for (auto& buf : acc) {
                const int write_items { (total > buff_len) ? buff_len : total };
                for (int i = 0; i < write_items; ++i)
                    res.push_back(std::forward<data_t>(buf[i]));
                total -= write_items;
            }
            co_return res;
        }

        ACE_AWAIT_NODISCARD auto read_str(const int flags = 0)
        -> async<std::expected<std::string, int>> {

            std::deque<std::array<char, buff_len>> acc {};
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await input_action(buff.data(), buff_len);
            if (bytes_read < 1) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await input_action(buff.data(), buff_len);
                if (bytes_read < 1) co_return std::unexpected(-bytes_read);
                total += bytes_read;
            }

            std::string res {};
            // NOTE: + null term char slot
            res.reserve(total + 1);
            for (auto& buf : acc) {
                const int write_bytes { (total > buff_len) ? buff_len : total };
                res.append(buf.data(), write_bytes);
                total -= write_bytes;
            }
            co_return res;
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::array<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), len_v * (sizeof(data_t) / sizeof(char)));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::span<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), buf.size_bytes());
        }


    protected:

        int         _fd;        ///< Socket file descriptor
        bool        _is_closed; ///< Socket closed flag
        any         _data;      ///< FD related params

    private:

        io_guard _guard {_fd, _is_closed};
    };

    namespace ace {
        using io_reactive_link = std::shared_ptr<core::io_link>;
    }


    class ace::core::io_buff {

        msghdr      _hdr {
            .msg_iov = nullptr,
            .msg_iovlen = 0
        };

        iovec*      _chunk_list_end   = nullptr;
        iovec*      _chunk_list_begin = nullptr;
        unsigned    _tail_capacity = 0;
        bool        _terminated = false;

        static constexpr std::size_t control_hdr_len = sizeof(void*);

        iovec* allocate_buf(const size_t len) {
            // NOTE: Allocating and subscribing new buff to chunk set
            const auto buf = services::kernel_controller::iovec_allocate(len);
            auto** new_control_hdr = static_cast<iovec**>(buf->iov_base);
            *new_control_hdr = nullptr;

            buf->iov_len = len;
            return buf;
        }

        void init_buf_list(iovec* buf) {
            _chunk_list_begin = _chunk_list_end = buf;
            // NOTE: Saving tail buf capacity
            _tail_capacity = buf->iov_len - control_hdr_len;
            ++_hdr.msg_iovlen;
        }

        void append_buf_list(iovec* buf) {
            // NOTE: Saving tail buf capacity
            _tail_capacity = buf->iov_len - control_hdr_len;
            auto** old_control_hdr = static_cast<iovec**>(_chunk_list_end->iov_base);
            *old_control_hdr = buf;
            _chunk_list_end = buf;
            ++_hdr.msg_iovlen;
        }

        void prepend_buf_list(iovec* buf) {
            auto** new_control_hdr = static_cast<iovec**>(buf->iov_base);
            *new_control_hdr = _chunk_list_begin;
            _chunk_list_begin = buf;
            ++_hdr.msg_iovlen;
        }

        static void* announce_buf_mem(const iovec* buf) {
            return static_cast<char*>(buf->iov_base) + control_hdr_len;
        }

        /**
         * @brief Applies C-string termination
         */
        void c_terminate() {
            if (_terminated) return;
            if (const auto mem = memtail(1)) {
                *static_cast<char*>(mem) = '\0';
                _terminated = true;
            }
        }

        /**
         * @brief Extends mempool if it's needed by required len and returns pointer at the buffer ending
         * @param len Required preallocated memory size
         * @return ptr to the preallocated memory at the buffer tail
         */
        void* memtail(const size_t len) {
            // NOTE: Getting tail buffer
            const auto tail_buf = _chunk_list_end;
            // NOTE: Checking if new data fits into tail buf
            if (tail_buf and (_tail_capacity - tail_buf->iov_len) > len) {
                const auto ret = static_cast<char*>(tail_buf->iov_base) + tail_buf->iov_len + control_hdr_len;
                tail_buf->iov_len = tail_buf->iov_len + len;
                return ret;
            }
            // NOTE: Allocating buffer
            const auto buf = allocate_buf(len);

            // NOTE: Initializing list with buf or appending it to the list
            if (not tail_buf) init_buf_list(buf);
            else append_buf_list(buf);

            // NOTE: Getting memory pointer without control_hdr
            return announce_buf_mem(buf);
        }

        /**
         * @brief Extends mempool and returns pointer at the buffer beginning
         * @param len Required preallocated memory size
         * @return ptr to the preallocated memory at the buffer head
         */
        void* memhead(const size_t len) {
            // NOTE: Getting tail buffer
            const auto tail_buf = _chunk_list_end;

            // NOTE: Allocating buffer
            const auto buf = allocate_buf(len);

            // NOTE: Initializing list with buf or prepending it to the list
            if (not tail_buf) init_buf_list(buf);
            else prepend_buf_list(buf);

            // NOTE: Getting memory pointer without control_hdr
            return announce_buf_mem(buf);
        }

        template <void* (io_buff::*mem_selector)(std::size_t), class... Args>
        bool emplace(std::format_string<Args...>&& fmt, Args&&... args) {
            const size_t len = std::formatted_size(fmt, args...);
            if (const auto mem = (this->*mem_selector)(len)) {
                std::format_to(mem, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                return true;
            }
            return false;
        }

        template <void* (io_buff::*mem_selector)(std::size_t)>
        bool emplace(const std::string_view&& str) {
            const size_t len = str.size();
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, str.data(), len);
                return true;
            }
            return false;
        }

        template <void* (io_buff::*mem_selector)(std::size_t)>
        bool emplace(const void *buf, const size_t len) {
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf, len);
                return true;
            }
            return false;
        }

        template <void* (io_buff::*mem_selector)(std::size_t), typename data_t>
        requires std::is_pod_v<data_t>
        bool emplace(const std::vector<data_t>& buf) {
            const size_t len = buf.size() * (sizeof(data_t) / sizeof(char));
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

        template <void* (io_buff::*mem_selector)(std::size_t), typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool emplace(const std::array<data_t, len_v>& buf) {
            constexpr size_t len = len_v * (sizeof(data_t) / sizeof(char));
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

        template <void* (io_buff::*mem_selector)(std::size_t), typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool emplace(const std::span<data_t, len_v>& buf) {
            constexpr size_t len = len_v * (sizeof(data_t) / sizeof(char));
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

    public:

        template <class... Args>
        bool append(std::format_string<Args...>&& fmt, Args&&... args) {
            return emplace<&io_buff::memtail>(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        bool append(const std::string_view&& str) {
            return emplace<&io_buff::memtail>(std::forward<const std::string_view>(str));
        }

        bool append(const void *buf, const size_t len) {
            return emplace<&io_buff::memtail>(std::forward<const void*>(buf), std::forward<const size_t>(len));
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        bool append(const std::vector<data_t>& buf) {
            return emplace<&io_buff::memtail>(std::forward<const std::vector<data_t>>(buf));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool append(const std::array<data_t, len_v>& buf) {
            return emplace<&io_buff::memtail>(std::forward<const std::array<data_t, len_v>>(buf));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool append(const std::span<data_t, len_v>& buf) {
            return emplace<&io_buff::memtail>(std::forward<const std::span<data_t, len_v>>(buf));
        }

        template <class... Args>
        bool prepend(std::format_string<Args...>&& fmt, Args&&... args) {
            return emplace<&io_buff::memhead>(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        bool prepend(const std::string_view&& str) {
            return emplace<&io_buff::memhead>(std::forward<const std::string_view>(str));
        }

        bool prepend(const void *buf, const size_t len) {
            return emplace<&io_buff::memhead>(std::forward<const void*>(buf), std::forward<const size_t>(len));
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        bool prepend(const std::vector<data_t>& buf) {
            return emplace<&io_buff::memhead>(std::forward<const std::vector<data_t>>(buf));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool prepend(const std::array<data_t, len_v>& buf) {
            return emplace<&io_buff::memhead>(std::forward<const std::array<data_t, len_v>>(buf));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool prepend(const std::span<data_t, len_v>& buf) {
            return emplace<&io_buff::memhead>(std::forward<const std::span<data_t, len_v>>(buf));
        }

        /**
         * @return Returns message iovec sequence len
         */
        [[nodiscard]] std::size_t get_len() const { return _hdr.msg_iovlen; }

        /**
         * @brief Assembles buffer into @c msghdr
         * @return Pointer to @c msghdr
         */
        msghdr* assemble() {
            auto* iovecs = new iovec[_hdr.msg_iovlen];
            const iovec* current = *static_cast<iovec**>(_chunk_list_begin->iov_base);
            for (int i =0; i < _hdr.msg_iovlen and current not_eq nullptr; ++i) {
                iovecs[i].iov_base = static_cast<char*>(current->iov_base) + control_hdr_len;
                iovecs[i].iov_len = current->iov_len;
                current = *static_cast<iovec**>(current->iov_base);
            }
            _hdr.msg_iov = iovecs;
            return &_hdr;
        }

        void clear() {
            iovec* current = *static_cast<iovec**>(_chunk_list_begin->iov_base);
            while (current not_eq nullptr) {
                const auto next = *static_cast<iovec**>(current->iov_base);
                services::kernel_controller::iovec_deallocate(current);
                current = next;
            }
            _chunk_list_begin = nullptr;
            _chunk_list_end = nullptr;
            _tail_capacity = 0;
            _terminated = false;
            delete [] _hdr.msg_iov;
            _hdr.msg_iov = nullptr;
            _hdr.msg_iovlen = 0;
        }

        ~io_buff() { clear(); }
    };

#endif //ACE_IO_H
