#pragma once

#include "pkmn/core/bytes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pkmn::extract {

struct Options {
    bool nitrofs = true;
    bool overlays = true;
    bool arm9 = true;
    bool narcs = true;
    u32 narc_limit = 0;
    u32 narc_depth = 3;
};

struct Entry {
    std::string path;
    std::string hash;
    u64 size = 0;
};

struct Result {
    usize files = 0;
    u64 bytes = 0;
    int narc_archives = 0;
    usize narc_members = 0;
    usize skipped = 0;
    std::vector<Entry> manifest;
};

Result to_directory(byte_span rom, const std::filesystem::path& dir, const Options& options = {});

std::string manifest_text(const Result& result);

}
