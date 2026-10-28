# Writes OUTPUT, a C++ file defining game_addresses::data::<NAME> and
# <NAME>Size as the bytes of INPUT. Run with cmake -P.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){32})" "\\1\n    " bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}. Do not edit.
#include <cstddef>

namespace game_addresses {
namespace data {
extern const unsigned char ${NAME}[] = {
    ${bytes}0x00
};
extern const size_t ${NAME}Size = ${size};
}  // namespace data
}  // namespace game_addresses
")
