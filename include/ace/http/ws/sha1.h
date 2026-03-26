#ifndef ACE_HTTP_WS_SHA1_H
#define ACE_HTTP_WS_SHA1_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ace::http::ws::detail {

// RFC 3174 SHA-1 — used only for WebSocket handshake (Sec-WebSocket-Accept)
inline std::array<uint8_t, 20> sha1(std::string_view input) {
    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    std::string msg(input);
    uint64_t bit_len = msg.size() * 8;
    msg.push_back('\x80');
    while (msg.size() % 64 != 56) msg.push_back('\0');
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

    auto rot = [](uint32_t v, unsigned n) -> uint32_t {
        return (v << n) | (v >> (32u - n));
    };

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint8_t>(msg[off + i*4])   << 24u)
                 | (static_cast<uint8_t>(msg[off + i*4+1]) << 16u)
                 | (static_cast<uint8_t>(msg[off + i*4+2]) <<  8u)
                 |  static_cast<uint8_t>(msg[off + i*4+3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rot(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);           k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t tmp = rot(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rot(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out{};
    auto put = [&](int off, uint32_t v) {
        out[off]   = static_cast<uint8_t>(v >> 24);
        out[off+1] = static_cast<uint8_t>(v >> 16);
        out[off+2] = static_cast<uint8_t>(v >>  8);
        out[off+3] = static_cast<uint8_t>(v);
    };
    put(0, h0); put(4, h1); put(8, h2); put(12, h3); put(16, h4);
    return out;
}

inline std::string base64_encode(const uint8_t* data, std::size_t len) {
    static constexpr char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t v  =  static_cast<uint32_t>(data[i]) << 16;
        if (i+1 < len) v |= static_cast<uint32_t>(data[i+1]) << 8;
        if (i+2 < len) v |= static_cast<uint32_t>(data[i+2]);
        out.push_back(T[(v >> 18) & 63]);
        out.push_back(T[(v >> 12) & 63]);
        out.push_back(i+1 < len ? T[(v >>  6) & 63] : '=');
        out.push_back(i+2 < len ? T[(v      ) & 63] : '=');
    }
    return out;
}

} // namespace ace::http::ws::detail

#endif // ACE_HTTP_WS_SHA1_H
