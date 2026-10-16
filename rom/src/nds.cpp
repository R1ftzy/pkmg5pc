#include "pkmn/core/nds.h"

#include <functional>

namespace pkmn::nds {
namespace {

    constexpr usize HEADER_MIN = 0x200;
    constexpr usize OVERLAY_ENTRY_SIZE = 32;

    u32 rd32(byte_span rom, usize off)
    {
        return static_cast<u32>(rom[off]) | (static_cast<u32>(rom[off + 1]) << 8)
            | (static_cast<u32>(rom[off + 2]) << 16) | (static_cast<u32>(rom[off + 3]) << 24);
    }

    u16 rd16(byte_span buf, usize off)
    {
        return static_cast<u16>(buf[off]) | static_cast<u16>(static_cast<u16>(buf[off + 1]) << 8);
    }

    bool in_bounds(byte_span rom, usize off, usize size)
    {
        return off <= rom.size() && size <= rom.size() - off;
    }

    std::string read_ascii(byte_span rom, usize off, usize len)
    {
        std::string s;
        s.reserve(len);
        for (usize i = 0; i < len; ++i) {
            char c = static_cast<char>(rom[off + i]);
            if (c == '\0') {
                break;
            }
            s.push_back(c);
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) {
            s.pop_back();
        }
        return s;
    }

}

std::optional<Header> parse_header(byte_span rom)
{
    if (rom.size() < HEADER_MIN) {
        return std::nullopt;
    }
    Header h;
    h.title = read_ascii(rom, 0x00, 12);
    h.game_code = read_ascii(rom, 0x0C, 4);
    h.arm9_offset = rd32(rom, 0x20);
    h.arm9_entry = rd32(rom, 0x24);
    h.arm9_ram = rd32(rom, 0x28);
    h.arm9_size = rd32(rom, 0x2C);
    h.arm7_offset = rd32(rom, 0x30);
    h.arm7_entry = rd32(rom, 0x34);
    h.arm7_ram = rd32(rom, 0x38);
    h.arm7_size = rd32(rom, 0x3C);
    h.fnt_offset = rd32(rom, 0x40);
    h.fnt_size = rd32(rom, 0x44);
    h.fat_offset = rd32(rom, 0x48);
    h.fat_size = rd32(rom, 0x4C);
    h.ovl9_offset = rd32(rom, 0x50);
    h.ovl9_size = rd32(rom, 0x54);
    h.ovl7_offset = rd32(rom, 0x58);
    h.ovl7_size = rd32(rom, 0x5C);
    return h;
}

usize overlay_count(byte_span rom)
{
    auto h = parse_header(rom);
    if (!h || h->ovl9_offset == 0 || h->ovl9_size < OVERLAY_ENTRY_SIZE) {
        return 0;
    }
    if (!in_bounds(rom, h->ovl9_offset, h->ovl9_size)) {
        return 0;
    }
    return h->ovl9_size / OVERLAY_ENTRY_SIZE;
}

std::optional<Overlay> overlay(byte_span rom, usize index)
{
    auto h = parse_header(rom);
    if (!h || h->ovl9_offset == 0) {
        return std::nullopt;
    }
    usize off = h->ovl9_offset + index * OVERLAY_ENTRY_SIZE;
    if (!in_bounds(rom, off, OVERLAY_ENTRY_SIZE)) {
        return std::nullopt;
    }
    Overlay o;
    o.id = rd32(rom, off + 0);
    o.ram_addr = rd32(rom, off + 4);
    o.ram_size = rd32(rom, off + 8);
    o.bss_size = rd32(rom, off + 12);
    o.sinit_start = rd32(rom, off + 16);
    o.sinit_end = rd32(rom, off + 20);
    o.file_id = rd32(rom, off + 24);
    o.compressed_size = rd32(rom, off + 28) & 0x00FFFFFF;
    return o;
}

std::optional<FatEntry> fat_entry(byte_span rom, u32 file_id)
{
    auto h = parse_header(rom);
    if (!h || h->fat_offset == 0) {
        return std::nullopt;
    }
    usize off = static_cast<usize>(h->fat_offset) + static_cast<usize>(file_id) * 8;
    if (!in_bounds(rom, off, 8)) {
        return std::nullopt;
    }
    FatEntry e;
    e.start = rd32(rom, off);
    e.end = rd32(rom, off + 4);
    return e;
}

std::optional<byte_span> file_data(byte_span rom, u32 file_id)
{
    auto e = fat_entry(rom, file_id);
    if (!e || e->end <= e->start || e->end > rom.size()) {
        return std::nullopt;
    }
    return rom.subspan(e->start, e->end - e->start);
}

std::vector<FileEntry> list_files(byte_span rom)
{
    auto h = parse_header(rom);
    if (!h || h->fnt_offset == 0 || h->fnt_size < 8) {
        return {};
    }
    if (!in_bounds(rom, h->fnt_offset, h->fnt_size)) {
        return {};
    }
    return parse_fnt(rom.subspan(h->fnt_offset, h->fnt_size));
}

std::vector<FileEntry> parse_fnt(byte_span fnt)
{
    std::vector<FileEntry> out;
    if (fnt.size() < 8) {
        return out;
    }

    std::vector<bool> visited(0x1000, false);
    usize guard = 0;

    std::function<void(u16, const std::string&)> walk = [&](u16 dir_id, const std::string& prefix) {
        u16 index = static_cast<u16>(dir_id & 0x0FFF);
        if (index >= visited.size() || visited[index]) {
            return;
        }
        visited[index] = true;

        usize dir_off = static_cast<usize>(index) * 8;
        if (dir_off + 8 > fnt.size()) {
            return;
        }
        usize sub_off = rd32(fnt, dir_off);
        u32 file_id = rd16(fnt, dir_off + 4);

        usize pos = sub_off;
        while (pos < fnt.size()) {
            if (++guard > 200000) {
                return;
            }
            u8 type = fnt[pos++];
            if (type == 0x00) {
                break;
            }
            if (type == 0x80) {
                continue;
            }
            usize len = type < 0x80 ? type : (type - 0x80);
            if (pos + len > fnt.size()) {
                break;
            }
            std::string name(reinterpret_cast<const char*>(fnt.data() + pos), len);
            pos += len;
            if (type < 0x80) {
                out.push_back(FileEntry { prefix + name, file_id });
                ++file_id;
            } else {
                if (pos + 2 > fnt.size()) {
                    break;
                }
                u16 sub = rd16(fnt, pos);
                pos += 2;
                walk(sub, prefix + name + "/");
            }
        }
    };

    walk(0xF000, "");
    return out;
}

}
