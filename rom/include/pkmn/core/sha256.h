#pragma once

#include "pkmn/core/bytes.h"

#include <array>
#include <string>

namespace pkmn::sha256 {

using Digest = std::array<u8, 32>;

Digest compute(byte_span data);
std::string to_hex(const Digest& digest);

}
