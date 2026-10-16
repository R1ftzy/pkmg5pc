#include "check.h"

#include "pkmn/core/compression.h"

#include <random>
#include <vector>

using namespace pkmn;
using namespace pkmn::compression;

namespace {

std::vector<u8> random_bytes(usize n, u32 seed, int alphabet)
{
    std::mt19937 rng(seed);
    std::vector<u8> v(n);
    for (u8& b : v) {
        b = static_cast<u8>(alphabet > 0 ? (rng() % static_cast<u32>(alphabet)) : (rng() & 0xFF));
    }
    return v;
}

std::vector<u8> all_same(usize n, u8 value) { return std::vector<u8>(n, value); }

std::vector<u8> pattern(usize n, u32 seed)
{
    std::mt19937 rng(seed);
    std::vector<u8> v;
    v.reserve(n);
    while (v.size() < n) {
        usize run = 1 + (rng() % 40);
        u8 value = static_cast<u8>(rng() & 0xFF);
        for (usize i = 0; i < run && v.size() < n; ++i) {
            v.push_back(value);
        }
    }
    return v;
}

void roundtrip_rle(const std::vector<u8>& data)
{
    auto packed = rle_compress(data);
    CHECK(detect(packed) == Format::rle);
    CHECK(decompressed_size(packed) == data.size());
    CHECK(rle_decompress(packed) == data);
    CHECK(decompress(packed) == data);
}

void roundtrip_lz10(const std::vector<u8>& data)
{
    auto packed = lz10_compress(data);
    CHECK(detect(packed) == Format::lz10);
    CHECK(decompressed_size(packed) == data.size());
    CHECK(lz10_decompress(packed) == data);
    CHECK(decompress(packed) == data);
}

void roundtrip_lz11(const std::vector<u8>& data)
{
    auto packed = lz11_compress(data);
    CHECK(detect(packed) == Format::lz11);
    CHECK(decompressed_size(packed) == data.size());
    CHECK(lz11_decompress(packed) == data);
    CHECK(decompress(packed) == data);
}

void roundtrip_huffman4(const std::vector<u8>& data)
{
    auto packed = huffman4_compress(data);
    CHECK(detect(packed) == Format::huffman4);
    CHECK(decompressed_size(packed) == data.size());
    CHECK(huffman_decompress(packed) == data);
    CHECK(decompress(packed) == data);
}

void roundtrip_huffman8(const std::vector<u8>& data)
{
    auto packed = huffman8_compress(data);
    CHECK(detect(packed) == Format::huffman8);
    CHECK(decompressed_size(packed) == data.size());
    CHECK(huffman_decompress(packed) == data);
    CHECK(decompress(packed) == data);
}

}

TEST_CASE(rle_roundtrip)
{
    roundtrip_rle(random_bytes(2048, 1, 0));
    roundtrip_rle(random_bytes(2048, 2, 3));
    roundtrip_rle(all_same(1000, 0xAB));
    roundtrip_rle(pattern(4096, 3));
    roundtrip_rle({ 1, 1, 1, 2, 3, 4, 4, 4, 4, 4, 5 });
    roundtrip_rle({ 9, 8, 7, 6 });
}

TEST_CASE(lz10_roundtrip)
{
    roundtrip_lz10(random_bytes(2048, 11, 0));
    roundtrip_lz10(random_bytes(1024, 12, 4));
    roundtrip_lz10(all_same(2000, 0x5A));
    roundtrip_lz10(pattern(4096, 13));
    roundtrip_lz10({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 });
}

TEST_CASE(lz11_roundtrip)
{
    roundtrip_lz11(random_bytes(2048, 21, 0));
    roundtrip_lz11(random_bytes(1024, 22, 4));
    roundtrip_lz11(all_same(5000, 0x33));
    roundtrip_lz11(pattern(8192, 23));
    roundtrip_lz11({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 });
}

TEST_CASE(huffman4_roundtrip)
{
    roundtrip_huffman4(random_bytes(2048, 31, 0));
    roundtrip_huffman4(random_bytes(1024, 32, 5));
    roundtrip_huffman4(pattern(4096, 33));
}

TEST_CASE(huffman8_roundtrip)
{
    roundtrip_huffman8(random_bytes(2048, 41, 12));
    roundtrip_huffman8(random_bytes(4096, 42, 6));
    roundtrip_huffman8(random_bytes(1024, 43, 3));
}

TEST_CASE(decompress_rejects_bad_input)
{
    CHECK(!detect(byte_span {}).has_value());
    CHECK(!detect(std::vector<u8> { 0x00, 0x01, 0x02, 0x03 }).has_value());
    CHECK_THROWS(decompress(byte_span {}));
    CHECK_THROWS(decompress(std::vector<u8> { 0x99, 0x00, 0x00, 0x00 }));
    std::vector<u8> truncated { 0x10, 0x10, 0x00, 0x00 };
    CHECK_THROWS(lz10_decompress(truncated));
}
