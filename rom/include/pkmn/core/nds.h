#pragma once

#include "pkmn/core/bytes.h"

#include <optional>
#include <string>
#include <vector>

namespace pkmn::nds {

struct Header {
    std::string title;
    std::string game_code;
    u32 arm9_offset = 0;
    u32 arm9_entry = 0;
    u32 arm9_ram = 0;
    u32 arm9_size = 0;
    u32 arm7_offset = 0;
    u32 arm7_entry = 0;
    u32 arm7_ram = 0;
    u32 arm7_size = 0;
    u32 fnt_offset = 0;
    u32 fnt_size = 0;
    u32 fat_offset = 0;
    u32 fat_size = 0;
    u32 ovl9_offset = 0;
    u32 ovl9_size = 0;
    u32 ovl7_offset = 0;
    u32 ovl7_size = 0;
};

std::optional<Header> parse_header(byte_span rom);

struct Overlay {
    u32 id = 0;
    u32 ram_addr = 0;
    u32 ram_size = 0;
    u32 bss_size = 0;
    u32 sinit_start = 0;
    u32 sinit_end = 0;
    u32 file_id = 0;
    u32 compressed_size = 0;

    bool compressed() const { return compressed_size != 0; }
};

usize overlay_count(byte_span rom);
std::optional<Overlay> overlay(byte_span rom, usize index);

struct FatEntry {
    u32 start = 0;
    u32 end = 0;
};

std::optional<FatEntry> fat_entry(byte_span rom, u32 file_id);
std::optional<byte_span> file_data(byte_span rom, u32 file_id);

struct FileEntry {
    std::string path;
    u32 file_id = 0;
};

std::vector<FileEntry> parse_fnt(byte_span fnt);
std::vector<FileEntry> list_files(byte_span rom);

}
