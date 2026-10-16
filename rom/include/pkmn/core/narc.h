#pragma once

#include "pkmn/core/bytes.h"
#include "pkmn/core/nds.h"

#include <optional>
#include <vector>

namespace pkmn::narc {

struct Archive {
    std::vector<nds::FatEntry> fat;
    std::vector<nds::FileEntry> names;
    usize image_offset = 0;

    usize file_count() const { return fat.size(); }
};

std::optional<Archive> parse(byte_span data);
std::optional<byte_span> file_data(byte_span data, const Archive& arc, u32 file_id);

}
