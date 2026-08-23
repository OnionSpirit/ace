#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "environment.h"

#include <ace/console.h>
#include <ace/io.h>
#include <ace/net.h>

namespace {

struct socket_echo_fixture : base_fixture {
    void TearDown() override {
        ace::reset_signal();
    }

    ace::task socket_listener() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _server_port);
        if (not selection_entry) { co_return; }
        auto listener = co_await selection_entry.listen();
        if (not listener) { co_return; }
        const auto connection = co_await listener.accept("127.0.0.1", _client_port);
        if (not connection) { co_return; }
        for (int i = 0; i < 5; ++i) {
            if (auto result = co_await connection.recv_buf())
                ace::println("Server received: '{}'", result.value().as<std::string>());
            else
                ace::println("Server failed: '{}'", strerror(result.error()));
        }
        co_return;
    }

    ace::task socket_abuser() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _client_port);
        if (not selection_entry) { co_return; }
        const auto connection =
            co_await selection_entry.connect("127.0.0.1", _server_port);
        if (not connection) { co_return; }
        for (int i = 1; i < 6; ++i) {
            std::string msg = "Echo message " + std::to_string(i);
            if (co_await connection.send(msg))
                ace::println("Client sent: '{}'", msg);
        }
        co_return;
    }

    ace::task socket_listener_zc() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _zc_server_port);
        if (not selection_entry) { co_return; }
        auto listener = co_await selection_entry.listen();
        if (not listener) { co_return; }
        const auto connection =
            co_await listener.accept("127.0.0.1", _zc_client_port);
        if (not connection) { co_return; }
        for (int i = 0; i < 5; ++i) {
            if (auto buf = co_await connection.recv_buf())
                ace::println("Server [zc] received: '{}'", buf.value());
            else
                ace::println("Server [zc] failed. error: {}", strerror(buf.error()));
        }
        co_return;
    }

    ace::task socket_abuser_zc() {
        auto bind_entry = co_await ace::net::socket_tcp();
        if (not bind_entry) { co_return; }
        auto selection_entry = co_await bind_entry.bind("127.0.0.1", _zc_client_port);
        if (not selection_entry) { co_return; }
        const auto connection =
            co_await selection_entry.connect("127.0.0.1", _zc_server_port);
        if (not connection) { co_return; }
        for (int i = 1; i < 6; ++i) {
            ace::io::buffer buf;
            buf.append("Echo message {}", i);
            if (co_await connection.send(buf) == EXIT_SUCCESS)
                ace::println("Client [zc] sent: '{}'", buf);
        }
        co_return;
    }

    static constexpr uint16_t _server_port = 8000;
    static constexpr uint16_t _client_port = 8001;
    static constexpr uint16_t _zc_server_port = 9000;
    static constexpr uint16_t _zc_client_port = 9001;
};

// Verifies a TCP client sends five messages that the paired listener receives.
TEST_F(socket_echo_fixture, do_io_socket_echo) {
    ace::schedule(socket_listener());
    ace::schedule(socket_abuser());
    ace::run();
    // A drained dispatcher proves both socket state machines reached completion.
    ASSERT_TRUE(ace::empty());
}

// Verifies the buffer-based send path completes a five-message TCP exchange.
TEST_F(socket_echo_fixture, do_io_socket_echo_zc) {
    ace::schedule(socket_listener_zc());
    ace::schedule(socket_abuser_zc());
    ace::run();
    ASSERT_TRUE(ace::empty());
}

} // namespace
