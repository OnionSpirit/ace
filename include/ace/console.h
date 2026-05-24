#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <list>
#include <ace/core/async.h>
#include <ace/core/io.h>
#include <ace/futures/get_runner.h>

// NOTE: It is needed to use external fmt lib with older standards which does not support std::format
#ifndef __FMT__
#define __FMT__ std
#endif

namespace ace {

    /**
     * @warning Don't take it serious. For the most part this is a dumb joke.
     * 0, 1, 2, 3, 4... Guinnesses and then things went wrong.
     * Actually I've just wanned an abstraction for the async input.
     * I've improved this thing up just to debug @c vortex mechanism and @c kernelic module.
     * @c console module helped me to reveal a bunch of shitty bugs.
     * That is why console has a set of async print functions.
     * There are no blazing features just printing via @c io_uring
     */
    class console {

        console() = default;

        struct lazy_print_observer : core::services::kernel_observer {

            std::vector<uint8_t> _buffer{};

            void on_result(const int res) override {
                _slot_pool.raw_sync(this);
            }

            ~lazy_print_observer() override = default;
        };

        static constexpr int buff_len = 256;

        static std::atomic<std::FILE*> _output;

        static thread_local nukes::dynamic::reg_freelist<lazy_print_observer> _slot_pool;

        static auto get_instance() {
            thread_local console instance {};
            return instance;
        }

        static void print_dispatch(const std::FILE* file, const __FMT__::string_view buff) {
            // NOTE: Trying to get thread local runner from the dispatcher
            auto* runner_identity = reinterpret_cast<runner_pool_t*>(ace::core::dispatcher::get_local_runner());
            // NOTE: If can not get slot or identity not found -> using busy branch
            if (lazy_print_observer* slot; not _slot_pool.capture(slot) or not runner_identity) [[unlikely]] {
                if (write(file->_fileno, buff.data(), buff.size()) < 0)
                    throw std::runtime_error(std::string("print failed: ") + strerror(errno));
            }
            // NOTE: Pushing data to slot, and setting identity for kernelic
            else [[likely]] {
                slot->_runner_identity = runner_identity;
                slot->_buffer.assign(buff.begin(), buff.end());
                if (not core::services::kernel_controller::write(slot, file->_fileno,
                    slot->_buffer.data(), slot->_buffer.size(), 0))
                    throw std::runtime_error("print failed");
            }
        }

        template <class... Args>
        static void print_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
            print_dispatch(file, buff);
        }

        template <class... Args>
        static void println_impl(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::string buff = __FMT__::format(std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...) + '\n';
            print_dispatch(file, buff);
        }

        static void print_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            const auto buff = std::string(std::forward<const __FMT__::string_view>(str));
            print_dispatch(file, buff);
        }

        static void println_impl(const std::FILE* file, const __FMT__::string_view&& str) {
            const std::string buff = std::string(std::forward<const __FMT__::string_view>(str)) + '\n';
            print_dispatch(file, buff);
        }

    public:

        [[nodiscard]] static promise<std::expected<std::string, int>> input() {

            std::deque<std::array<char, buff_len>> acc {};
            int total = 0;

            auto& buff = acc.emplace_back();
            int bytes_read = co_await core::read_query(STDIN_FILENO, buff.data(), buff_len);
            if (bytes_read < 0) co_return std::unexpected(-bytes_read);
            total += bytes_read;

            while (bytes_read == buff_len) {
                buff = acc.emplace_back();
                bytes_read = co_await core::read_query(STDIN_FILENO, buff.data(), buff_len);
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

        template <class... Args>
        static auto println(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto println(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return println_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto println(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println(const std::FILE* file, const __FMT__::string_view&& str) {
            return println_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto println() {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return println_impl(file, "");
        }

        static auto println(const std::FILE* file) {
            return println_impl(file, "");
        }

        template <class... Args>
        static auto print(__FMT__::format_string<Args...>&& fmt, Args&&... args) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        template <class... Args>
        static auto print(const std::FILE* file, __FMT__::format_string<Args...>&& fmt, Args&&... args) {
            return print_impl(file, std::forward<__FMT__::format_string<Args...>>(fmt), std::forward<Args>(args)...);
        }

        static auto print(const __FMT__::string_view&& str) {
            const std::FILE* file = _output.load(std::memory_order_acquire);
            return print_impl(file, std::forward<const __FMT__::string_view>(str));
        }

        static auto print(const std::FILE* file, const __FMT__::string_view&& str) {
            return print_impl(file, std::forward<const __FMT__::string_view>(str));
        }

    };

    std::atomic<std::FILE*> console::_output = stdout;

    thread_local nukes::dynamic::reg_freelist<console::lazy_print_observer> console::_slot_pool {};

}

#undef std

#endif //ACE_CONSOLE_H
