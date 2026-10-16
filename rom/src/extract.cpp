#include "pkmn/core/extract.h"

#include "pkmn/core/compression.h"
#include "pkmn/core/narc.h"
#include "pkmn/core/nds.h"
#include "pkmn/core/sha256.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>

namespace pkmn::extract {

namespace {

    bool is_safe_rel(const std::string& rel)
    {
        if (rel.empty() || rel.front() == '/' || rel.find('\\') != std::string::npos) {
            return false;
        }
        usize start = 0;
        while (start <= rel.size()) {
            usize slash = rel.find('/', start);
            usize end = slash == std::string::npos ? rel.size() : slash;
            std::string segment = rel.substr(start, end - start);
            if (segment.empty() || segment == "." || segment == "..") {
                return false;
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        return true;
    }

    bool is_narc(byte_span data)
    {
        return data.size() >= 0x10 && data[0] == 'N' && data[1] == 'A' && data[2] == 'R'
            && data[3] == 'C';
    }

    void extract_narc(const std::function<void(const std::string&, byte_span)>& write,
        byte_span data, const std::string& prefix, u32 depth, Result& result)
    {
        auto arc = narc::parse(data);
        if (!arc) {
            return;
        }
        for (u32 i = 0; i < arc->file_count(); ++i) {
            auto member = narc::file_data(data, *arc, i);
            if (!member) {
                ++result.skipped;
                continue;
            }
            char id[16];
            std::snprintf(id, sizeof(id), "%04u", i);
            ++result.narc_members;

            if (depth > 0 && is_narc(*member)) {
                ++result.narc_archives;
                std::string container = prefix + "/" + id + ".narc";
                write(container, *member);
                extract_narc(write, *member, prefix + "/" + id + ".d", depth - 1, result);
            } else {
                write(prefix + "/" + id, *member);
            }
        }
    }

}

Result to_directory(byte_span rom, const std::filesystem::path& dir, const Options& options)
{
    Result result;
    auto header = nds::parse_header(rom);
    if (!header) {
        return result;
    }

    auto write = [&](const std::string& rel, byte_span data) {
        if (!is_safe_rel(rel)) {
            ++result.skipped;
            return;
        }
        std::filesystem::path out = dir / std::filesystem::path(rel);
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        if (!file) {
            ++result.skipped;
            return;
        }
        file.write(
            reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            ++result.skipped;
            return;
        }

        Entry entry;
        entry.path = rel;
        entry.hash = sha256::to_hex(sha256::compute(data));
        entry.size = data.size();
        result.manifest.push_back(std::move(entry));
        ++result.files;
        result.bytes += data.size();
    };

    if (options.arm9 && header->arm9_size > 0
        && static_cast<usize>(header->arm9_offset) + header->arm9_size <= rom.size()) {
        byte_span bin = rom.subspan(header->arm9_offset, header->arm9_size);
        try {
            write("arm9.bin", compression::blz_decompress(bin));
        } catch (...) {
            ++result.skipped;
        }
    }

    if (options.overlays) {
        usize count = nds::overlay_count(rom);
        for (usize i = 0; i < count; ++i) {
            auto ov = nds::overlay(rom, i);
            if (!ov) {
                continue;
            }
            auto data = nds::file_data(rom, ov->file_id);
            if (!data) {
                ++result.skipped;
                continue;
            }
            std::vector<u8> bytes;
            if (ov->compressed()) {
                try {
                    bytes = compression::blz_decompress(*data);
                } catch (...) {
                    ++result.skipped;
                    continue;
                }
            } else {
                bytes.assign(data->begin(), data->end());
            }
            char name[32];
            std::snprintf(name, sizeof(name), "overlays/ov%03u.bin", ov->id);
            write(name, bytes);
        }
    }

    if (options.nitrofs) {
        u32 narc_count = 0;
        for (const auto& fe : nds::list_files(rom)) {
            auto data = nds::file_data(rom, fe.file_id);
            if (!data) {
                ++result.skipped;
                continue;
            }
            write("nitrofs/" + fe.path, *data);

            if (!options.narcs || !is_narc(*data)) {
                continue;
            }
            if (options.narc_limit != 0 && narc_count >= options.narc_limit) {
                continue;
            }
            ++narc_count;
            ++result.narc_archives;
            extract_narc(write, *data, "narc/" + fe.path, options.narc_depth, result);
        }
    }

    std::sort(result.manifest.begin(), result.manifest.end(),
        [](const Entry& a, const Entry& b) { return a.path < b.path; });
    return result;
}

std::string manifest_text(const Result& result)
{
    std::string out;
    out.reserve(result.manifest.size() * 80);
    for (const auto& entry : result.manifest) {
        out += entry.hash;
        out += "  ";
        out += entry.path;
        out += '\n';
    }
    return out;
}

}
