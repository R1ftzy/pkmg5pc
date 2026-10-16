#pragma once

#include "pkmn/core/bytes.h"

#include <array>
#include <string>

namespace pkmn::sha1 {

using Digest = std::array<u8, 20>;

Digest compute(byte_span data);
std::string to_hex(const Digest& digest);

}
