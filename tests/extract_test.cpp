#include "check.h"

#include "pkmn/core/extract.h"
#include "pkmn/core/nds.h"
#include "pkmn/core/sha256.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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

TEST_CASE(extract_manifest_deterministic)
{
    std::filesystem::path rom_path = find_rom();
    if (rom_path.empty() || !std::filesystem::exists(rom_path)) {
        std::printf("  (skipped: no ROM found; set PKMNG5PC_ROM)\n");
        return;
    }

    std::vector<u8> rom = read_file(rom_path);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base = std::filesystem::temp_directory_path()
        / ("pkmng5pc_extract_test_" + std::to_string(stamp));
    std::filesystem::remove_all(base);

    extract::Options options;
    options.narc_limit = 2;

    extract::Result a = extract::to_directory(rom, base / "a", options);
    extract::Result b = extract::to_directory(rom, base / "b", options);

    CHECK(a.files > 0);
    CHECK(a.files == b.files);
    CHECK(!a.manifest.empty());
    CHECK(extract::manifest_text(a) == extract::manifest_text(b));

    int missing = 0;
    int bad = 0;
    for (const auto& entry : a.manifest) {
        std::filesystem::path p = (base / "a") / entry.path;
        if (!std::filesystem::exists(p)) {
            ++missing;
            continue;
        }
        if (std::filesystem::file_size(p) != entry.size) {
            ++bad;
            continue;
        }
        if (sha256::to_hex(sha256::compute(read_file(p))) != entry.hash) {
            ++bad;
        }
    }
    CHECK_MSG(missing == 0, "manifest lists files not written");
    CHECK_MSG(bad == 0, "manifest hash/size mismatch");

    std::string text = extract::manifest_text(a);
    CHECK(text.back() == '\n');
    std::printf("  extracted: %zu files, %d narc archives, %zu members\n", a.files, a.narc_archives,
        a.narc_members);

    std::filesystem::remove_all(base);
}
