#ifndef ACE_CONSOLE_H
#define ACE_CONSOLE_H

#include <ace/core/context.h>
#include <ace/core/io.h>

// TODO: Add selection between fmt and std C++26 at build system level
// #include <fmt/format.h>
#define FORMAT std

namespace ace {

    class console {

        console() = default;

    protected:

        static constexpr int buff_len = 256;

        static auto get_instance() {
            static console instance {};
            return instance;
        }

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
        // [[nodiscard]] static async<void> println(FORMAT::string_view fmt, Args&&... args) {
        //     const std::string output = FORMAT::vformat(fmt, FORMAT::make_format_args(args...)) + "\n";
        //     const int res = co_await core::write_query(stdout->_fileno, output.data(), output.size());
        //     if (res < 0)
        //         throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        // }

        template <class... Args>
        [[nodiscard]] static async<void> println(FORMAT::format_string<Args...> fmt, Args&&... args) {
            const std::string output = FORMAT::format(fmt, std::forward<Args>(args)...) + "\n";
            const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        [[nodiscard]] static async<void> println(std::FILE* file, FORMAT::format_string<Args...> fmt, Args&&... args) {
            const std::string output = FORMAT::format(fmt, std::forward<Args>(args)...) + "\n";
            const int res = co_await core::write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        [[nodiscard]] static async<void> println(std::string str) {
            str += '\n';
            const int res = co_await core::write_query(STDOUT_FILENO, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        [[nodiscard]] static async<void> println(const std::FILE* file, std::string str) {
            str += '\n';
            const int res = co_await core::write_query(file->_fileno, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        [[nodiscard]] static async<void> println() {
            const std::string output {'\n'};
            const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        [[nodiscard]] static async<void> println(const std::FILE* file) {
            const std::string output {'\n'};
            const int res = co_await core::write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        [[nodiscard]] static async<void> print(FORMAT::format_string<Args...> fmt, Args&&... args) {
            const std::string output = FORMAT::format(fmt, std::forward<Args>(args)...);
            const int res = co_await core::write_query(STDOUT_FILENO, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        template <class... Args>
        [[nodiscard]] static async<void> print(std::FILE* file, FORMAT::format_string<Args...> fmt, Args&&... args) {
            const std::string output = FORMAT::format(fmt, std::forward<Args>(args)...);
            const int res = co_await core::write_query(file->_fileno, output.data(), output.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

        [[nodiscard]] static async<void> print(const std::string_view str) {
            const int res = co_await core::write_query(STDOUT_FILENO, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
            co_return;
        }

        [[nodiscard]] static async<void> print(const std::FILE* file, const std::string_view str) {
            const int res = co_await core::write_query(file->_fileno, str.data(), str.size());
            if (res < 0)
                throw std::runtime_error(std::string("stdout write failed: ") + strerror(-res));
        }

    };

}

#undef FORMAT

#endif //ACE_CONSOLE_H