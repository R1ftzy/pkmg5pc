#include "pkmn/core/sha1.h"

#include <cstring>

namespace pkmn::sha1 {

namespace {

    constexpr u32 rotl(u32 x, u32 n) { return (x << n) | (x >> (32 - n)); }

    void process_block(u32 state[5], const u8* block)
    {
        u32 w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<u32>(block[i * 4]) << 24)
                | (static_cast<u32>(block[i * 4 + 1]) << 16)
                | (static_cast<u32>(block[i * 4 + 2]) << 8) | static_cast<u32>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        u32 a = state[0];
        u32 b = state[1];
        u32 c = state[2];
        u32 d = state[3];
        u32 e = state[4];

        for (int i = 0; i < 80; ++i) {
            u32 f = 0;
            u32 k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            u32 temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

}

Digest compute(byte_span data)
{
    u32 state[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };

    usize len = data.size();
    usize full = len / 64;
    for (usize i = 0; i < full; ++i) {
        process_block(state, data.data() + i * 64);
    }

    u8 tail[128] = { 0 };
    usize rem = len % 64;
    std::memcpy(tail, data.data() + full * 64, rem);
    tail[rem] = 0x80;
    usize tail_len = rem < 56 ? 64 : 128;
    u64 bits = static_cast<u64>(len) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_len - 1 - i] = static_cast<u8>(bits >> (i * 8));
    }
    process_block(state, tail);
    if (tail_len == 128) {
        process_block(state, tail + 64);
    }

    Digest out {};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<u8>(state[i] >> 24);
        out[i * 4 + 1] = static_cast<u8>(state[i] >> 16);
        out[i * 4 + 2] = static_cast<u8>(state[i] >> 8);
        out[i * 4 + 3] = static_cast<u8>(state[i]);
    }
    return out;
}

std::string to_hex(const Digest& digest)
{
    static const char hex[] = "0123456789abcdef";
    std::string out(40, '0');
    for (usize i = 0; i < digest.size(); ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    return out;
}

}
