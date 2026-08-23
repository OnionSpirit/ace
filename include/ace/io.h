/**
 * @file io.h
 * @brief Asynchronous I/O abstraction layer built on top of @c io_uring.
 *
 * @details This header defines a type-safe, coroutine-friendly I/O framework
 * that wraps Linux @c io_uring operations.  The key building blocks are:
 *
 *  - <b>@c io::entity<T> </b> — CRTP base for sole file-descriptor owners.
 *    Provides RAII FD lifecycle (via @c io_guard), move-only semantics, and
 *    ownership-transferring async @c close().
 *  - <b>@c io_query<T> </b> — CRTP base for individual I/O requests (read,
 *    write, close, etc.).  Each query is an awaitable that suspends the caller
 *    until @c kernel_controller delivers the @c io_uring completion.
 *  - <b>@c io_link </b> — Higher-level abstraction that combines an FD with
 *    @c write() / @c read() methods and a polymorphic @c any data payload.
 *  - <b>@c io_guard </b> — RAII guard that asynchronously closes the FD on
 *    destruction if still open.
 *  - <b>@c io::outcast </b> — Fire-and-forget command queue for I/O operations
 *    that must run even outside a coroutine context (used internally by guards).
 *  - <b>@c any </b> — Minimal type-erased value holder for carrying custom data
 *    alongside an FD.
 *
 * ### Entity state machine
 *
 * @mermaid{ graph LR; Idle[\"invalid (fd=-1)\"]-->Open[\"entity owns FD\"]; Open--close()-->Closing[\"close query owns FD\"]; Closing-->Closed[\"closed\"]; Open--move/extract-->Idle; }
 *
 * @see ace::services::kernel_controller, ace::io::entity,
 *      ace::io::link
 */
#ifndef ACE_IO_H
#define ACE_IO_H


#include <climits>
#include <format>
#include <limits>
#include <utility>

#include "ace/services/kernelic.h"

namespace ace::io {
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
     * @c on_result() (called from the kernelic service on CQE arrival) stores
     * the result and re-attaches the waiter.
     *
     * @tparam query_core_t  The concrete query type (CRTP).
     *
     * @warning Does not define @c await_resume() — derived types must provide it.
     */
    template <typename query_core_t>
    struct query;

    /**
     * @brief Awaitable for binary reads from a file descriptor.
     * @details Never appends a NUL terminator; caller-owned storage must remain
     * writable and alive until completion or cancellation.
     * @see io_query
     */
    struct read_query;

    /** @brief Awaitable for writing to a file descriptor. @see io_query */
    struct write_query;

    /**
     * @brief Awaitable for closing a file descriptor.
     * @details Public construction is non-owning; @c entity::close() returns an
     * owning form that dispatches even when discarded before submission.
     * @see io_query
     */
    struct close_query;

    /**
     * @brief Thin wrapper for msghdr handling and processing
     */
    class buffer;

    /**
     * @brief RAII guard that asynchronously closes an FD on destruction.
     *
     * @details When the guard goes out of scope, it submits an async close
     * request to @c kernel_controller via the @c io::outcast mechanism.  If
     * the current thread is not running a runner, it falls back to scheduling
     * a close task on the dispatcher.  The same dispatch path accepts FD
     * ownership from discarded entity-owned close queries.
     */
    struct guard;

    /**
     * @brief Customisation point for converting between I/O entity types.
     *
     * @details Specializations of @c io::caster define how to:
     *  - @c from_entity() — create the target entity from a source entity
     *    (extracting FD and closed flag).
     *  - @c as_link() — cast the entity to an @c io::link derived type.
     *
     * @tparam T  Target entity type (specialized by derived types).
     */
    template <typename T>
    struct caster {

        // NOTE: Defines how to create current entity from another entity
        static auto from_entity(const int, const bool, auto&&) {
            static_assert(false, "Can not cast from another <entity>");
        }

        // NOTE: Defines how to cast current entity to io_link derived type
        static auto as_link(const int, const bool, auto&&) {
            static_assert(false, "Can not cast to <link>");
        }
    };

    /**
     * @brief CRTP base for file-descriptor owners with RAII lifecycle.
     *
     * @details Derived types represent entities that hold an open file
     * descriptor.  The base provides:
     *  - Move-only semantics (transferring sole FD ownership).
     *  - @c extract() — extract FD + closed flag and invalidate the entity.
     *  - @c close() — immediately invalidate and transfer the FD to an owning
     *    async @c close_query.
     *  - @c consume() — static factory that moves FD from a source entity.
     *  - @c io_guard member — ensures FD is closed on destruction via RAII.
     *
     * @tparam entity_t  The concrete derived entity type (CRTP).
     */
    template <typename entity_t>
    struct entity;

    /**
     * @brief Encapsulated set of global entities for fire-and-forget I/O.
     *
     * @details @c io::outcast provides a thread-local pool of @c command objects
     * that can submit I/O operations without a coroutine context.  Used by
     * @c io_guard to issue async @c close() even when the current thread is
     * not running inside @c runner::run().
     */
    struct outcast;

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
     * @c write() / @c read() / @c read_buf() methods.
     * Derived types example (@c ace::fs::file_link, @c ace::net::connection_link)
     * implement the @c output_action and @c input_action to perform the actual
     * I/O via @c io_uring or a fallback blocking call.
     */
    class link;

    /// @brief Return type for input operations
    using input_t = std::expected<buffer, int>;

    /// @brief Shared link type
    using slink = std::shared_ptr<link>;

}

/**
 * @def IMPORT_ERROR_HANDLING
 * @brief Injects error-state helpers into a derived entity or link class.
 * @details Defines @c operator bool() (true when the FD is valid or idle) and
 * @c error() (throws on a successful/idle entity, otherwise returns
 * @c strerror() of the negated FD value).
 */
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

/**
 * @def IMPORT_IO_ENTITY_ENV(class)
 * @brief Injects @c io::entity<class> base aliases, move-only special members,
 * protected FD members and the @c IMPORT_ERROR_HANDLING block into a derived
 * @c io::entity class.
 */
#define IMPORT_IO_ENTITY_ENV(class)                                                         \
                                                                                            \
    using io_entity_t = ace::io::entity<class>;                                             \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_entity_t::_fd;                                                                 \
    using io_entity_t::_is_closed;                                                          \
                                                                                            \
public:                                                                                     \
                                                                                            \
    class(const class&) = delete;                                                           \
    class& operator=(const class&) = delete;                                                \
    class(class&&) noexcept = default;                                                      \
    class& operator=(class&&) noexcept = default;                                           \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

/**
 * @def IMPORT_IO_ENTITY_FABRICATION
 * @brief Injects the @c io::entity base constructors into a derived entity class.
 */
#define IMPORT_IO_ENTITY_FABRICATION using io_entity_t::io_entity_t;

/**
 * @def IMPORT_IO_LINK_ENV(class)
 * @brief Injects @c io::link base aliases, move-only special members,
 * protected members and the @c IMPORT_ERROR_HANDLING block into a derived
 * @c io::link class.
 */
#define IMPORT_IO_LINK_ENV(class)                                                           \
                                                                                            \
    typedef ace::io::link io_link_t;                                                        \
    typedef ace::io::any any_t;                                                             \
                                                                                            \
protected:                                                                                  \
                                                                                            \
    using io_link_t::_fd;                                                                   \
    using io_link_t::_is_closed;                                                            \
    using io_link_t::_data;                                                                 \
                                                                                            \
