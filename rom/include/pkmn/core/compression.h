#pragma once

#include "pkmn/core/bytes.h"

#include <optional>
#include <vector>

namespace pkmn::compression {

enum class Format : u8 {
    none = 0x00,
    lz10 = 0x10,
    lz11 = 0x11,
    huffman4 = 0x24,
    huffman8 = 0x28,
    rle = 0x30,
};

std::optional<Format> detect(byte_span in);

u32 decompressed_size(byte_span in);

std::vector<u8> decompress(byte_span in);

std::vector<u8> lz10_decompress(byte_span in);
std::vector<u8> lz11_decompress(byte_span in);
std::vector<u8> rle_decompress(byte_span in);
std::vector<u8> huffman_decompress(byte_span in);

std::vector<u8> blz_decompress(byte_span in);

std::vector<u8> lz10_compress(byte_span in);
std::vector<u8> lz11_compress(byte_span in);
std::vector<u8> rle_compress(byte_span in);
std::vector<u8> huffman4_compress(byte_span in);
std::vector<u8> huffman8_compress(byte_span in);

}
