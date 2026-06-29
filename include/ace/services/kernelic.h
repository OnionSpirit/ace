/**
 * @file kernelic.h
 * @brief Thread-local @c io_uring controller vortex and observer interface.
 *
 * @details This header defines the integration layer between ACE and the
 * Linux @c io_uring subsystem:
 *
 *  - <b>@c kernel_observer </b>— polymorphic callback interface invoked when
 *    a completion queue entry (CQE) arrives.  Used as the @c user_data
 *    payload in SQE submissions.
 *  - <b>@c kernel_controller </b>— thread-local vortex that owns the
 *    @c io_uring ring.  Its @c ping() method dequeues and submits buffered
 *    SQEs, then processes incoming CQEs by calling @c on_result() on the
 *    associated observer.
 *  - <b>@c kernel_entity </b> — deferred SQE storage.  When the submission
 *    queue is full (4096 entries), new operations are buffered in a
 *    thread-local queue and submitted on the next @c ping().
 *
 * ### How it fits into ACE
 *
 * @c kernel_controller is a vortex (background polling service).  The
 * dispatcher calls @c yank_vortex() periodically, which invokes @c ping().
 * @c ping() submits pending SQEs, drains CQEs, and notifies waiting
 * coroutines via @c kernel_observer::on_result().
 *
 * @see ace::io::query, ace::io::entity, ace::core::traits::vortex_traits
 */
#ifndef ACE_SERVICES_KERNELIC_H
#define ACE_SERVICES_KERNELIC_H

#include <algorithm>
#include <cstring>
#include <liburing.h>

#include "ace/core/traits/vortex.h"
#include "ace/core/tools/queue.h"
#include "ace/core/tools/iovec_alloc.h"

namespace ace::services {

    /**
     * @brief Polymorphic completion handler for @c io_uring operations.
     *
     * @details An instance of @c kernel_observer is passed as @c user_data
     * in each SQE.  When the CQE arrives, @c kernel_controller::ping() calls
     * @c on_result(res) which typically stores the result and re-attaches
     * the waiting coroutine.
     *
     * Derived types include @c io_query (coroutine awaitable) and
     * @c io_hanged::command (fire-and-forget).
     */
    struct kernel_observer {

        kernel_observer() = default;

        explicit kernel_observer(runner_pool_t* identity)
            : _runner_identity(identity) {}

        /**
         * @brief Activates observer with CQE result
         * @param res CQE result value
         */
        virtual void on_result(int res) = 0;

        runner_pool_t* _runner_identity = nullptr;
        bool _on_cancel = false; ///< Next response will indicate count of canceled operations
        bool _multishot = false; ///< Mark if multishot is enabled

        virtual ~kernel_observer() = default;
    };

    /**
     * @brief Thread-local @c io_uring controller vortex.
     *
     * @details Each runner thread gets its own @c kernel_controller instance
     * (via @c vortex_traits with @c e_thread_local).  It:
     *  1. Initialises the @c io_uring ring on construction.
     *  2. Exposes static convenience methods (@c read(), @c write(),
     *     @c send(), @c accept(), etc.) that wrap @c submit().
     *  3. Runs @c ping() from the vortex polling loop — submits buffered
     *     SQEs, drains CQEs, and calls @c observer->on_result() for each
     *     completion.
     *
     * The ring supports up to 4096 concurrent operations; overflow is
     * buffered in @c _submission_buffer (a queue of @c kernel_entity).
     */
    struct kernel_controller : core::traits::vortex_traits<kernel_controller, core::vortex_spawn_mode::e_thread_local> {

    private:

        struct kernel_entity;

        static thread_local io_uring_params _ring_params;
        static thread_local io_uring _ring;
        static thread_local int _queries;
        static thread_local bool _need_submission;

    public:

        kernel_controller();

        ~kernel_controller();

        static constexpr unsigned max_entries = 4096;

        static thread_local core::tools::queue<kernel_entity> _submission_buffer;
        static thread_local core::tools::iovec_allocator _iovec_alloc;

        static bool ping();

        static auto create_observer() noexcept;

        /**
         * @brief Submits IO request to controller.
         * @param [in] io_uring_foo IO function ptr.
         * @param [in] observer pointer to an external object that waits
         * for the operation result of the requested operation.
         * @param [in, out] params IO function params (without sqe).
         * @warning IO function params shall be passed without SQE ptr.
         *
         * Same order but observer ptr required instead of the SQE ptr:
         *
         * @c io_uring_prep_open(sqe, path, flags, mode) - @b liburing @b interface.
         *
         * @c submit(io_uring_prep_open, observer, path, flags, mode) - @b submit @b interface.
         */
        template <typename foo_t, typename ... Params>
        static bool submit(foo_t io_uring_foo, kernel_observer* observer, Params... params) noexcept;

