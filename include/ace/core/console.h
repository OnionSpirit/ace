#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <ace/core/context.h>
#include <ace/core/io.h>


namespace ace {

    class console {

        console() = default;

    protected:

        static constexpr int buff_len = 256;

        static auto get_instance() {
            static console instance {};
            return instance;
        }

        struct abandoned_write_query : core::io_query<abandoned_write_query> {

            abandoned_write_query() = delete;

            explicit abandoned_write_query(const int fd, const void *buf, const unsigned nbytes, const uint64_t offset = 0)
                : io_query(fd)
                , _fd(fd)
                , _buf(buf)
                , _nbytes(nbytes)
                , _offset(offset) {
                // _abandoned = true;
            }

            bool setup_query(kernel_observer* kwp) const {
                return core::modules::kernel_controller::write(kwp, _fd, _buf, _nbytes, _offset);
            }

            [[nodiscard]] int await_resume() const { return _res; }

            const int _fd;
            const void *_buf;
            const unsigned _nbytes;
            const uint64_t _offset;
        };

        template<size_t N>
        struct constexpr_string {
            std::array<char, N> data;

            explicit constexpr constexpr_string(const char (&str)[N]) {
                std::copy_n(str, N, data.begin());
            }

            [[nodiscard]] constexpr size_t size() const { return N; }
            [[nodiscard]] constexpr const char* c_str() const { return data.data(); }
        };

        template<size_t N>
        static constexpr auto newline_append(const constexpr_string<N>& fmt) {
            std::array<char, N + 1> result;
            std::copy_n(fmt.data.data(), N - 1, result.begin());
            result[N - 1] = '\n';
            result[N] = '\0';
            return result;
        };

    public:

        [[nodiscard]] static async<std::string> input() {
            std::stringstream ss;
            char buff[buff_len] = {};
            int bytes_read = co_await core::read_query(STDIN_FILENO, buff, buff_len);
            ss << buff;
            while (bytes_read == buff_len) {
                bzero(buff, buff_len);
                bytes_read = co_await core::read_query(STDIN_FILENO, buff, buff_len);
                ss << buff;
            }
            co_return ss.str();
        }

        // TODO: figure out if that vfromat way better
        // template <class... Args>
        // ACE_AWAIT_NODISCARD
        // static async<void> println(std::string_view fmt, Args&&... args) {
        //     const std::string output = std::vformat(fmt, std::make_format_args(args...)) + "\n";
        //     const int res = co_await abandoned_write_query(stdout->_fileno, output.data(), output.size());
        //     if (res < 0)
        //         throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        // }

        template <class... Args>
        static promise<void> println(std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...) + "\n";
            const int res = co_await abandoned_write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        // TODO: Figure out constexpr concat
        // template<typename... Args, size_t N>
        // static promise<void> println(const constexpr_string<N>& c_fmt, Args&&... args) {
        //     std::format_string<Args...> fmt = newline_append(c_fmt).data();
        //     const std::string output = std::format(fmt, std::forward<Args>(args)...);
        //     const int res = co_await abandoned_write_query(STDOUT_FILENO, output.data(), output.size());
        //     if (res < 0)
        //         throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        // }

        template <class... Args>
        static promise<void> println(std::FILE* file, std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...) + "\n";
            const int res = co_await abandoned_write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println(std::string str) {
            str += '\n';
            const int res = co_await abandoned_write_query(STDOUT_FILENO, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        static promise<void> println(const std::FILE* file, std::string str) {
            str += '\n';
            const int res = co_await abandoned_write_query(file->_fileno, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println() {
            const std::string output {'\n'};
            const int res = co_await abandoned_write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> println(const std::FILE* file) {
            const std::string output {'\n'};
            const int res = co_await abandoned_write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        static promise<void> print(std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...);
            const int res = co_await abandoned_write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        static promise<void> print(std::FILE* file, std::format_string<Args...> fmt, Args&&... args) {
            const std::string output = std::format(fmt, std::forward<Args>(args)...);
            const int res = co_await abandoned_write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        static promise<void> print(const std::string_view str) {
            const int res = co_await abandoned_write_query(STDOUT_FILENO, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        static promise<void> print(const std::FILE* file, const std::string_view str) {
            const int res = co_await abandoned_write_query(file->_fileno, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

    };

}

#undef std

#endif //ACE_CONSOLE_H
