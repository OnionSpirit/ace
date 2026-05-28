#ifndef ACE_IO_H
#define ACE_IO_H


#include <climits>
#include <utility>

#include "ace/core/services/kernelic.h"

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef FMT_SRC
#define FMT_SRC std
#endif

namespace ace::core {
    // NOTE: Concept to check if type is defined as io_query
    template <typename query_t>
    concept is_query = requires(query_t q, services::kernel_observer* kwp) {
        { q.setup_query(kwp) } -> std::same_as<bool>;
    };

    // NOTE: Concept to check if type is defined as io_entity
    template <typename entry_t>
    concept is_entity = requires(entry_t q) {
        { q._fd } -> std::same_as<int>;
        { q._is_closed } -> std::same_as<bool>;
    };

    /**
     * @brief An interface to interact with the
     * @c ace::core::services::kernel_controller via @c co_await operator.
     * @tparam query_core_t Specific query type.
     *
     * @warning Does not define @c await_resume(...) logic.
     */
    template <typename query_core_t>
    struct io_query;

    struct read_query;

    struct write_query;

    struct close_query;

    /**
     * @brief RAII io fd guard
     */
    struct io_guard;

    template <typename>
    struct io_caster {

        // NOTE: Defines how to create current entity from another entity
        static auto from_entity(const int, const bool, auto&&) {
            static_assert(false, "Can not cast from another <io_entity>");
        }

        // NOTE: Defines how to cast current to io_link derived type
        static auto as_link(const int, const bool, auto&&) {
            static_assert(false, "Can not cast to <io_link>");
        }
    };

    /**
     * @brief Handler for a file descriptor with RAII guard behavior.
     * The io_entity derived types shall represent FD state by providing allowed async operations depending on FD state.
     * @tparam entity_t Derived entity type
     */
    template <typename entity_t>
    struct io_entity;

    /**
     * @brief Encapsulated set of global entities for starting and handling hanged processing
     */
    struct io_hanged;

    class any;

    /**
     * @brief Common interface for io abstractions
     */
    class io_link;

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


    struct ace::core::io_hanged {

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
        //     throw std::runtime_error(FMT_SRC::format("io operation failed: {}\nuser data: {}", strerror(-res), user_data));
        // }

        static void basic_fail_handler(const int res) {
            throw std::runtime_error(FMT_SRC::format("io operation failed: {}", strerror(-res)));
        }

        // static void(*fail_cb_handler)(int, const std::span<char>&); ///< Fail handler for commands errors handling

        static void(*fail_cb_handler)(int); ///< Fail handler for commands errors handling

        static thread_local nukes::dynamic::reg_freelist<command> _command_pool; ///< Pool of command to start hanged processing wo @c co_await usage
    };

    thread_local nukes::dynamic::reg_freelist<ace::core::io_hanged::command> ace::core::io_hanged::_command_pool {};

    // inline void(*ace::core::io_hanged::fail_cb_handler)(int, const std::span<char>&) = basic_fail_handler;

    inline void(*ace::core::io_hanged::fail_cb_handler)(int) = basic_fail_handler;


    template <typename query_core_t>
    struct ace::core::io_query : traits::future_traits<query_core_t>, services::kernel_observer {

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

    struct ace::core::close_query : io_query<close_query> {

        IMPORT_IO_QUERY_ENV(close_query)

        close_query() = delete;

        explicit close_query(const int fd) : io_query_t(fd) {}

        bool setup_query(kernel_observer* kwp) const noexcept {
            return services::kernel_controller::close(kwp, _fd);
        }

        [[nodiscard]] int await_resume() const { return _res; }
    };


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

            // NOTE: Trying to get thread local runner from the dispatcher
            auto* runner_identity = reinterpret_cast<runner_pool_t*>(dispatcher::get_local_runner());
            // NOTE: If can not get slot or identity not found -> scheduling closing task
            if (io_hanged::command* cmd; not io_hanged::_command_pool.capture(cmd) or not runner_identity) [[unlikely]]
                schedule(pending_close(_fd));
            // NOTE: Setting identity for kernelic and run lazy
            else [[likely]] {
                cmd->_runner_identity = runner_identity;
                if (not services::kernel_controller::close(cmd, _fd) and io_hanged::fail_cb_handler)
                    io_hanged::fail_cb_handler(EAGAIN); // Maybe EIO?
            }
        }
    };


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
            auto [fd, is_closed] = std::move(io.extract());
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

        explicit io_link(const int fd)
            : _fd(fd)
            , _is_closed(false) { };

        // NOTE: This method is made to never forget to move ownership
        template<typename entity_t>
        static auto consume(entity_t& io) noexcept {
            auto [fd, is_closed] = std::move(io.extract());
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
        void writeln(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = FMT_SRC::format(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            output_action(buff);
        }

        void writeln(const FMT_SRC::string_view&& str) {
            const std::string buff = std::string(std::forward<const FMT_SRC::string_view>(str)) + '\n';
            output_action(buff);
        }

        template <class... Args>
        void write(FMT_SRC::format_string<Args...>&& fmt, Args&&... args) {
            std::span buff = FMT_SRC::format(std::forward<FMT_SRC::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            output_action(buff);
        }

        void write(const FMT_SRC::string_view&& str) {
            output_action(std::forward<const FMT_SRC::string_view>(str));
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

        ACE_AWAIT_NODISCARD promise<int> read(void *buf, const size_t len, const int flags = 0) {
            co_return co_await input_action(buf, len);
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD promise<int> read(std::vector<data_t>& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size() * (sizeof(data_t) / sizeof(char)));
        }

        ACE_AWAIT_NODISCARD promise<int> read(std::string& buf, const int flags = 0) {
            co_return co_await input_action(buf.data(), buf.size());
        }

        template <typename data_t>
        requires std::is_pod_v<data_t>
        [[nodiscard]] auto read_vec(const int flags = 0)
        -> promise<std::expected<std::vector<data_t>, int>> {
            static constexpr int buff_len_bytes = buff_len * (sizeof(data_t) / sizeof(char));

            std::deque<std::array<data_t, buff_len>> acc;
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await input_action(reinterpret_cast<void*>(buff.data()), buff_len_bytes);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await input_action(reinterpret_cast<void*>(buff.data()), buff_len_bytes);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
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
        -> promise<std::expected<std::string, int>> {

            std::deque<std::array<char, buff_len>> acc {};
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await input_action(buff.data(), buff_len);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await input_action(buff.data(), buff_len);
                if (bytes_read < 0) co_return std::unexpected(-bytes_read);
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
        ACE_AWAIT_NODISCARD promise<int> read(std::array<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), len_v * (sizeof(data_t) / sizeof(char)));
        }

        template <typename data_t, size_t len_v>
        requires std::is_pod_v<data_t>
        ACE_AWAIT_NODISCARD promise<int> read(std::span<data_t, len_v>& buf, const int flags = 0) {
            co_return co_await input_action(reinterpret_cast<void*>(buf.data()), buf.size_bytes());
        }


    protected:

        int         _fd;        ///< Socket file descriptor
        bool        _is_closed; ///< Socket closed flag
        any         _data;      ///< FD related params

    private:

        io_guard _guard {_fd, _is_closed};
    };

#endif //ACE_IO_H