        /**
         * @brief Registers a set of file descriptors with io_uring for fixed-file
         * operations (IORING_REGISTER_FILES). Once registered, FDs can be
         * referenced by their fixed index (iosqe_fixed_file) bypassing the
         * per-syscall FD table lookup.
         * @param fds array of file descriptors
         * @param nr_fds number of FDs
         * @return 0 on success, negative errno on failure
         */
        static int register_files(const int* fds, unsigned nr_fds) noexcept {
            return io_uring_register_files(&_ring, fds, nr_fds);
        }

        /**
         * @brief Updates a single file descriptor in the registered files set.
         * @param index index in the registered files array
         * @param fd new file descriptor value
         * @return 0 on success, negative errno on failure
         */
        static int register_files_update(unsigned index, int fd) noexcept {
            return io_uring_register_files_update(&_ring, index, &fd, 1);
        }

        /**
         * @brief Unregisters previously registered file descriptors.
         * @return 0 on success, negative errno on failure
         */
        static int unregister_files() noexcept {
            return io_uring_unregister_files(&_ring);
        }

        static bool nop(kernel_observer* observer) {
            return submit(io_uring_prep_nop, observer);
        }

        static bool socket(kernel_observer* observer, const int domain, const int type,
            const int protocol, const unsigned int flags) {
            return submit(io_uring_prep_socket, observer, domain, type, protocol, flags);
        }

        static bool cancel(kernel_observer* observer, const int flags) {
            return observer->_on_cancel = submit(io_uring_prep_cancel, observer, observer, flags);
        }

        static bool cancel_fd(kernel_observer* observer, const int fd, const int flags) {
            return observer->_on_cancel = submit(io_uring_prep_cancel_fd, observer, fd, flags);
        }

        static bool open(kernel_observer* observer, const char* path, const int flags, const mode_t mode) {
            return submit(io_uring_prep_open, observer, path, flags, mode);
        }

        static bool close(kernel_observer* observer, const int fd) noexcept {
            return submit(io_uring_prep_close, observer, fd);
        }

        static bool bind(kernel_observer* observer, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_bind, observer, fd, addr, addrlen);
        }

