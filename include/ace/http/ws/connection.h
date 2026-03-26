#ifndef ACE_HTTP_WS_CONNECTION_H
#define ACE_HTTP_WS_CONNECTION_H

#include "frame.h"
#include "ace/futures/network.h"
#include "ace/coroutines/context.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ace::http::ws {

struct message {
    enum class type { text, binary, ping, pong, close };
    type     kind     = type::text;
    std::string payload;
    uint16_t close_code = 1000;

    [[nodiscard]] bool is_close()  const noexcept { return kind == type::close; }
    [[nodiscard]] bool is_text()   const noexcept { return kind == type::text;  }
    [[nodiscard]] bool is_binary() const noexcept { return kind == type::binary; }
    [[nodiscard]] bool is_ping()   const noexcept { return kind == type::ping;  }
};

/**
 * @brief Async WebSocket connection (server-side).
 *
 * Wraps an io_connection_entity after the HTTP upgrade handshake.
 * recv() reads one complete WS message (including fragmented frames).
 * send*() write unmasked server→client frames (RFC 6455 §5.1).
 *
 * Maximum incoming payload size: 16 MiB. Larger frames are rejected
 * with close code 1009 (Message Too Big).
 */
class connection {
    static constexpr uint64_t MAX_PAYLOAD = 16u * 1024u * 1024u;

    ace::futures::io_connection_entity& _conn;
    std::vector<uint8_t> _rbuf;

    // Reads exactly `n` bytes into _rbuf[offset .. offset+n).
    ace::async<bool> recv_exact(std::size_t offset, std::size_t n) {
        _rbuf.resize(offset + n);
        std::size_t got = 0;
        while (got < n) {
            int r = co_await _conn.recv(_rbuf.data() + offset + got, n - got);
            if (r <= 0) co_return false;
            got += static_cast<std::size_t>(r);
        }
        co_return true;
    }

public:
    explicit connection(ace::futures::io_connection_entity& conn) : _conn(conn) {}

    /**
     * @brief Receive one complete WS message.
     * Returns a default-constructed message (kind=text, empty payload) on error.
     */
    ace::async<message> recv() {
        _rbuf.clear();

        // Step 1: 2-byte base header
        if (!co_await recv_exact(0, 2)) co_return {};

        const uint8_t len7  = _rbuf[1] & 0x7Fu;
        const bool    masked = (_rbuf[1] & 0x80u) != 0;

        // Step 2: extended length + mask key
        std::size_t extra = 0;
        if      (len7 == 126) extra = 2;
        else if (len7 == 127) extra = 8;
        if (masked) extra += 4;

        if (extra > 0 && !co_await recv_exact(2, extra)) co_return {};

        // Step 3: parse complete header
        frame_header hdr{};
        if (!parse_frame_header(_rbuf.data(), _rbuf.size(), hdr)) co_return {};

        // Reject oversized frames
        if (hdr.payload_len > MAX_PAYLOAD) {
            co_await close(1009);
            co_return message{ .kind = message::type::close, .close_code = 1009 };
        }

        // Step 4: read payload
        if (hdr.payload_len > 0 && !co_await recv_exact(hdr.header_size, hdr.payload_len))
            co_return {};

        // Step 5: unmask
        uint8_t* payload_ptr = _rbuf.data() + hdr.header_size;
        if (hdr.masked) unmask(payload_ptr, hdr.payload_len, hdr.mask);

        std::string payload(reinterpret_cast<char*>(payload_ptr),
                            static_cast<std::size_t>(hdr.payload_len));

        switch (hdr.op) {
            case opcode::text:
                co_return message{ .kind = message::type::text,   .payload = std::move(payload) };
            case opcode::binary:
                co_return message{ .kind = message::type::binary, .payload = std::move(payload) };
            case opcode::ping:
                co_return message{ .kind = message::type::ping,   .payload = std::move(payload) };
            case opcode::pong:
                co_return message{ .kind = message::type::pong,   .payload = std::move(payload) };
            case opcode::close: {
                uint16_t code = 1000;
                if (payload.size() >= 2)
                    code = static_cast<uint16_t>(
                        (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]));
                co_return message{ .kind = message::type::close, .close_code = code };
            }
            default:
                co_return message{ .kind = message::type::text, .payload = std::move(payload) };
        }
    }

    ace::async<int> send(std::string_view text) {
        auto frame = encode_frame(opcode::text, text);
        co_return co_await _conn.send(frame.data(), frame.size());
    }

    ace::async<int> send_binary(std::span<const std::byte> data) {
        auto sv = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
        auto frame = encode_frame(opcode::binary, sv);
        co_return co_await _conn.send(frame.data(), frame.size());
    }

    ace::async<int> ping(std::string_view payload = {}) {
        auto frame = encode_frame(opcode::ping, payload);
        co_return co_await _conn.send(frame.data(), frame.size());
    }

    ace::async<int> close(uint16_t code = 1000, std::string_view reason = {}) {
        std::string payload;
        payload.push_back(static_cast<char>(code >> 8));
        payload.push_back(static_cast<char>(code & 0xFFu));
        payload.append(reason);
        auto frame = encode_frame(opcode::close, payload);
        co_return co_await _conn.send(frame.data(), frame.size());
    }
};

} // namespace ace::http::ws

#endif // ACE_HTTP_WS_CONNECTION_H