public:                                                                                     \
                                                                                            \
    class(const class&) = delete;                                                           \
    class& operator=(const class&) = delete;                                                \
    class(class&&) noexcept = default;                                                      \
    class& operator=(class&&) noexcept = default;                                           \
                                                                                            \
    IMPORT_ERROR_HANDLING                                                                   \
                                                                                            \
    ~class() override = default;

/**
 * @def IMPORT_IO_LINK_FABRICATION
 * @brief Injects the @c io::link base constructors into a derived link class.
 */
#define IMPORT_IO_LINK_FABRICATION using io_link_t::io_link_t;

/**
 * @def IMPORT_IO_QUERY_ENV(class)
 * @brief Injects the @c io::query<class> base alias, protected members and a
 * defaulted virtual destructor into a derived query class.
 */
#define IMPORT_IO_QUERY_ENV(class)                    \
    typedef ace::io::query<class> io_query_t;         \
    using io_query_t::_fd;                            \
    using io_query_t::_res;                           \
    ~class() override = default;


    template <typename query_core_t>
    struct ace::io::query : core::traits::future_traits<query_core_t>, services::kernel_observer {

        IMPORT_FUTURE_ENV(query_core_t);

        /**
         * @brief Constructs a query bound to a file descriptor.
         * @param fd File descriptor to perform the I/O operation on.
         * @note Static-asserts that @c query_core_t satisfies the @c is_query concept.
         */
        explicit query(const int fd) : _fd(fd) {
            static_assert(is_query<query_core_t>,
                "Query object shall implement 'bool setup_query(ace::core::kernel_waiter*)' method");
        }

        /**
         * @brief Router that stores the waiting task until the I/O operation completes.
         *
         * @details Installed into the awaiting coroutine's promise by
         * @c await_suspend().  @c redirect() saves the node, which is later
             * re-attached by @c on_result().  @c cancel() follows the concrete
             * query's cancellation policy and otherwise requests kernel cancellation.
         */
        struct query_router : runner_router {

            query_router() = delete;

            /**
             * @brief Binds the router to its owning query.
             * @param query_ Owning @c query instance.
             */
            explicit query_router(query* query_)
                : _query(query_) {};

            /**
             * @brief Stores the awaiting task node for later re-attachment.
             * @param node Task node of the suspended coroutine.
             */
            void redirect(omni_node node) override {
                _query->_waiter = node;
            }

            /**
             * @brief Requests cancellation unless the concrete query overrides it.
             * @details Queries with a @c cancel_query() member control their own
             * cancellation policy; all other queries use kernel cancellation.
             */
            void cancel() override {
                // TODO: Improve cancel with pop from local submission queue
                if constexpr (requires(query_core_t& query_) { query_.cancel_query(); })
                    static_cast<query_core_t*>(_query)->cancel_query();
                else
                    services::kernel_controller::cancel(_query, 0);
            }

            ~query_router() override = default;

            query* _query;                 ///< Owning query instance
        };

        omni_node _waiter;               ///< Awaited task storage
        int       _res       = INT_MIN;  ///< IO_URING operation result
        const int _fd;                   ///< FD to interact with
        bool      _is_silent = false;    ///< Mark to detach and not suspend

        /**
         * @brief Queries never complete synchronously.
         * @return Always @c false.
         */
        bool await_ready() override { return false; };

        /**
         * @brief Submits the operation to @c kernel_controller and suspends the caller.
         * @param coroutine Awaiting coroutine handle (provides the runner identity
         *                  and the router slot to install the @c query_router into).
         * @return @c true when the operation was submitted and the caller must suspend,
         *         @c false when the query is silent (@c _is_silent) or submission failed.
         */
        bool await_suspend(auto coroutine) {
            _runner_identity = coroutine.promise()._runner.template as<runner_pool_t>();
            if (_fd < 0)
                throw std::logic_error("Trying to make query on failed 'io_entity' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (INT_MIN == _fd)
                throw std::logic_error("Trying to make query on idle 'io_entry' [Query object type: "
                    + std::string{typeid(query_core_t).name()} + "]");
            if (static_cast<query_core_t*>(this)->setup_query(this) and not _is_silent) {
                coroutine.promise()._runner_router = query_router{this};
                return true;
            }
            return false;
        }

        /**
         * @brief Stores the I/O result and re-attaches the waiting task.
         * @param res Completion result from @c kernel_controller (negative errno on failure).
         */
        void on_result(const int res) override {
            _res = res;
            if (_waiter)
                core::runner::reattach(_waiter);
        }

        /** @brief Virtual destructor (defaulted). */
        ~query() override = default;
    };


    /**
     * @brief Awaitable @c io_uring read query.
     *
     * @details Submits @c io_uring_prep_read via @c kernel_controller.  The
     * destination is treated as raw binary storage: exactly the bytes reported
     * by @c await_resume() are written and no NUL terminator is appended.
     *
     * @warning The storage referenced by @c buf must remain writable and alive
     * until the query completes or is canceled.
     */
    struct ace::io::read_query : query<read_query> {

        read_query() = delete;

        /**
         * @brief Constructs a read query.
         * @param fd File descriptor to read from.
         * @param buf Destination binary buffer whose lifetime extends through
         *            query completion.
         * @param nbytes Writable size of @p buf in bytes.
         * @param offset File offset (0 = current position).
         */
        [[nodiscard]] explicit read_query(const int fd, void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        /**
         * @brief Submits the read operation to @c kernel_controller.
         * @param kwp Kernel observer receiving the completion.
         * @return @c true on successful submission.
         */
        bool setup_query(kernel_observer* kwp) const {
            return services::kernel_controller::read(kwp, _fd, _buf, _nbytes, _offset);
        }

        /**
         * @brief Returns the raw read result without writing beyond the bytes read.
         * @return Number of bytes read, or a negative errno value.
         */
        [[nodiscard]] int await_resume() const { return _res; }

        const int _fd;                ///< File descriptor to read from
        void *_buf;                   ///< Caller-owned binary destination, alive through completion
        const unsigned _nbytes;       ///< Writable capacity of @c _buf in bytes
        const uint64_t _offset;       ///< File offset
    };


    /**
     * @brief Awaitable @c io_uring write query.
     *
     * @details Submits @c io_uring_prep_write via @c kernel_controller.
     */
    struct ace::io::write_query : query<write_query> {

        write_query() = delete;

        /**
         * @brief Constructs a write query.
         * @param fd File descriptor to write to.
         * @param buf Source buffer.
         * @param nbytes Number of bytes to write.
         * @param offset File offset (0 = current position).
         */
        explicit write_query(const int fd, const void *buf, const unsigned nbytes, const uint64_t offset = 0)
            : query(fd)
            , _fd(fd)
            , _buf(buf)
            , _nbytes(nbytes)
            , _offset(offset) {}

        /**
         * @brief Submits the write operation to @c kernel_controller.
         * @param kwp Kernel observer receiving the completion.
         * @return @c true on successful submission.
         */
        bool setup_query(kernel_observer* kwp) const {
            return services::kernel_controller::write(kwp, _fd, _buf, _nbytes, _offset);
        }

        /**
         * @brief Returns the write result.
         * @return Number of bytes written, or a negative errno value.
         */
        [[nodiscard]] int await_resume() const { return _res; }

        const int _fd;                ///< File descriptor to write to
        const void *_buf;             ///< Source buffer
        const unsigned _nbytes;       ///< Number of bytes to write
        const uint64_t _offset;       ///< File offset
    };

    /**
     * @brief Awaitable @c io_uring close query.
     *
     * @details A directly constructed @c close_query(fd) is a non-owning
     * awaitable: destroying it without awaiting does not close that FD.  The
     * owning form returned by @c entity::close() holds sole ownership, cannot
     * cancel an already submitted close, and dispatches the close through the
     * guard cleanup path if it is destroyed before submission.
     */
    struct ace::io::close_query : query<close_query> {

        typedef ace::io::query<close_query> io_query_t;
        using io_query_t::_fd;
        using io_query_t::_res;

        close_query() = delete;

        /**
         * @brief Constructs a non-owning close query.
         * @param fd File descriptor to close when the query is awaited.
         * @warning The caller remains responsible for @p fd until this query is
         * awaited and submitted.  Discarding this public form has no effect.
         */
        explicit close_query(const int fd) : io_query_t(fd) {}

        /** @brief Copying is disabled because an owning query has sole FD ownership. */
        close_query(const close_query&) = delete;
        close_query& operator=(const close_query&) = delete;

        /**
         * @brief Transfers an unsubmitted query and any FD ownership.
         * @warning A query must not be moved after it has been submitted.
         */
        close_query(close_query&& query) noexcept
            : io_query_t(query._fd)
            , _owns_fd(std::exchange(query._owns_fd, false))
            , _is_submitted(std::exchange(query._is_submitted, false))
            , _is_noop(std::exchange(query._is_noop, false)) {
            _res = query._res;
        }

        close_query& operator=(close_query&&) = delete;

        /**
         * @brief Reports an idempotent close of an already-invalid entity as ready.
         * @return @c true when no descriptor needs closing.
         */
        bool await_ready() override { return _is_noop; }

        /**
         * @brief Submits the close operation to @c kernel_controller.
         * @param kwp Kernel observer receiving the completion.
         * @return @c true on successful submission.
         */
        bool setup_query(kernel_observer* kwp) noexcept {
            return _is_submitted = services::kernel_controller::close(kwp, _fd);
        }

        /**
         * @brief Handles cancellation routed from the awaiting coroutine.
         * @details Entity-owned closes are non-cancelable after submission so
         * the descriptor cannot remain open.  Public non-owning queries retain
         * the normal query cancellation behavior.
         */
        void cancel_query() noexcept {
            if (not _owns_fd)
                services::kernel_controller::cancel(this, 0);
        }

        /**
         * @brief Returns the close result.
         * @return @c 0 on success, or a negative errno value.
         */
        [[nodiscard]] int await_resume() const { return _res; }

        /**
         * @brief Dispatches an owned close that was discarded before submission.
         * @details A submitted query never retries from its destructor, including
         * when the kernel reports a close error.
         */
        ~close_query() override;

    private:

        /** @brief Constructs the owning form used exclusively by @c entity::close(). */
        close_query(const int fd, const bool owns_fd) noexcept
            : io_query_t(fd)
            , _owns_fd(owns_fd and fd >= 0)
            , _is_noop(fd < 0) {
            if (fd < 0)
                _res = 0;
        }

        bool _owns_fd = false;      ///< This query has sole ownership of @c _fd.
        bool _is_submitted = false; ///< A close SQE or deferred kernel request owns the operation.
        bool _is_noop = false;      ///< Entity was already invalid; no kernel operation is needed.

        template <typename>
        friend struct entity;
    };


    /**
     * @brief Type-erased heap-allocated value holder.
     *
     * @details Stores an arbitrary copy-constructible value on the heap and
     * destroys it when the @c any goes out of scope.  Supports move semantics
     * and explicit @c release() to drop the managed value without destroying
     * the @c any itself.
     */
    class ace::io::any {

        void* _data = nullptr;                 ///< Heap-allocated managed value
        void(*_deleter)(void*) = nullptr;      ///< Type-erased deleter for @c _data

        /**
         * @brief Destroys the stored value and frees its memory.
         * @tparam target_t Stored value type.
         * @param mem Raw heap pointer to destroy.
         */
        template <typename target_t>
        static void deleter_impl(void* mem) {
            static_cast<target_t*>(mem)->~target_t();
            free(mem);
        }

    public:

        /** @brief Constructs an empty (null) holder. */
        any() = default;

        /** @brief Copying is not allowed — the managed value is owned. */
        any(const any&) = delete;

        /**
         * @brief Move constructor — transfers the managed value.
         */
        any(any&& other) noexcept
            : _data(std::exchange(other._data, nullptr))
            , _deleter(std::exchange(other._deleter, nullptr)) {}

        any& operator=(const any&) = delete;

        /**
         * @brief Move assignment — releases the current value, then transfers.
         */
        any& operator=(any&& other) noexcept {
            if (this != &other) {
                if (_data && _deleter) _deleter(_data);
                _data = std::exchange(other._data, nullptr);
                _deleter = std::exchange(other._deleter, nullptr);
            }
            return *this;
        }

        /**
         * @brief Constructs from an arbitrary value (heap-allocated copy/move).
         * @tparam data_t Value type to store.
         * @param data Value to store.
         */
        template <typename data_t>
        any(data_t&& data) noexcept {
            if (void* mem = malloc(sizeof(data_t))) {
                new (mem) data_t(std::forward<data_t>(data));
                _data = mem;
                _deleter = deleter_impl<data_t>;
            }
        }

        /**
         * @brief Destroys the managed value without destroying the @c any itself.
         */
        void release() noexcept {
            if (_data && _deleter) _deleter(_data);
            _data = nullptr;
            _deleter = nullptr;
        }

        /**
         * @brief Destroys the managed value if present.
         */
        ~any() {
            if (_data != nullptr and _deleter != nullptr)
                _deleter(_data);
        }
    };


    /**
     * @brief Scatter-gather buffer built on a chain of @c iovec chunks.
     *
     * @details Each chunk carries a hidden control header (a pointer to the
     * next chunk), so the whole chain can be assembled into a compact
     * @c msghdr iovec array on demand.  Supports @c append()/@c prepend()/
     * @c expand()/@c shape(), formatted writes, @c clone() and @c as<T>()
     * conversions.
     */
    class ace::io::buffer {

        ACE_CACHE_LINE(0)

        /// @brief Assembled message header (@c msg_iov is null until @c assemble())
        msghdr _hdr {
            .msg_iov = nullptr,
            .msg_iovlen = 0
        };
        /// @brief Tail chunk of the list (last appended or expanded)
        iovec*           _chunk_list_end     = nullptr;

        ACE_CACHE_LINE(1)

        /// @brief Chunk preceding the tail (used by @c prepend() and @c shape())
        iovec*           _chunk_list_pre_end = nullptr;
        /// @brief Head chunk of the list
        iovec*           _chunk_list_begin   = nullptr;
        /// @brief Sum of payload lengths of all chunks
        std::size_t      _total_len          = 0;


        /**
         * @brief Allocates a new chunk of @p len payload bytes plus a control header.
         * @param len Payload size of the new chunk.
         * @return New chunk.
         * @throws std::bad_alloc on allocation failure.
         */
        iovec* allocate_buf(const size_t len) {
            if (len > std::numeric_limits<std::size_t>::max() - control_hdr_len)
                throw std::bad_alloc();
            // NOTE: Allocating and subscribing new buff to chunk set
            const auto buf = services::kernel_controller::iovec_allocate(len + control_hdr_len);
            auto** new_control_hdr = static_cast<iovec**>(buf->iov_base);
            *new_control_hdr = nullptr;

            buf->iov_len = len;
            _total_len += len;
            return buf;
        }

        /**
         * @brief Returns a chunk to the arena and adjusts the total length.
         * @param buf Chunk to deallocate.
         */
        void deallocate_buf(iovec* buf) {
            _total_len -= buf->iov_len;
            services::kernel_controller::iovec_deallocate(buf);
        }

        /**
         * @brief Makes @p buf the sole chunk of the list.
         * @param buf Chunk to install as both head and tail.
         */
        void init_buf_list(iovec* buf) {
            _chunk_list_begin = _chunk_list_end = buf;
            ++_hdr.msg_iovlen;
        }

        /**
         * @brief Links @p buf after the current tail chunk.
         * @param buf Chunk to append.
         */
        void append_buf_list(iovec* buf) {
            auto** old_control_hdr = static_cast<iovec**>(_chunk_list_end->iov_base);
            *old_control_hdr = buf;
            _chunk_list_pre_end = _chunk_list_end;
            _chunk_list_end = buf;
            ++_hdr.msg_iovlen;
        }

        /**
         * @brief Links @p buf before the current head chunk.
         * @param buf Chunk to prepend.
         */
        void prepend_buf_list(iovec* buf) {
            auto** new_control_hdr = static_cast<iovec**>(buf->iov_base);
            *new_control_hdr = _chunk_list_begin;
            if (not _chunk_list_pre_end)
                _chunk_list_pre_end = _chunk_list_begin;
            _chunk_list_begin = buf;
            ++_hdr.msg_iovlen;
        }

        /**
         * @brief Returns the payload pointer of a chunk, skipping its control header.
         * @param buf Chunk to inspect.
         * @return Pointer to the chunk payload.
         */
        static void* announce_buf_mem(const iovec* buf) {
            return static_cast<char*>(buf->iov_base) + control_hdr_len;
        }

        /**
         * @brief Extends mempool if it's needed by required len and returns pointer at the buffer ending
         * @param len Required preallocated memory size
         * @return ptr to the preallocated memory at the buffer tail
         */
        void* memtail(const size_t len) {
            // NOTE: Forbidding modification after assembling
            if (_hdr.msg_iov) return nullptr;
            // NOTE: Getting tail buffer
            const auto tail_buf = _chunk_list_end;

            // NOTE: Allocating buffer
            const auto buf = allocate_buf(len);
            if (not buf) return nullptr;

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
            // NOTE: Forbidding modification after assembling
            if (_hdr.msg_iov) return nullptr;
            // NOTE: Getting tail buffer
            const auto tail_buf = _chunk_list_end;

            // NOTE: Allocating buffer
            const auto buf = allocate_buf(len);
            if (not buf) return nullptr;

            // NOTE: Initializing list with buf or prepending it to the list
            if (not tail_buf) init_buf_list(buf);
            else prepend_buf_list(buf);

            // NOTE: Getting memory pointer without control_hdr
            return announce_buf_mem(buf);
        }

        /**
         * @brief Formats @p args into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting append (@c memtail) or prepend (@c memhead) memory.
         * @tparam Args Format argument types.
         * @param fmt Format string.
         * @param args Format arguments.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t), class... Args>
        requires (sizeof...(Args) > 0)
        bool emplace(std::format_string<Args...>&& fmt, Args&&... args) {
            const size_t len = std::formatted_size(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            if (auto mem = static_cast<char*>((this->*mem_selector)(len))) {
                std::format_to_n(mem, len, std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
                return true;
            }
            return false;
        }

        /**
         * @brief Copies a string view into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting memory location.
         * @param str String to copy.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t)>
        bool emplace(const std::string_view&& str) {
            const size_t len = str.size();
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, str.data(), len);
                return true;
            }
            return false;
        }

        /**
         * @brief Copies a byte range [@p first, @p last) into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting memory location.
         * @param first Start of the byte range.
         * @param last End of the byte range.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t)>
        bool emplace(const void *first, const void* last) {
            const size_t len = static_cast<const std::byte*>(last) - static_cast<const std::byte*>(first);
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, first, len);
                return true;
            }
            return false;
        }

        /**
         * @brief Copies a POD vector into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting memory location.
         * @tparam data_t POD element type.
         * @param buf Vector to copy.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t), typename data_t>
        requires std::is_pod_v<data_t>
        bool emplace(const std::vector<data_t>& buf) {
            const size_t len = buf.size() * (sizeof(data_t) / sizeof(char));
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

        /**
         * @brief Copies a POD array into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting memory location.
         * @tparam data_t POD element type.
         * @tparam len_v Array length.
         * @param buf Array to copy.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t), typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool emplace(const std::array<data_t, len_v>& buf) {
            constexpr size_t len = len_v * (sizeof(data_t) / sizeof(char));
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

        /**
         * @brief Copies a POD span into memory obtained from @p mem_selector.
         * @tparam mem_selector Member function selecting memory location.
         * @tparam data_t POD element type.
         * @tparam len_v Span length (or @c std::dynamic_extent).
         * @param buf Span to copy.
         * @return @c true on success, @c false if allocation failed or the buffer was already assembled.
         */
        template <void* (buffer::*mem_selector)(std::size_t), typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool emplace(const std::span<data_t, len_v>& buf) {
            const size_t len = [&] {
                if constexpr (len_v == std::dynamic_extent)
                    return buf.size() * sizeof(data_t);
                else
                    return len_v * sizeof(data_t);
            }();
            if (const auto mem = (this->*mem_selector)(len)) {
                std::memcpy(mem, buf.data(), len);
                return true;
            }
            return false;
        }

    public:

        /** @brief Constructs an empty buffer. */
        buffer() = default;

        /** @brief Copying is not allowed — the chunk list is owned. */
        buffer(const buffer&) = delete;
        /** @brief Copying is not allowed — the chunk list is owned. */
        buffer& operator=(const buffer&) = delete;

        /**
         * @brief Move constructor — transfers the chunk list and total length.
         */
        buffer(buffer&& b) noexcept {
            _hdr = b._hdr;
            b._hdr = msghdr{};
            _chunk_list_begin = b._chunk_list_begin;
            b._chunk_list_begin = nullptr;
            _chunk_list_pre_end = b._chunk_list_pre_end;
            b._chunk_list_pre_end = nullptr;
            _chunk_list_end = b._chunk_list_end;
            b._chunk_list_end = nullptr;
            _total_len = b._total_len;
            b._total_len = 0;
        }

        /**
         * @brief Move assignment — transfers the chunk list and total length.
         */
        buffer& operator=(buffer&& b) noexcept {
            _hdr = b._hdr;
            b._hdr = msghdr{};
            _chunk_list_begin = b._chunk_list_begin;
            b._chunk_list_begin = nullptr;
            _chunk_list_pre_end = b._chunk_list_pre_end;
            b._chunk_list_pre_end = nullptr;
            _chunk_list_end = b._chunk_list_end;
            b._chunk_list_end = nullptr;
            _total_len = b._total_len;
            b._total_len = 0;
            return *this;
        }

        /** @brief Size of the per-chunk control header (a pointer to the next chunk). */
        static constexpr std::size_t control_hdr_len = sizeof(void*);

        /**
         * @brief Formats and appends data to the buffer tail.
         * @tparam Args Format argument types.
         * @param fmt Format string.
         * @param args Format arguments.
         * @return @c true on success.
         */
        template <class... Args>
        bool append(std::format_string<Args...>&& fmt, Args&&... args) {
            return emplace<&buffer::memtail>(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        /**
         * @brief Appends a string view to the buffer tail.
         * @param str String to append.
         * @return @c true on success.
         */
        bool append(const std::string_view&& str) {
            return emplace<&buffer::memtail>(std::forward<const std::string_view>(str));
        }

        /**
         * @brief Appends a byte range [@p first, @p last) to the buffer tail.
         * @param first Start of the byte range.
         * @param last End of the byte range.
         * @return @c true on success.
         */
        bool append(const void *first, const void* last) {
            return emplace<&buffer::memtail>(std::forward<const void*>(first), std::forward<const void*>(last));
        }

        /**
         * @brief Appends a POD vector to the buffer tail.
         * @tparam data_t POD element type.
         * @param buf Vector to append.
         * @return @c true on success.
         */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        bool append(const std::vector<data_t>& buf) {
            return emplace<&buffer::memtail>(std::forward<const std::vector<data_t>>(buf));
        }

        /**
         * @brief Appends a POD array to the buffer tail.
         * @tparam data_t POD element type.
         * @tparam len_v Array length.
         * @param buf Array to append.
         * @return @c true on success.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool append(const std::array<data_t, len_v>& buf) {
            return emplace<&buffer::memtail>(std::forward<const std::array<data_t, len_v>>(buf));
        }

        /**
         * @brief Appends a POD span to the buffer tail.
         * @tparam data_t POD element type.
         * @tparam len_v Span length (or @c std::dynamic_extent).
         * @param buf Span to append.
         * @return @c true on success.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool append(const std::span<data_t, len_v>& buf) {
            return emplace<&buffer::memtail>(std::forward<const std::span<data_t, len_v>>(buf));
        }

        /**
         * @brief Appends formatted data followed by a newline to the buffer tail.
         * @tparam Args Format argument types.
         * @param args Format arguments.
         * @return @c true on success.
         */
        template <class... Args>
        bool appendln(Args&&... args) {
            return emplace<&buffer::memtail>(std::forward<Args>(args)...)
                and emplace<&buffer::memtail>("\n");
        }

        /**
         * @brief Formats and prepends data to the buffer head.
         * @tparam Args Format argument types.
         * @param fmt Format string.
         * @param args Format arguments.
         * @return @c true on success.
         */
        template <class... Args>
        bool prepend(std::format_string<Args...>&& fmt, Args&&... args) {
            return emplace<&buffer::memhead>(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        /**
         * @brief Prepends a string view to the buffer head.
         * @param str String to prepend.
         * @return @c true on success.
         */
        bool prepend(const std::string_view&& str) {
            return emplace<&buffer::memhead>(std::forward<const std::string_view>(str));
        }

        /**
         * @brief Prepends a byte range [@p first, @p last) to the buffer head.
         * @param first Start of the byte range.
         * @param last End of the byte range.
         * @return @c true on success.
         */
        bool prepend(const void *first, const void* last) {
            return emplace<&buffer::memhead>(std::forward<const void*>(first), std::forward<const void*>(last));
        }

        /**
         * @brief Prepends a POD vector to the buffer head.
         * @tparam data_t POD element type.
         * @param buf Vector to prepend.
         * @return @c true on success.
         */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        bool prepend(const std::vector<data_t>& buf) {
            return emplace<&buffer::memhead>(std::forward<const std::vector<data_t>>(buf));
        }

        /**
         * @brief Prepends a POD array to the buffer head.
         * @tparam data_t POD element type.
         * @tparam len_v Array length.
         * @param buf Array to prepend.
         * @return @c true on success.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool prepend(const std::array<data_t, len_v>& buf) {
            return emplace<&buffer::memhead>(std::forward<const std::array<data_t, len_v>>(buf));
        }

        /**
         * @brief Prepends a POD span to the buffer head.
         * @tparam data_t POD element type.
         * @tparam len_v Span length (or @c std::dynamic_extent).
         * @param buf Span to prepend.
         * @return @c true on success.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        bool prepend(const std::span<data_t, len_v>& buf) {
            return emplace<&buffer::memhead>(std::forward<const std::span<data_t, len_v>>(buf));
        }

        /**
         * @brief Converts the buffer contents into type @c T.
         * @tparam T Target type (specialized for @c std::string and @c std::vector<std::byte>).
         * @return Decayed default-constructed value when no specialization exists.
         */
        template <typename T>
        T as() const {
            static_assert("No 'as()' specialization for passed type <T>");
            return std::decay_t<T>{};
        }

        /**
         * @brief Reserve space in the buffer. Main purpose is to get memory to read to
         * @param [in] len extension size
         * @return memory pointer to the added part
         */
        void* expand(const std::size_t len) { return memtail(len); }

        /**
         * @brief Shrinks tail chunk to a provided len
         * @param [in] len new len for the tail chunk (last appended or expanded memory)
         */
        void shape(const std::size_t len) {
            if (not _chunk_list_end) return;
            auto* new_tail = allocate_buf(len);
            memcpy(new_tail->iov_base, _chunk_list_end->iov_base, len + control_hdr_len);
            // NOTE: Prepend or append done then use this case
            if (_chunk_list_pre_end) {
                *static_cast<iovec**>(_chunk_list_pre_end->iov_base) = new_tail;
                deallocate_buf(_chunk_list_end);
                _chunk_list_end = new_tail;
            }
            // NOTE: There was single buffer then replacing it
            else {
                deallocate_buf(_chunk_list_end);
                _chunk_list_begin = _chunk_list_end = new_tail;
            }
        }

        /**
         * @brief Assembles buffer into @c msghdr .
         *
         * @c assemble(...) actually has effect once before @c disassemble(),
         * @c clear() operations.
         *
         * - If you use @c io::buffer with @c ace::io interfaces then manual usage of this function overload has no point.
         *
         * - Allocates a control buffer of @c iovec on the special pool with same len as inner chunk list.
         *
         * - Memory size of @c iovec message buffer is much less than actual size of all listed chunks.
         *
         * - Whole chunk list would be provided into the @c iovec message buffer.
         *
         * @warning @c msghdr* lifetime is equal to @c ace::io::buffer lifetime
         * @return Pointer to local @c msghdr entity
         */
        msghdr* assemble() {

            iovec* current = _chunk_list_begin;

            // NOTE: Effect once guard
            if (_hdr.msg_iov)
                return &_hdr;

            _hdr.msg_iov = services::kernel_controller::iovec_pool_allocate(_hdr.msg_iovlen);

            for (size_t i =0; i < _hdr.msg_iovlen and current not_eq nullptr; ++i) {
                _hdr.msg_iov[i].iov_base = static_cast<char*>(current->iov_base) + control_hdr_len;
                _hdr.msg_iov[i].iov_len = current->iov_len;
                current = *static_cast<iovec**>(current->iov_base);
            }
            return &_hdr;
        }

        /**
         * @brief Drops local @c msghdr control metadata. Allows to reassemble without clear
         */
        void disassemble() {
            services::kernel_controller::iovec_pool_deallocate(_hdr.msg_iov, _hdr.msg_iovlen);
            _hdr.msg_iov = nullptr;
        }

        /**
         * @brief Clones current buffer data to a brand-new instance of buffer
         * @note Does not modify the source buffer: the clone is built from a
         * read-only walk of the chunk list.
         * @return Buffer instance with data copy
         */
        [[nodiscard]] buffer clone() const {
            buffer cl;
            const iovec* current = _chunk_list_begin;
            while (current not_eq nullptr) {
                const std::byte* data = static_cast<std::byte*>(current->iov_base) + control_hdr_len;
                cl.append( data, data + current->iov_len);
                current = *static_cast<iovec**>(current->iov_base);
            }

            return std::forward<buffer>(cl);
        }

        /**
         * @brief Sets the destination/source address of the message header.
         * @tparam addr_t Address type (e.g. @c sockaddr_in).
         * @param addr Address object to reference.
         */
        template <typename addr_t>
        void set_msg_name(addr_t& addr) { _hdr.msg_name = &addr; _hdr.msg_namelen = sizeof(addr_t); }

        /**
         * @brief Sets the control buffer of the message header.
         * @param ptr Control buffer pointer.
         */
        void set_msg_control(void* ptr) { _hdr.msg_control = ptr; }

        /**
         * @brief Sets the control buffer length of the message header.
         * @param len Control buffer length.
         */
        void set_msg_controllen(size_t len) { _hdr.msg_controllen = len; }

        /**
         * @brief Sets the flags of the message header.
         * @param flags Message flags.
         */
        void set_msg_flags(int flags) { _hdr.msg_flags = flags; }

        /**
         * @brief Clears and releases all resources
         */
        void clear() {
            disassemble();
            iovec* current = _chunk_list_begin;
            while (current not_eq nullptr) {
                const auto next = *static_cast<iovec**>(current->iov_base);
                deallocate_buf(current);
                current = next;
            }
            _chunk_list_begin = nullptr;
            _chunk_list_pre_end = nullptr;
            _chunk_list_end = nullptr;
            _hdr.msg_iovlen = 0;
            _hdr.msg_name = nullptr;
            _hdr.msg_namelen = 0;
            _hdr.msg_control = nullptr;
            _hdr.msg_controllen = 0;
            _hdr.msg_flags = 0;
            _total_len = 0;
        }

        /**
         * @brief Total payload length of the buffer.
         * @return Sum of all chunk payload lengths.
         */
        [[nodiscard]] std::size_t len() const { return _total_len; }

        /** @brief Releases all chunks and the assembled iovec array. */
        ~buffer() { clear(); }

        friend class ace::io::link;            ///< I/O layer needs chunk list access
        friend class std::formatter<buffer>;   ///< Formatting needs chunk list access
    };


    /**
     * @brief std::formatter specialization enabling @c std::format of an @c io::buffer.
     * @details Writes the concatenated payloads of all chunks into the format context.
     */
    template <>
    struct std::formatter<ace::io::buffer> {
        constexpr auto parse(std::format_parse_context& ctx) {
            return ctx.begin();
        }

        auto format(const ace::io::buffer& buf, std::format_context& ctx) const {
            auto out_buf = ctx.out();
            const iovec* current = buf._chunk_list_begin;
            while (current not_eq nullptr) {
                char* data = static_cast<char*>(current->iov_base) + ace::io::buffer::control_hdr_len;
                out_buf = std::copy_n(data, current->iov_len, out_buf);
                current = *static_cast<iovec**>(current->iov_base);
            }
            return out_buf;
        }
    };


    /**
     * @brief Fire-and-forget I/O command system.
     *
     * @details Provides a thread-local pool of @c command objects for
     * dispatching @c io_uring operations outside of coroutine context.
     * This is used by @c io_guard to close FDs asynchronously even
     * when the destructor runs outside @c runner::run().
     */
    struct ace::io::outcast {

        /**
         * @brief A single fire-and-forget I/O command.
         *
         * @details Each @c command wraps an @c io_uring operation.  On
         * completion, @c on_result() calls @c raw_sync() to return the
         * command to the pool.  Errors are handled by the global
         * @c fail_cb_handler.
         */
        struct command : services::kernel_observer {

            buffer _buffer {};                                              ///< Payload of the fire-and-forget command
            std::span<const char> _description { "<not specified>" };  ///< User data passed to the fail handler

            /**
             * @brief Handles the completion of a fire-and-forget command.
             * @param res Operation result (negative errno on failure).
             * @details Invokes the global @c fail_cb_handler on failure, then
             * returns the command to the pool via @c raw_sync().
             */
            void on_result(const int res) override {
                if (res < 0 and fail_cb_handler) {
                    // NOTE: A throwing handler must not kill the kernel
                    // service coroutine — otherwise the ring is never pinged
                    // again and all subsequent I/O leaks.
                    try { fail_cb_handler(res, _description); }
                    catch (const std::exception& e) {
                        std::cerr << "outcast-io-failure : {\n" << e.what() << "\n}" << std::endl;
                    }
                    catch (...) {
                        std::cerr << "outcast-io-failure : { <unknown> }" << std::endl;
                    }
                }
                _command_pool.raw_sync(this);
            }

            /** @brief Virtual destructor (defaulted). */
            ~command() override = default;
        };

        /**
         * @brief Default fail handler — throws an exception describing the failed operation.
         * @param res Negative errno value of the failed operation.
         * @param description User data attached to the command.
         */
        static void basic_fail_handler(const int res, const std::span<const char>& description) {
            throw std::runtime_error(std::format(
                "\tio-result-code : {},\n\tio-result-description : {},\n\tio-description : {}",
                -res, strerror(-res), std::string{description.data(), description.size()}
            ));
        }

        static void(*fail_cb_handler)(int, const std::span<const char>&); ///< Fail handler for commands errors handling

        static thread_local nukes::dynamic::reg_freelist<command> _command_pool; ///< Pool of command to start outcast processing wo @c co_await usage
    };

    inline thread_local nukes::dynamic::reg_freelist<ace::io::outcast::command> ace::io::outcast::_command_pool {};

    inline void(*ace::io::outcast::fail_cb_handler)(int, const std::span<const char>&) = basic_fail_handler;


    /**
     * @brief Simple RAII guard that ensures an FD is asynchronously closed.
     *
     * @details Constructed with a reference to the FD and closed flag.  On
     * destruction, if the FD is still valid and not already closed, it
     * submits an async @c close() via @c io::outcast or falls back to
     * scheduling a close task on the dispatcher.
     */
    struct ace::io::guard final {
        /** @brief Guard must be bound to FD and closed-flag references. */
        guard() = delete;

        /**
         * @brief Binds the guard to the FD and closed-flag of an entity.
         * @param fd Reference to the entity's file descriptor.
         * @param closed Reference to the entity's closed flag.
         */
        explicit guard(const int& fd, const bool& closed)
            : _fd(fd)
            , _closed(closed) {}

        const int& _fd;      ///< Referenced file descriptor
        const bool& _closed; ///< Referenced closed flag

        /**
         * @brief Busy-path close task: awaits @c close_query for @p fd.
         * @param fd File descriptor to close.
         */
        static task pending_close(const int fd) noexcept {
            if (const int res = co_await close_query{fd}; res < 0)
                std::cerr << strerror(res) << std::endl;
        }

        /**
         * @brief Dispatches one asynchronous close through the guard cleanup path.
         * @param fd File descriptor whose ownership is being released.
         * @details Uses the @c io::outcast command pool when a runner identity is
         * available, otherwise falls back to scheduling @c pending_close().
         */
        static void dispatch_close(const int fd) noexcept {
            if (fd < 0) return;
            // NOTE: Trying to get current runner.
            // NOTE: Doing it manually for cases when classic 'runner::run()' is unused
            auto* runner_identity = core::runner::get().as<runner_pool_t>();
            // NOTE: Pushing data to slot, and setting identity for kernelic
            if (outcast::command* cmd; runner_identity and outcast::_command_pool.capture(cmd)) [[likely]]
            {
                cmd->_runner_identity = runner_identity;
                cmd->_description = "io::guard file descriptor lazy-close";
                if (services::kernel_controller::close(cmd, fd))
                    return;
                // NOTE: Submission rejected; no completion will return the command to the pool.
                outcast::_command_pool.raw_sync(cmd);
            }
            // NOTE: If can not get slot or identity not found -> using busy behavior
            schedule(pending_close(fd));
        }

        /** @brief Releases the referenced FD if it is still valid and open. */
        ~guard() noexcept {
            if (_fd < 0 or _closed) return;
            dispatch_close(_fd);
        }
    };


    inline ace::io::close_query::~close_query() {
        if (_owns_fd and not _is_submitted) {
            _owns_fd = false;
            guard::dispatch_close(_fd);
        }
    }


    /**
     * @brief CRTP base for I/O entities — owners of a file descriptor with RAII lifecycle.
     *
     * @details Provides move-only sole ownership, @c extract(), async @c close(),
     * and the @c consume() static factory.  An @c io_guard member ensures the
     * FD is closed on destruction.  Moving invalidates the source; move
     * assignment first releases the destination's old FD through that guard.
     *
     * @tparam entity_t  Derived entity type.
     */
    template <typename entity_t>
    struct ace::io::entity {

        /** @brief Constructs an invalid (idle) entity with FD @c -1. */
        entity()
            : _fd(-1)
            , _is_closed(true) {}

        /**
         * @brief Constructs an entity from an FD and closed state.
         * @param fd File descriptor to own.
         * @param is_closed Initial closed flag.
         */
        entity(const int fd, const bool is_closed)
            : _fd(fd)
            , _is_closed(is_closed) { };

        /** @brief Copying is disabled because an entity solely owns its FD. */
        entity(const entity&) = delete;
        entity& operator=(const entity&) = delete;

        // NOTE: This method is made to never forget to move ownership
        /**
         * @brief Static factory: extracts the FD and closed flag from a source
         * entity and creates the current entity type from them.
         * @tparam entry_t Source entity type.
         * @param io Source entity (consumed via @c extract()).
         * @return New entity of type @c entity_t.
         */
        template<typename entry_t>
        static entity_t consume(entry_t& io) noexcept {
            auto [fd, is_closed] = io.extract();
            if (fd < 0) is_closed = true;
            return caster<entity_t>::from_entity(fd, is_closed, std::move(io));
        }

        /**
         * @brief Move constructor that transfers sole FD ownership.
         * @details The source is invalidated and the newly constructed guard is
         * bound to the destination's own fields.
         */
        entity(entity&& io) noexcept
            : _fd(std::exchange(io._fd, -1))
            , _is_closed(std::exchange(io._is_closed, true)) {}

        /**
         * @brief Move assignment that releases the old FD then transfers ownership.
         * @details Self-move is a no-op.  The guard is reconstructed against the
         * destination fields after its old binding performs normal cleanup.
         */
        entity& operator=(entity&& io) noexcept {
            if (this == &io)
                return *this;
            _guard.~guard();
            _fd = std::exchange(io._fd, -1);
            _is_closed = std::exchange(io._is_closed, true);
            new (&_guard) guard(_fd, _is_closed);
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
         * @return A tuple of the FD and the @c is_closed() result
         */
        [[nodiscard]] auto extract() {
            return std::tuple {
                std::exchange(_fd, -1),
                std::exchange(_is_closed, true)
            };
        }

        /**
         * @brief Immediately invalidates the entity and transfers its FD to an
         * owning asynchronous close query.
         * @details The returned query is non-cancelable after submission.  If
         * discarded before submission, its destructor still dispatches exactly
         * one close through the guard cleanup path.  Calling @c close() again
         * returns a ready query whose result is @c 0.
         * @return Owning @c close_query, or a ready no-op query when invalid.
         */
        [[nodiscard]] auto close()
            -> io::close_query {
            _is_closed = true;
            return io::close_query{std::exchange(_fd, -1), true};
        }

        /** @brief Virtual destructor (defaulted). */
        virtual ~entity() = default;

    protected:

        int  _fd;                      ///< Socket file descriptor
        bool _is_closed;               ///< Socket closed flag

    private:

        guard _guard {_fd, _is_closed}; ///< RAII guard permanently bound to this entity's fields
    };


    /**
     * @brief Common base for higher-level I/O abstractions.
     *
     * @details Solely owns an FD and an optional @c any data payload.  The type
     * is move-only; moving invalidates the source, and move assignment releases
     * the destination's old FD through its guard before transfer.  Provides
     * @c writeln(), @c write() and @c read() overloads (raw buffer,
     * @c std::string, POD vector/array/span) plus @c read_buf() as
     * convenience methods.  Derived types implement
     * @c output_action() and @c input_action() for the actual I/O.
     */
    class ace::io::link {

    protected:

        /**
         * @brief Writing function
         * @param [in] buff data to write
         */
        virtual void output_action(buffer&& buff) = 0;

        /**
         * @brief Reading function
         * @param [out] buff buffer to read to
         * @param [in] len size of read buffer
         */
        virtual promise<int> input_action(void *buff, std::size_t len) = 0;

    public:

        /** @brief Constructs an invalid (idle) link. */
        link()
            : _fd(-1)
            , _is_closed(true)
            , _data() {}

        /**
         * @brief Constructs a link from an FD and closed state.
         * @param fd File descriptor to own.
         * @param is_closed Initial closed flag.
         */
        link(const int fd, const bool is_closed)
            : _fd(fd)
            , _is_closed(is_closed) { };

        /**
         * @brief Constructs a link from an FD, closed state and payload data.
         * @param fd File descriptor to own.
         * @param is_closed Initial closed flag.
         * @param data Custom payload associated with the FD.
         */
        link(const int fd, const bool is_closed, any data)
            : _fd(fd)
            , _is_closed(is_closed)
            , _data(std::move(data)) { };

        /** @brief Copying is disabled because a link solely owns its FD. */
        link(const link&) = delete;
        link& operator=(const link&) = delete;

        // NOTE: This method is made to never forget to move ownership
        /**
         * @brief Static factory: extracts the FD and closed flag from a source
         * entity and creates the current link type from them.
         * @tparam entity_t Source entity type.
         * @param io Source entity (consumed via @c extract()).
         * @return New link instance.
         */
        template<typename entity_t>
        static auto consume(entity_t& io) noexcept {
            auto [fd, is_closed] = io.extract();
            if (fd < 0) is_closed = true;
            return caster<entity_t>::as_link(fd, is_closed, std::move(io));
        }

        /**
         * @brief Move constructor that transfers sole FD ownership and payload.
         * @details The source is invalidated and the destination guard remains
         * bound to the destination fields.
         */
        link(link&& io) noexcept
            : _fd(std::exchange(io._fd, -1))
            , _is_closed(std::exchange(io._is_closed, true))
            , _data(std::move(io._data)) {}

        /**
         * @brief Move assignment that releases the old FD then transfers ownership.
         * @details Self-move is a no-op.  The guard is reconstructed against the
         * destination fields after its old binding performs normal cleanup.
         */
        link& operator=(link&& io) noexcept {
            if (this == &io)
                return *this;
            _guard.~guard();
            _fd = std::exchange(io._fd, -1);
            _is_closed = std::exchange(io._is_closed, true);
            _data = std::move(io._data);
            new (&_guard) guard(_fd, _is_closed);
            return *this;
        }

        /** @brief Virtual destructor (defaulted). */
        virtual ~link() = default;

        /**
         * @brief Formats data plus a newline and writes it to the link.
         * @tparam Args Format argument types.
         * @param fmt Format string.
         * @param args Format arguments.
         */
        template <class... Args>
        void writeln(std::format_string<Args...>&& fmt, Args&&... args) {
            buffer buff {};
            buff.append(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            buff.append("\n");
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a string view plus a newline to the link.
         * @param str String to write.
         */
        void writeln(const std::string_view&& str) {
            buffer buff {};
            buff.append(std::forward<const std::string_view>(str));
            buff.append("\n");
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a buffer's contents plus a newline to the link.
         * @param buf Buffer to write.
         */
        void writeln(buffer&& buf) {
            buf.append("\n");
            output_action(std::forward<buffer>(buf));
        }

        /**
         * @brief Formats data and writes it to the link.
         * @tparam Args Format argument types.
         * @param fmt Format string.
         * @param args Format arguments.
         */
        template <class... Args>
        void write(std::format_string<Args...>&& fmt, Args&&... args) {
            buffer buff {};
            buff.append(std::forward<std::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a string view to the link.
         * @param str String to write.
         */
        void write(const std::string_view&& str) {
            buffer buff {};
            buff.append(std::forward<const std::string_view>(str));
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a byte range [@p first, @p last) to the link.
         * @param first Start of the byte range.
         * @param last End of the byte range.
         */
        void write(const void *first, const void* last) {
            buffer buff {};
            buff.append(first, last);
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a POD vector to the link.
         * @tparam data_t POD element type.
         * @param buf Vector to write.
         */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        auto write(const std::vector<data_t>& buf) {
            buffer buff {};
            buff.append(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a POD array to the link.
         * @tparam data_t POD element type.
         * @tparam len_v Array length.
         * @param buf Array to write.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        auto write(const std::array<data_t, len_v>& buf) {
            buffer buff {};
            buff.append(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a POD span to the link.
         * @tparam data_t POD element type.
         * @tparam len_v Span length (or @c std::dynamic_extent).
         * @param buf Span to write.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        auto write(const std::span<data_t, len_v>& buf) {
            buffer buff {};
            buff.append(buf.data(), buf.data() + buf.size_bytes());
            output_action(std::forward<buffer>(buff));
        }

        /**
         * @brief Writes a buffer to the link.
         * @param buf Buffer to write.
         */
        void write(buffer&& buf) {
            output_action(std::forward<buffer>(buf));
        }

        /**
         * @brief Reads up to @p len binary bytes into a raw buffer.
         * @details No NUL terminator is appended.  The destination storage must
         * remain writable and alive until the returned coroutine completes.
         * @param buf Destination binary buffer.
         * @param len Writable buffer size in bytes.
         * @param flags Reserved for future use.
         * @return Number of bytes read, or a negative errno value.
         */
        ACE_AWAIT_NODISCARD async<int> read(void *buf, const size_t len, const int flags = 0) {
            co_return co_await input_action(buf, len);
        }

        /**
         * @brief Reads into a POD vector.
         * @tparam data_t POD element type.
         * @param buf Destination vector.
         * @param flags Reserved for future use.
         * @return Number of bytes read, or a negative errno value.
         */
        template <typename data_t>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::vector<data_t>& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
        }

        /**
         * @brief Reads into an existing string buffer.
         * @param buf Destination string.
         * @param flags Reserved for future use.
         * @return Number of bytes read, or a negative errno value.
         */
        ACE_AWAIT_NODISCARD async<int> read(std::string& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size());
        }

        /**
         * @brief Reads into a POD array.
         * @tparam data_t POD element type.
         * @tparam len_v Array length.
         * @param buf Destination array.
         * @param flags Reserved for future use.
         * @return Number of bytes read, or a negative errno value.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::array<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), len_v * (sizeof(data_t) / sizeof(char)));
        }

        /**
         * @brief Reads into a POD span.
         * @tparam data_t POD element type.
         * @tparam len_v Span length (or @c std::dynamic_extent).
         * @param buf Destination span.
         * @param flags Reserved for future use.
         * @return Number of bytes read, or a negative errno value.
         */
        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD async<int> read(std::span<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), buf.size_bytes());
        }

        /**
         * @brief Reads until a short read occurs, growing the buffer as needed.
         * @param flags Reserved for future use.
         * @return @c io::input_t holding the buffer on success, or @c std::unexpected with a negative errno.
         */
        async<io::input_t> read_buf(const int flags = 0) {
            static constexpr int buf_len = 64;

            buffer buf {};
            auto data = buf.expand(buf_len);

            int bytes_read = co_await input_action(data, buf_len);
            if (bytes_read < 1) co_return std::unexpected(-bytes_read);

            while (bytes_read == buf_len) {
                data = buf.expand(buf_len);
                bytes_read = co_await input_action(data, buf_len);
                if (bytes_read < 1) co_return std::unexpected(-bytes_read);
            }

            if (bytes_read < buf_len)
                buf.shape(bytes_read);

            co_return std::forward<buffer>(buf);
        }

    protected:

        int         _fd;        ///< Socket file descriptor
        bool        _is_closed; ///< Socket closed flag
        any         _data;      ///< FD related params

    private:

        guard _guard {_fd, _is_closed}; ///< RAII guard permanently bound to this link's fields
    };


// ====================================- io::buffer::as<...> specialisations -====================================


    /**
     * @brief Specialization: converts the buffer contents into a @c std::string.
     * @return Concatenated payloads of all chunks.
     */
    template <>
    inline std::string ace::io::buffer::as<std::string>() const {
        std::string str;
        const iovec* current = _chunk_list_begin;
        for (size_t i =0; i < _hdr.msg_iovlen and current not_eq nullptr; ++i) {
            str.append(static_cast<char*>(current->iov_base) + control_hdr_len, current->iov_len);
            current = *static_cast<iovec**>(current->iov_base);
        }
        return str;
    }

    /**
     * @brief Specialization: converts the buffer contents into a @c std::vector<std::byte>.
     * @return Byte vector holding the payloads of all chunks.
     */
    template <>
    inline std::vector<std::byte> ace::io::buffer::as<std::vector<std::byte>>() const {
        std::vector<std::byte> buf;
        const iovec* current = _chunk_list_begin;
        for (size_t i =0; i < _hdr.msg_iovlen and current not_eq nullptr; ++i) {
            for (size_t j = 0; j < current->iov_len; ++j)
                buf.push_back(
                    std::forward<std::byte>(
                        static_cast<std::byte*>(current->iov_base)[j + control_hdr_len]
                    )
                );
            current = *static_cast<iovec**>(current->iov_base);
        }
        return buf;
    }

#endif //ACE_IO_H
