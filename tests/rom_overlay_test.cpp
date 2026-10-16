#include "check.h"

#include "pkmn/core/compression.h"
#include "pkmn/core/narc.h"
#include "pkmn/core/nds.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace pkmn;

namespace {

std::vector<u8> read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::filesystem::path find_rom()
{
    if (const char* env = std::getenv("PKMNG5PC_ROM"); env != nullptr && *env != '\0') {
        return env;
    }
    std::filesystem::path dir = std::filesystem::path(PKMNG5PC_SOURCE_DIR) / "roms";
    if (!std::filesystem::exists(dir)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".nds") {
            return entry.path();
        }
    }
    return {};
}

}

TEST_CASE(nds_overlays_decompress)
{
    std::filesystem::path rom_path = find_rom();
    if (rom_path.empty() || !std::filesystem::exists(rom_path)) {
        std::printf("  (skipped: no ROM found; set PKMNG5PC_ROM)\n");
        return;
    }
    std::vector<u8> rom = read_file(rom_path);
    std::printf("  ROM: %s (%zu bytes)\n", rom_path.filename().string().c_str(), rom.size());

    auto header = nds::parse_header(rom);
    CHECK(header.has_value());
    if (!header) {
        return;
    }
    CHECK(!header->title.empty());
    std::printf("  title='%s' code='%s' arm9=%u ovl9=%u\n", header->title.c_str(),
        header->game_code.c_str(), header->arm9_size, header->ovl9_size);

    usize count = nds::overlay_count(rom);
    CHECK(count > 0);

    if (header->arm9_size > 0
        && static_cast<usize>(header->arm9_offset) + header->arm9_size <= rom.size()) {
        byte_span bin = byte_span(rom).subspan(header->arm9_offset, header->arm9_size);
        try {
            auto dec = compression::blz_decompress(bin);
            std::printf("  arm9 blz: %u -> %zu\n", header->arm9_size, dec.size());
            CHECK(!dec.empty());
        } catch (const std::exception& e) {
            std::printf("  arm9 blz failed: %s\n", e.what());
            CHECK_MSG(false, "arm9 blz decompress failed");
        }
    }

    auto files = nds::list_files(rom);
    std::printf("  nitrofs: %zu files\n", files.size());
    CHECK(!files.empty());
    u32 fat_entries = header->fat_size / 8;
    int bad_id = 0;
    for (usize i = 0; i < files.size(); ++i) {
        if (i < 5) {
            std::printf("    %s (id=%u)\n", files[i].path.c_str(), files[i].file_id);
        }
        if (files[i].file_id >= fat_entries) {
            ++bad_id;
        }
    }
    CHECK_MSG(bad_id == 0, "FNT file id out of FAT range");

    int narc_archives = 0;
    int narc_members = 0;
    int narc_named = 0;
    int narc_bad = 0;
    for (const auto& fe : files) {
        auto data = nds::file_data(rom, fe.file_id);
        if (!data || data->size() < 0x10) {
            continue;
        }
        byte_span d = *data;
        if (!(d[0] == 'N' && d[1] == 'A' && d[2] == 'R' && d[3] == 'C')) {
            continue;
        }
        auto arc = narc::parse(d);
        if (!arc || arc->file_count() == 0) {
            ++narc_bad;
            continue;
        }
        ++narc_archives;
        narc_members += static_cast<int>(arc->file_count());
        narc_named += static_cast<int>(arc->names.size());
        for (const auto& ne : arc->names) {
            if (ne.file_id >= arc->file_count()) {
                ++narc_bad;
            }
        }
        if (narc_archives <= 3) {
            std::printf("  narc %s: %zu files, %zu named\n", fe.path.c_str(), arc->file_count(),
                arc->names.size());
        }
    }
    std::printf("  narc: %d archives, %d members, %d named, %d bad\n", narc_archives, narc_members,
        narc_named, narc_bad);
    CHECK(narc_archives > 0);
    CHECK_MSG(narc_bad == 0, "invalid NARC archive or member id");

    int compressed = 0;
    int matched = 0;
    int raw_matched = 0;
    int mismatched = 0;
    for (usize i = 0; i < count; ++i) {
        auto ov = nds::overlay(rom, i);
        if (!ov) {
            continue;
        }
        auto data = nds::file_data(rom, ov->file_id);
        if (!data) {
            continue;
        }
        if (ov->compressed()) {
            ++compressed;
            if (data->size() != ov->compressed_size) {
                ++mismatched;
                if (mismatched <= 3) {
                    std::printf("  ov%zu compressed_size=%u but file=%zu\n", i, ov->compressed_size,
                        data->size());
                }
                continue;
            }
            try {
                auto out = compression::blz_decompress(*data);
                if (out.size() == ov->ram_size) {
                    ++matched;
                } else {
                    ++mismatched;
                    if (mismatched <= 5) {
                        std::printf("  size mismatch ov%zu: got %zu want %u\n", i, out.size(),
                            ov->ram_size);
                    }
                }
            } catch (const std::exception& e) {
                ++mismatched;
                if (mismatched <= 5) {
                    std::printf("  blz failed ov%zu: %s\n", i, e.what());
                }
            }
        } else if (data->size() == ov->ram_size) {
            ++raw_matched;
        } else {
            ++mismatched;
            if (mismatched <= 5) {
                std::printf(
                    "  raw mismatch ov%zu: file=%zu ram=%u\n", i, data->size(), ov->ram_size);
            }
        }
    }
    std::printf("  overlays: %d compressed (%d ram-matched), %d raw matched, %d mismatched\n",
        compressed, matched, raw_matched, mismatched);
    CHECK(compressed > 0);
    CHECK(matched == compressed);
    CHECK(mismatched == 0);
}