        static bool connect(kernel_observer* observer, const int fd, const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_connect, observer, fd, addr, addrlen);
        }

        static bool listen(kernel_observer* observer, const int fd, const int backlog) {
            return submit(io_uring_prep_listen, observer, fd, backlog);
        }

        static bool accept(kernel_observer* observer, const int fd, sockaddr *addr, socklen_t *addrlen, const int flags) {
            return submit(io_uring_prep_accept, observer, fd, addr, addrlen, flags);
        }

        static bool send(kernel_observer* observer, const int fd, const void *buf, const size_t len, const int flags) {
            return submit(io_uring_prep_send, observer, fd, buf, len, flags);
        }

        static bool send_zc(kernel_observer* observer, const int fd, const void *buf, const size_t len,
                            const int flags, const unsigned int zc_flags) {
            return submit(io_uring_prep_send_zc, observer, fd, buf, len, flags, zc_flags);
        }

        static bool send_zc_fixed(kernel_observer* observer, const int fd, const void *buf, const size_t len,
                            const int flags, const unsigned int zc_flags, const unsigned buf_index) {
            return submit(io_uring_prep_send_zc_fixed, observer, fd, buf, len, flags, zc_flags, buf_index);
        }

        static bool sendto(kernel_observer* observer, const int fd, const void *buf, const size_t len, const int flags,
            const sockaddr *addr, const socklen_t addrlen) {
            return submit(io_uring_prep_sendto, observer, fd, buf, len, flags, addr, addrlen);
        }

        /**
         * @brief Scatter-gather send via io_uring_prep_sendmsg.
         * Allows sending data from multiple non-contiguous buffers (iovec)
         * in a single syscall, avoiding extra copy for composite headers.
         * For zero-copy with pre-registered buffers, use sendmsg_fixed().
         * @param observer CQE completion handler
         * @param fd socket file descriptor
         * @param msg pointer to msghdr with iovec array
         * @param flags send flags (MSG_ZEROCOPY etc.)
         */
        static bool sendmsg(kernel_observer* observer, const int fd, const msghdr* msg, const int flags) {
            return submit(io_uring_prep_sendmsg_zc, observer, fd, msg, flags);
        }

        [[deprecated("Not supported in liburing yet, may occur shitty bugs")]]
        static bool sendmsg(kernel_observer* observer, const int fd, const msghdr* msg, const int flags, const unsigned buf_index) {
            return submit(io_uring_prep_sendmsg_zc_fixed, observer, fd, msg, flags, buf_index);
        }

        /**
         * @brief Scatter-gather recv via io_uring_prep_recvmsg.
         */
        static bool recvmsg(kernel_observer* observer, const int fd, msghdr* msg, const int flags) {
            return submit(io_uring_prep_recvmsg, observer, fd, msg, flags);
        }

        static bool recv(kernel_observer* observer, const int fd, void *buf, const size_t len, const int flags) {
            return submit(io_uring_prep_recv, observer, fd, buf, len, flags);
        }

        static bool read(kernel_observer* observer, const int fd, void *buf, const unsigned nbytes, const uint64_t offset) {
            return submit(io_uring_prep_read, observer, fd, buf, nbytes, offset);
        }

        static bool write(kernel_observer* observer, const int fd, const void *buf, const unsigned nbytes, const uint64_t offset) {
            return submit(io_uring_prep_write, observer, fd, buf, nbytes, offset);
        }

        static bool writev(kernel_observer* observer, const int fd, const iovec *vec, const unsigned len, const uint64_t offset, const int flags) {
            return submit(io_uring_prep_writev2, observer, fd, vec, len, offset, flags);
        }

        // ── iovec allocator ───────────────────────────────────────────

        static auto iovec_allocate(size_t size) noexcept -> iovec* {
            return _iovec_alloc.allocate(size);
        }

        static auto iovec_deallocate(iovec* iov) noexcept -> void {
            _iovec_alloc.deallocate(iov);
        }

        static auto iovec_pool_allocate(size_t len) noexcept -> iovec* {
            return _iovec_alloc.allocate_as<iovec>(len);
        }

        static auto iovec_pool_deallocate(iovec* iov, size_t len) noexcept -> void {
            _iovec_alloc.deallocate_as(iov, sizeof(iovec) * len);
        }

        static auto iovec_alloc() noexcept -> core::tools::iovec_allocator& { return _iovec_alloc; }

    };

    /**
     * @brief Deferred SQE — stores an @c io_uring operation for later submission.
     *
     * @details When the submission queue is full (4096 entries), new operations
     * are buffered as @c kernel_entity objects in the thread-local
     * @c _submission_buffer.  Each entity captures the function pointer, SQE,
     * and up to 8 parameters.  @c apply() reconstructs the original call.
     */
    struct kernel_controller::kernel_entity {

        template <typename io_uring_foo_t, typename ... Args>
        kernel_entity(io_uring_foo_t foo, io_uring_sqe *sqe, Args... args);

        // NOTE: Polymorphic action handler
        template <typename io_uring_foo_t, typename ... Args>
        static void action_templ(void* io_uring_foo, io_uring_sqe* sqe, const uintptr_t* params);

        void apply();

        ACE_CACHE_LINE(0)

        uintptr_t _params[8] = {};

        ACE_CACHE_LINE(1)

        void (*_action)(void*, io_uring_sqe*, const uintptr_t*) = nullptr;
        io_uring_sqe* _sqe = nullptr;
        void* _io_uring_foo = nullptr;

        static thread_local core::tools::slab_mempool<kernel_entity> _kernelic_entity_mempool;
    };

    inline thread_local core::tools::slab_mempool<kernel_controller::kernel_entity> kernel_controller::kernel_entity::_kernelic_entity_mempool {
        core::tools::slab_mempool<kernel_entity>()
    };

    inline thread_local core::tools::queue<kernel_controller::kernel_entity> kernel_controller::_submission_buffer {
        kernel_entity::_kernelic_entity_mempool
    };

    inline thread_local core::tools::iovec_allocator kernel_controller::_iovec_alloc {};

    inline thread_local io_uring_params kernel_controller::_ring_params {};
    inline thread_local io_uring kernel_controller::_ring {};

    inline thread_local int kernel_controller::_queries {};
    inline thread_local bool kernel_controller::_need_submission {false};

}

#define ACE_SERVICES_KERNEL_CONTROLLER_SPACE \
ace::services::kernel_controller::

#define ACE_SERVICES_KERNEL_CONTROLLER_MEMBER(returnT) \
inline returnT ACE_SERVICES_KERNEL_CONTROLLER_SPACE

#define ACE_SERVICES_KERNEL_ENTITY_SPACE \
ace::services::kernel_controller::kernel_entity::

#define ACE_SERVICES_KERNEL_ENTITY_MEMBER(returnT) \
inline returnT ACE_SERVICES_KERNEL_ENTITY_SPACE


ACE_SERVICES_KERNEL_CONTROLLER_MEMBER()
kernel_controller() {
    memset(&_ring_params, 0, sizeof(_ring_params));
    io_uring_queue_init_params(max_entries, &_ring, &_ring_params);
}

