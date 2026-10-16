#include "pkmn/core/narc.h"

#include <string>

namespace pkmn::narc {

namespace {

    u16 rd16(byte_span buf, usize off)
    {
        return static_cast<u16>(buf[off]) | static_cast<u16>(static_cast<u16>(buf[off + 1]) << 8);
    }

    u32 rd32(byte_span buf, usize off)
    {
        return static_cast<u32>(buf[off]) | (static_cast<u32>(buf[off + 1]) << 8)
            | (static_cast<u32>(buf[off + 2]) << 16) | (static_cast<u32>(buf[off + 3]) << 24);
    }

    bool magic_is(byte_span buf, usize off, const char* tag)
    {
        if (off + 4 > buf.size()) {
            return false;
        }
        return buf[off] == static_cast<u8>(tag[0]) && buf[off + 1] == static_cast<u8>(tag[1])
            && buf[off + 2] == static_cast<u8>(tag[2]) && buf[off + 3] == static_cast<u8>(tag[3]);
    }

    usize align4(usize value) { return (value + 3) & ~static_cast<usize>(3); }

}

std::optional<Archive> parse(byte_span data)
{
    if (data.size() < 0x10 || !magic_is(data, 0, "NARC")) {
        return std::nullopt;
    }
    Archive arc;
    usize chunks = rd16(data, 0x0E);
    usize off = rd16(data, 0x0C);
    if (off == 0) {
        off = 0x10;
    }

    for (usize i = 0; i < chunks && off + 8 <= data.size(); ++i) {
        u32 chunk_size = rd32(data, off + 4);
        if (chunk_size < 8 || off + chunk_size > data.size()) {
            return std::nullopt;
        }
        usize body = off + 8;
        usize body_size = chunk_size - 8;

        if (magic_is(data, off, "BTAF")) {
            if (body_size < 4) {
                return std::nullopt;
            }
            usize files = rd16(data, body);
            usize fat_off = body + 4;
            if (files * 8 > body_size - 4) {
                return std::nullopt;
            }
            arc.fat.reserve(files);
            for (usize f = 0; f < files; ++f) {
                arc.fat.push_back(
                    nds::FatEntry { rd32(data, fat_off + f * 8), rd32(data, fat_off + f * 8 + 4) });
            }
        } else if (magic_is(data, off, "BTNF")) {
            arc.names = nds::parse_fnt(data.subspan(body, body_size));
        } else if (magic_is(data, off, "GMIF")) {
            arc.image_offset = body;
        }

        off = align4(off + chunk_size);
    }

    if (arc.image_offset == 0 && arc.fat.empty()) {
        return std::nullopt;
    }
    return arc;
}

std::optional<byte_span> file_data(byte_span data, const Archive& arc, u32 file_id)
{
    if (file_id >= arc.fat.size()) {
        return std::nullopt;
    }
    const nds::FatEntry& e = arc.fat[file_id];
    if (e.end < e.start) {
        return std::nullopt;
    }
    usize start = arc.image_offset + e.start;
    usize size = e.end - e.start;
    if (start > data.size() || size > data.size() - start) {
        return std::nullopt;
    }
    return data.subspan(start, size);
}

}
