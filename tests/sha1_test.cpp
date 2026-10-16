#include "check.h"

#include "pkmn/core/sha1.h"

#include <string>
#include <vector>

using namespace pkmn;

TEST_CASE(sha1_known_vectors)
{
    CHECK(sha1::to_hex(sha1::compute(byte_span {})) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    const std::string abc = "abc";
    byte_span abc_span(reinterpret_cast<const u8*>(abc.data()), abc.size());
    CHECK(sha1::to_hex(sha1::compute(abc_span)) == "a9993e364706816aba3e25717850c26c9cd0d89d");

    const std::string quick = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    byte_span quick_span(reinterpret_cast<const u8*>(quick.data()), quick.size());
    CHECK(sha1::to_hex(sha1::compute(quick_span)) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    std::vector<u8> million(1000000, static_cast<u8>('a'));
    CHECK(sha1::to_hex(sha1::compute(million)) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE(sha1_padding_boundaries)
{
    // SHA-1 appends 0x80 and the bit length, and needs a whole extra block once
    // the payload reaches 56 bytes. These lengths straddle every padding edge:
    // 55 fits one block, 56 forces a second, 64 is an exact block, 120 is two
    // blocks worth of padding room. Digests cross-checked against Windows CNG,
    // a separate implementation.
    auto repeated_a = [](usize count) {
        std::vector<u8> data(count, static_cast<u8>('a'));
        return sha1::to_hex(sha1::compute(data));
    };

    CHECK(repeated_a(55) == "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    CHECK(repeated_a(56) == "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    CHECK(repeated_a(63) == "03f09f5b158a7a8cdad920bddc29b81c18a551f5");
    CHECK(repeated_a(64) == "0098ba824b5c16427bd7a1122a5a442a25ec644d");
    CHECK(repeated_a(65) == "11655326c708d70319be2610e8a57d9a5b959d3b");
    CHECK(repeated_a(119) == "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56");
    CHECK(repeated_a(120) == "f34c1488385346a55709ba056ddd08280dd4c6d6");
}