ACE_SERVICES_KERNEL_CONTROLLER_MEMBER()
~kernel_controller() {
    io_uring_queue_exit(&_ring);
}


ACE_SERVICES_KERNEL_CONTROLLER_MEMBER(bool)
ping() {
    // NOTE: Setting requests to the io_uring
    _need_submission = _need_submission or not _submission_buffer.empty();
    for (unsigned int i = 0; i < (max_entries - _queries) and not _submission_buffer.empty(); ++i) {
        auto entity = _submission_buffer.dequeue();
        entity.apply();
    }

    // NOTE: Requesting submission if it's needed
    if (std::exchange(_need_submission, false))
        io_uring_submit(&_ring);

    // NOTE: Receiving responses from the io_uring
    io_uring_cqe* cqe_s[max_entries] {};
    const unsigned int cqe_count = io_uring_peek_batch_cqe(&_ring, cqe_s, max_entries);
    for (unsigned int i = 0; i < cqe_count; ++i) {

        const auto cqe = cqe_s[i];
        const auto identity = io_uring_cqe_get_data(cqe);
        const auto observer = static_cast<kernel_observer*>(identity);

        if (observer == nullptr) {
            --_queries;
            continue;
        }

        // NOTE: CQE flow operations tracking
        const bool cqe_flow = IORING_CQE_F_MORE & cqe->flags;
        const bool multishot_active = cqe_flow and observer->_multishot;

        if (cqe_flow)
            ++_queries;
        else {
            observer->on_result(cqe->res);
        }

        // NOTE: Awaking observer on multishot CQE
        if (multishot_active)
            observer->on_result(cqe->res);

        if (multishot_active and observer->_on_cancel) {
            _queries -= cqe->res;
        } else
            --_queries;

        // if (cqe_flow) ++_queries;
        //
        // if (multishot_active and observer->_on_cancel) {
        //     _queries -= cqe->res;
        // } else if (multishot_active or not cqe_flow) {
        //     observer->on_result(cqe->res);
        //     --_queries;
        // }

        io_uring_cqe_seen(&_ring, cqe);
    }

    return _queries not_eq 0;
}


template <typename foo_t, typename ... Params> bool
ACE_SERVICES_KERNEL_CONTROLLER_SPACE
submit(foo_t io_uring_foo, kernel_observer* observer, Params... params) noexcept {
    if (not observer->_runner_identity)
        observer->_runner_identity = core::runner::get().as<runner_pool_t>();
    touch(observer->_runner_identity);
    io_uring_sqe *sqe = io_uring_get_sqe(&_ring);
    io_uring_sqe_set_data(sqe, observer);
    ++_queries;
    if (_queries < 4096) {
        io_uring_foo(sqe, params...);
        _need_submission = true;
    }
    else if (not _submission_buffer.enqueue( kernel_entity{io_uring_foo, sqe, params...} )) [[unlikely]]
        return false;
    return true;
}


template <typename io_uring_foo_t, typename ... Args>
ACE_SERVICES_KERNEL_ENTITY_SPACE
kernel_entity(io_uring_foo_t foo, io_uring_sqe *sqe, Args... args) {
    _action = action_templ<io_uring_foo_t, Args...>;
    _sqe = sqe;
    _io_uring_foo = reinterpret_cast<void*>(foo);
    // NOTE: Placement new to copy params to the local storage
    new (_params) std::tuple<Args...>(args...);
}


template <typename io_uring_foo_t, typename ... Args> void
ACE_SERVICES_KERNEL_ENTITY_SPACE
action_templ(void* io_uring_foo, io_uring_sqe* sqe, const uintptr_t* params) {
    std::tuple<Args...> tuple { *reinterpret_cast<const std::tuple<Args...>*>(params) };
    io_uring_foo_t foo { reinterpret_cast<io_uring_foo_t>(io_uring_foo) };
    [&]<std::size_t ... index_v>(std::index_sequence<index_v...>) {
        foo(sqe, std::get<index_v>(tuple)...);
    }(std::make_index_sequence<sizeof...(Args)>{});
}


ACE_SERVICES_KERNEL_ENTITY_MEMBER(void)
apply() {
    if (_sqe not_eq nullptr)
        _action(_io_uring_foo, _sqe, _params);
}

#undef ACE_SERVICES_KERNEL_CONTROLLER_SPACE
#undef ACE_SERVICES_KERNEL_CONTROLLER_MEMBER
#undef ACE_SERVICES_KERNEL_ENTITY_SPACE
#undef ACE_SERVICES_KERNEL_ENTITY_MEMBER
#endif //ACE_SERVICES_KERNELIC_H
