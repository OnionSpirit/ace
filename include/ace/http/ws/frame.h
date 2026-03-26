#ifndef ACE_HTTP_WS_FRAME_H
#define ACE_HTTP_WS_FRAME_H

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace ace::http::ws {

enum class opcode : uint8_t {
    continuation = 0x0,
    text         = 0x1,
    binary       = 0x2,
    close        = 0x8,
    ping         = 0x9,
    pong         = 0xA,
};

struct frame_header {
    bool     fin;
    opcode   op;
    bool     masked;
    uint64_t payload_len;
    uint8_t  mask[4];
    // Bytes consumed by this header (2..14)
    std::size_t header_size;
};

/**
 * @brief Parse a WS frame header from raw buffer.
 * @return false if not enough data yet; true and fills `out` if complete.
 */
inline bool parse_frame_header(const uint8_t* data, std::size_t avail, frame_header& out) noexcept {
    if (avail < 2) return false;

    out.fin    = (data[0] & 0x80u) != 0;
    out.op     = static_cast<opcode>(data[0] & 0x0Fu);
    out.masked = (data[1] & 0x80u) != 0;

    const uint8_t len7 = data[1] & 0x7Fu;
    std::size_t hdr    = 2;

    if (len7 < 126) {
        out.payload_len = len7;
    } else if (len7 == 126) {
        if (avail < 4) return false;
        out.payload_len = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        hdr = 4;
    } else { // 127
        if (avail < 10) return false;
        out.payload_len = 0;
        for (int i = 0; i < 8; ++i)
            out.payload_len = (out.payload_len << 8) | data[2 + i];
        hdr = 10;
    }

    if (out.masked) {
        if (avail < hdr + 4) return false;
        std::memcpy(out.mask, data + hdr, 4);
        hdr += 4;
    } else {
        std::memset(out.mask, 0, 4);
    }

    out.header_size = hdr;
    return true;
}

/**
 * @brief Unmask (or re-mask) payload bytes in-place.
 * XOR is its own inverse, so this doubles as mask and unmask.
 */
inline void unmask(uint8_t* payload, uint64_t len, const uint8_t mask[4]) noexcept {
    for (uint64_t i = 0; i < len; ++i)
        payload[i] ^= mask[i & 3];
}

/**
 * @brief Encode a single server→client WS frame (no masking per RFC 6455 §5.1).
 */
inline std::string encode_frame(opcode op, std::string_view payload, bool fin = true) {
    std::string frame;
    frame.reserve(payload.size() + 10);

    frame.push_back(static_cast<char>((fin ? 0x80u : 0x00u) | static_cast<uint8_t>(op)));

    const uint64_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len < 65536) {
        frame.push_back('\x7E');
        frame.push_back(static_cast<char>(len >> 8));
        frame.push_back(static_cast<char>(len & 0xFFu));
    } else {
        frame.push_back('\x7F');
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFFu));
    }

    frame.append(payload);
    return frame;
}

} // namespace ace::http::ws

#endif // ACE_HTTP_WS_FRAME_H
