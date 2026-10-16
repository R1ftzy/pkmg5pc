#include "check.h"

#include "pkmn/core/sha256.h"

#include <string>
#include <vector>

using namespace pkmn;

TEST_CASE(sha256_known_vectors)
{
    CHECK(sha256::to_hex(sha256::compute(byte_span {}))
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::string abc = "abc";
    byte_span abc_span(reinterpret_cast<const u8*>(abc.data()), abc.size());
    CHECK(sha256::to_hex(sha256::compute(abc_span))
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::string quick = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    byte_span quick_span(reinterpret_cast<const u8*>(quick.data()), quick.size());
    CHECK(sha256::to_hex(sha256::compute(quick_span))
        == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    std::vector<u8> million(1000000, static_cast<u8>('a'));
    CHECK(sha256::to_hex(sha256::compute(million))
        == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
