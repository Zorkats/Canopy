#include "nltx.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace nltx {
namespace {

constexpr char kNltxMagic[] = "NMPLTEX1";
constexpr char kYkcmpMagic[] = "YKCMP_V1";
constexpr size_t kMagicSize = 8;
// The version every vanilla file has. 100 selects an older layout, and 101
// makes the engine ignore +0x34 and read the payload at 0x80.
constexpr uint32_t kVersion = 0x66;
constexpr uint32_t kOldLayoutVersion = 100;
constexpr uint32_t kFixedOffsetVersion = 101;
constexpr uint32_t kYkcmpLz4 = 9;
// +0x20..+0x2B of every vanilla file: mips 1, compressed 1, array 0.
constexpr uint8_t kHeaderTemplate[12] = {0x04, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

// LZ4_decompress_fast rejects a match that ends in the last 5 output bytes,
// and a literal run that ends in the last 8 unless it is the final one. The
// encoder starts no match in the last 12 bytes, as the reference one does.
constexpr size_t kMinMatch = 4;
constexpr size_t kLastLiterals = 5;
constexpr size_t kWildCopy = 8;
constexpr size_t kMatchFindLimit = 12;
constexpr size_t kMaxDistance = 0xFFFF;
constexpr int kHashBits = 16;
constexpr int kMaxAttempts = 16;

uint16_t ReadU16(const uint8_t* data, size_t offset) {
    uint16_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

uint32_t ReadU32(const uint8_t* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

void WriteU32(uint8_t* data, size_t offset, uint32_t value) {
    std::memcpy(data + offset, &value, sizeof(value));
}

std::string Hex(uint32_t value) {
    char text[16] = {};
    std::snprintf(text, sizeof(text), "0x%X", value);
    return text;
}

std::string Size(uint32_t width, uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

// ---- LZ4 -----------------------------------------------------------------

// Adds the 255-continued bytes that follow a length nibble of 15.
bool ReadLength(const uint8_t* input, size_t inputSize, size_t& position, size_t& length) {
    uint8_t byte = 0;
    do {
        if (position >= inputSize) {
            return false;
        }
        byte = input[position++];
        length += byte;
    } while (byte == 255);
    return true;
}

void WriteLength(std::vector<uint8_t>& block, size_t length) {
    for (; length >= 255; length -= 255) {
        block.push_back(255);
    }
    block.push_back(static_cast<uint8_t>(length));
}

// One sequence: the literals, then a match unless length is 0.
void WriteSequence(
    std::vector<uint8_t>& block, const uint8_t* literals, size_t literalCount, size_t distance, size_t length
) {
    const size_t matchCode = length ? length - kMinMatch : 0;
    block.push_back(static_cast<uint8_t>((std::min<size_t>(literalCount, 15) << 4) | std::min<size_t>(matchCode, 15)));
    if (literalCount >= 15) {
        WriteLength(block, literalCount - 15);
    }
    block.insert(block.end(), literals, literals + literalCount);
    if (length == 0) {
        return;
    }
    block.push_back(static_cast<uint8_t>(distance & 0xFF));
    block.push_back(static_cast<uint8_t>(distance >> 8));
    if (matchCode >= 15) {
        WriteLength(block, matchCode - 15);
    }
}

uint32_t Hash(const uint8_t* at) {
    return (ReadU32(at, 0) * 2654435761u) >> (32 - kHashBits);
}

// ---- YKCMP type 4 --------------------------------------------------------
// The opcode stream tools\ykcmp.py documents:
//   00-7F  a run of that many literal bytes
//   80-BF  copy (b >> 4) - 7 bytes from distance (b & 0xF) + 1
//   C0-DF  copy b - 0xBE bytes from distance n0 + 1
//   E0-FF  copy ((b & 0x1F) << 4 | n0 >> 4) + 3 bytes from distance
//          ((n0 & 0xF) << 8 | n1) + 1

bool DecodeType4(const uint8_t* input, size_t inputSize, uint8_t* output, size_t rawSize, std::string& error) {
    size_t in = 0;
    size_t out = 0;
    while (out < rawSize) {
        if (in >= inputSize) {
            error = "YKCMP type 4: the input ended at output " + std::to_string(out) + " of " + std::to_string(rawSize);
            return false;
        }
        const uint8_t op = input[in++];
        if (op < 0x80) {
            if (op > inputSize - in || op > rawSize - out) {
                error = "YKCMP type 4: a literal run passes the end of the data";
                return false;
            }
            std::memcpy(output + out, input + in, op);
            in += op;
            out += op;
            continue;
        }
        size_t length = 0;
        size_t distance = 0;
        if (op < 0xC0) {
            length = static_cast<size_t>(op >> 4) - 7;
            distance = static_cast<size_t>(op & 0xF) + 1;
        } else if (op < 0xE0) {
            if (in >= inputSize) {
                error = "YKCMP type 4: the input ended inside a copy";
                return false;
            }
            length = static_cast<size_t>(op) - 0xBE;
            distance = static_cast<size_t>(input[in++]) + 1;
        } else {
            if (inputSize - in < 2) {
                error = "YKCMP type 4: the input ended inside a copy";
                return false;
            }
            length = ((static_cast<size_t>(op & 0x1F) << 4) | (input[in] >> 4)) + 3;
            distance = ((static_cast<size_t>(input[in] & 0xF) << 8) | input[in + 1]) + 1;
            in += 2;
        }
        if (distance > out || length > rawSize - out) {
            error = "YKCMP type 4: a copy at output " + std::to_string(out) + " is out of range";
            return false;
        }
        for (size_t index = 0; index < length; ++index) {
            output[out + index] = output[out + index - distance];
        }
        out += length;
    }
    return true;
}

// ---- BC7 -----------------------------------------------------------------

struct Bc7Mode {
    int subsets;
    int partitionBits;
    int rotationBits;
    int selectorBits;
    int colorBits;
    int alphaBits;
    int endpointPBits;  // one p-bit per endpoint
    int sharedPBits;    // one p-bit per subset
    int indexBits;
    int indexBits2;     // the second index set of modes 4 and 5
};

constexpr Bc7Mode kBc7Modes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

constexpr uint8_t kWeights2[4] = {0, 21, 43, 64};
constexpr uint8_t kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr uint8_t kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

// The subset of each pixel, for each of the 64 partitions.
constexpr uint8_t kPartitions2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1}, {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1}, {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0}, {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}, {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0}, {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1}, {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0}, {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0}, {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0}, {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0}, {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1},
};

constexpr uint8_t kPartitions3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2}, {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2}, {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0}, {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2}, {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1}, {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2}, {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0}, {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0}, {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1}, {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1}, {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1}, {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2}, {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2}, {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2}, {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1}, {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0},
};

constexpr uint8_t kOneSubset[16] = {};

// The anchor pixel of subset 1 in two-subset partitions, and of subsets 1
// and 2 in three-subset ones. Subset 0 always anchors at pixel 0. An
// anchor's index is stored without its top bit, which is 0.
constexpr uint8_t kAnchors2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};

constexpr uint8_t kAnchors3Second[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};

constexpr uint8_t kAnchors3Third[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};

// Reads a 128-bit block from bit 0 of byte 0 up.
class BitReader {
public:
    explicit BitReader(const uint8_t* block) : block_(block) {}

    uint32_t Read(int count) {
        uint32_t value = 0;
        for (int bit = 0; bit < count; ++bit, ++position_) {
            value |= static_cast<uint32_t>((block_[position_ >> 3] >> (position_ & 7)) & 1) << bit;
        }
        return value;
    }

private:
    const uint8_t* block_;
    int position_ = 0;
};

// An endpoint of bits bits to 8, its top bits repeated into the low ones.
uint8_t Expand(uint32_t value, int bits) {
    value <<= 8 - bits;
    return static_cast<uint8_t>(value | (value >> bits));
}

int Weight(int bits, uint32_t index) {
    return bits == 2 ? kWeights2[index] : bits == 3 ? kWeights3[index] : kWeights4[index];
}

uint8_t Interpolate(uint8_t low, uint8_t high, int weight) {
    return static_cast<uint8_t>(((64 - weight) * low + weight * high + 32) >> 6);
}

}  // namespace

// ---- LZ4 blocks ----------------------------------------------------------

bool Lz4Decode(
    const uint8_t* input, size_t inputSize, uint8_t* output, size_t rawSize, size_t& used, std::string& error
) {
    size_t in = 0;
    size_t out = 0;
    for (;;) {
        if (in >= inputSize) {
            error = "LZ4: the input ended before a token";
            return false;
        }
        const uint8_t token = input[in++];
        size_t literals = token >> 4;
        if (literals == 15 && !ReadLength(input, inputSize, in, literals)) {
            error = "LZ4: the input ended inside a literal length";
            return false;
        }
        if (literals > inputSize - in || literals > rawSize - out) {
            error = "LZ4: a literal run at output " + std::to_string(out) + " passes the end of the data";
            return false;
        }
        if (literals) {
            std::memcpy(output + out, input + in, literals);
        }
        in += literals;
        out += literals;
        if (out == rawSize) {
            used = in;
            return true;
        }
        if (rawSize - out < kWildCopy) {
            error = "LZ4: a literal run ends " + std::to_string(rawSize - out) +
                    " bytes before the output end without being the last sequence";
            return false;
        }
        if (inputSize - in < 2) {
            error = "LZ4: the input ended before a match offset";
            return false;
        }
        const size_t distance = input[in] | (static_cast<size_t>(input[in + 1]) << 8);
        in += 2;
        if (distance == 0 || distance > out) {
            error = "LZ4: match offset " + std::to_string(distance) + " at output " + std::to_string(out) +
                    " is out of range";
            return false;
        }
        size_t length = token & 15;
        if (length == 15 && !ReadLength(input, inputSize, in, length)) {
            error = "LZ4: the input ended inside a match length";
            return false;
        }
        length += kMinMatch;
        if (length > rawSize - out || rawSize - out - length < kLastLiterals) {
            error = "LZ4: a match at output " + std::to_string(out) + " reaches the last 5 bytes, which must be literals";
            return false;
        }
        uint8_t* target = output + out;
        const uint8_t* source = target - distance;
        if (distance >= length) {
            std::memcpy(target, source, length);
        } else {
            for (size_t index = 0; index < length; ++index) {
                target[index] = source[index];
            }
        }
        out += length;
    }
}

void Lz4EncodeLiterals(const uint8_t* input, size_t size, std::vector<uint8_t>& block) {
    block.clear();
    block.reserve(size + size / 255 + 2);
    WriteSequence(block, input, size, 0, 0);
}

void Lz4Encode(const uint8_t* input, size_t size, std::vector<uint8_t>& block) {
    block.clear();
    block.reserve(size + size / 255 + 16);
    size_t anchor = 0;
    if (size > kMatchFindLimit && size < 0xFFFFFFFFu) {
        // Positions are stored plus one, so 0 means none. chain links each
        // position to the previous one with the same hash.
        std::vector<uint32_t> head(size_t(1) << kHashBits, 0);
        std::vector<uint32_t> chain(kMaxDistance + 1, 0);
        const size_t searchEnd = size - kMatchFindLimit;
        const size_t matchEnd = size - kLastLiterals;
        size_t inserted = 0;
        size_t position = 0;
        while (position < searchEnd) {
            for (; inserted < position; ++inserted) {
                const uint32_t hash = Hash(input + inserted);
                chain[inserted & kMaxDistance] = head[hash];
                head[hash] = static_cast<uint32_t>(inserted + 1);
            }
            size_t bestLength = 0;
            size_t bestDistance = 0;
            uint32_t candidate = head[Hash(input + position)];
            for (int attempt = 0; candidate && attempt < kMaxAttempts; ++attempt) {
                const size_t start = candidate - 1;
                if (position - start > kMaxDistance) {
                    break;
                }
                size_t length = 0;
                while (position + length < matchEnd && input[start + length] == input[position + length]) {
                    ++length;
                }
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = position - start;
                    if (position + length == matchEnd) {
                        break;
                    }
                }
                // A slot reused by a later position no longer leads back.
                const uint32_t next = chain[start & kMaxDistance];
                if (next >= candidate) {
                    break;
                }
                candidate = next;
            }
            if (bestLength < kMinMatch) {
                ++position;
                continue;
            }
            WriteSequence(block, input + anchor, position - anchor, bestDistance, bestLength);
            position += bestLength;
            anchor = position;
        }
    }
    WriteSequence(block, input + anchor, size - anchor, 0, 0);
}

// ---- YKCMP_V1 ------------------------------------------------------------

bool YkcmpUnpack(const uint8_t* data, size_t size, std::vector<uint8_t>& raw, std::string& error) {
    if (!data || size < kYkcmpHeaderSize || std::memcmp(data, kYkcmpMagic, kMagicSize) != 0) {
        error = "the payload is not a YKCMP_V1 container";
        return false;
    }
    const uint32_t type = ReadU32(data, 8) & 0xFF;
    const uint32_t total = ReadU32(data, 12);
    const uint32_t rawSize = ReadU32(data, 16);
    if (total < kYkcmpHeaderSize || total > size) {
        error = "the YKCMP total size " + std::to_string(total) + " does not fit the " + std::to_string(size) +
                " bytes available";
        return false;
    }
    if (rawSize > kMaxRawSize) {
        error = "the YKCMP raw size " + std::to_string(rawSize) + " is over the 1 GiB limit";
        return false;
    }
    const uint8_t* body = data + kYkcmpHeaderSize;
    const size_t bodySize = total - kYkcmpHeaderSize;
    raw.assign(rawSize, 0);
    if (type == 8 || type == 9) {
        size_t used = 0;
        if (!Lz4Decode(body, bodySize, raw.data(), rawSize, used, error)) {
            return false;
        }
        if (used != bodySize) {
            // The engine fails the load unless every byte is consumed.
            error = "LZ4 read " + std::to_string(used) + " of the " + std::to_string(bodySize) +
                    " payload bytes; the game requires all of them";
            return false;
        }
        return true;
    }
    if (type == 4) {
        return DecodeType4(body, bodySize, raw.data(), rawSize, error);
    }
    error = "YKCMP type " + std::to_string(type) + " is not supported, only types 4, 8 and 9 are";
    return false;
}

void YkcmpPack(const std::vector<uint8_t>& block, uint32_t rawSize, std::vector<uint8_t>& container) {
    container.assign(kYkcmpHeaderSize, 0);
    std::memcpy(container.data(), kYkcmpMagic, kMagicSize);
    WriteU32(container.data(), 8, kYkcmpLz4);
    WriteU32(container.data(), 12, static_cast<uint32_t>(block.size() + kYkcmpHeaderSize));
    WriteU32(container.data(), 16, rawSize);
    container.insert(container.end(), block.begin(), block.end());
}

// ---- BC7 -----------------------------------------------------------------

void DecodeBc7Block(const uint8_t* block, uint8_t* pixels) {
    int mode = 0;
    while (mode < 8 && !(block[0] & (1 << mode))) {
        ++mode;
    }
    if (mode == 8) {
        std::memset(pixels, 0, 64);
        return;
    }
    const Bc7Mode& info = kBc7Modes[mode];
    BitReader bits(block);
    bits.Read(mode + 1);
    const uint32_t partition = bits.Read(info.partitionBits);
    const uint32_t rotation = bits.Read(info.rotationBits);
    const uint32_t selector = bits.Read(info.selectorBits);

    // All reds, then all greens, blues and alphas, each in endpoint order:
    // subset 0 low, subset 0 high, subset 1 low and so on.
    const int endpointCount = info.subsets * 2;
    uint32_t endpoints[6][4] = {};
    for (int channel = 0; channel < 4; ++channel) {
        const int channelBits = channel < 3 ? info.colorBits : info.alphaBits;
        for (int endpoint = 0; endpoint < endpointCount; ++endpoint) {
            endpoints[endpoint][channel] = bits.Read(channelBits);
        }
    }
    int colorBits = info.colorBits;
    int alphaBits = info.alphaBits;
    if (info.endpointPBits || info.sharedPBits) {
        uint32_t pBits[6] = {};
        for (int endpoint = 0; endpoint < endpointCount; ++endpoint) {
            pBits[endpoint] = info.endpointPBits || endpoint % 2 == 0 ? bits.Read(1) : pBits[endpoint - 1];
        }
        for (int endpoint = 0; endpoint < endpointCount; ++endpoint) {
            for (int channel = 0; channel < 4; ++channel) {
                endpoints[endpoint][channel] = (endpoints[endpoint][channel] << 1) | pBits[endpoint];
            }
        }
        ++colorBits;
        alphaBits += alphaBits ? 1 : 0;
    }
    uint8_t colors[6][4] = {};
    for (int endpoint = 0; endpoint < endpointCount; ++endpoint) {
        for (int channel = 0; channel < 3; ++channel) {
            colors[endpoint][channel] = Expand(endpoints[endpoint][channel], colorBits);
        }
        colors[endpoint][3] = alphaBits ? Expand(endpoints[endpoint][3], alphaBits) : 255;
    }

    const uint8_t* subsetOf = info.subsets == 2   ? kPartitions2[partition]
                              : info.subsets == 3 ? kPartitions3[partition]
                                                  : kOneSubset;
    auto isAnchor = [&](int pixel) {
        return pixel == 0 || (info.subsets == 2 && pixel == kAnchors2[partition]) ||
               (info.subsets == 3 && (pixel == kAnchors3Second[partition] || pixel == kAnchors3Third[partition]));
    };
    uint32_t indices[16] = {};
    uint32_t indices2[16] = {};
    for (int pixel = 0; pixel < 16; ++pixel) {
        indices[pixel] = bits.Read(info.indexBits - (isAnchor(pixel) ? 1 : 0));
    }
    if (info.indexBits2) {
        for (int pixel = 0; pixel < 16; ++pixel) {
            indices2[pixel] = bits.Read(info.indexBits2 - (pixel == 0 ? 1 : 0));
        }
    }

    for (int pixel = 0; pixel < 16; ++pixel) {
        const uint8_t* low = colors[subsetOf[pixel] * 2];
        const uint8_t* high = colors[subsetOf[pixel] * 2 + 1];
        int colorWeight = Weight(info.indexBits, indices[pixel]);
        int alphaWeight = colorWeight;
        if (info.indexBits2) {
            // Mode 4's selector swaps which index set drives color.
            const int second = Weight(info.indexBits2, indices2[pixel]);
            if (selector) {
                colorWeight = second;
            } else {
                alphaWeight = second;
            }
        }
        uint8_t rgba[4] = {};
        for (int channel = 0; channel < 3; ++channel) {
            rgba[channel] = Interpolate(low[channel], high[channel], colorWeight);
        }
        rgba[3] = Interpolate(low[3], high[3], alphaWeight);
        if (rotation) {
            std::swap(rgba[3], rgba[rotation - 1]);
        }
        uint8_t* out = pixels + pixel * 4;
        out[0] = rgba[2];
        out[1] = rgba[1];
        out[2] = rgba[0];
        out[3] = rgba[3];
    }
}

bool DecodeBc7(
    const uint8_t* data, size_t size, uint32_t width, uint32_t height, std::vector<uint8_t>& pixels, std::string& error
) {
    if (width == 0 || height == 0) {
        error = "BC7: the size " + Size(width, height) + " is empty";
        return false;
    }
    const size_t blocksWide = (static_cast<size_t>(width) + 3) / 4;
    const size_t blocksHigh = (static_cast<size_t>(height) + 3) / 4;
    const size_t needed = blocksWide * blocksHigh * 16;
    if (!data || size < needed) {
        error = "BC7: " + std::to_string(size) + " bytes of blocks, a " + Size(width, height) + " image needs " +
                std::to_string(needed);
        return false;
    }
    pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    uint8_t tile[64] = {};
    for (size_t blockY = 0; blockY < blocksHigh; ++blockY) {
        const size_t rows = std::min<size_t>(4, height - blockY * 4);
        for (size_t blockX = 0; blockX < blocksWide; ++blockX) {
            DecodeBc7Block(data + (blockY * blocksWide + blockX) * 16, tile);
            const size_t columns = std::min<size_t>(4, width - blockX * 4);
            for (size_t row = 0; row < rows; ++row) {
                const size_t target = ((blockY * 4 + row) * width + blockX * 4) * 4;
                std::memcpy(pixels.data() + target, tile + row * 16, columns * 4);
            }
        }
    }
    return true;
}

// ---- Files ---------------------------------------------------------------

bool ParseHeader(const uint8_t* file, size_t size, Header& header, std::string& error) {
    header = Header();
    if (!file || size < kHeaderSize) {
        error = "the file has " + std::to_string(size) + " bytes, fewer than the 128-byte NMPLTEX1 header";
        return false;
    }
    if (std::memcmp(file, kNltxMagic, kMagicSize) != 0) {
        error = "the file does not start with NMPLTEX1";
        return false;
    }
    header.version = ReadU32(file, 0x10);
    header.format = ReadU32(file, 0x14);
    header.kind = header.format & 0xFFFFF;
    header.bpp = header.format >> 20;
    header.width = ReadU16(file, 0x18);
    header.height = ReadU16(file, 0x1C);
    header.field20 = file[0x20];
    header.field21 = file[0x21];
    header.paletteCount = ReadU16(file, 0x22);
    header.field24 = file[0x24];
    header.mips = file[0x25];
    header.compressed = file[0x26];
    header.arrays = file[0x27];
    header.field28 = ReadU16(file, 0x28);
    header.rawSize = ReadU32(file, 0x2C);
    header.payloadSize = ReadU32(file, 0x30);
    header.dataOffset = header.version == kFixedOffsetVersion ? static_cast<uint32_t>(kHeaderSize) : ReadU32(file, 0x34);
    header.dynamic = ReadU32(file, 0x38) != 0;

    if (header.version == kOldLayoutVersion) {
        error = "version 100 uses an older header layout, which is not supported";
        return false;
    }
    if (header.kind == 0 || header.bpp == 0) {
        error = "format " + Hex(header.format) + " has no kind or no bits per pixel";
        return false;
    }
    if (header.width == 0 || header.height == 0) {
        error = "the header gives the size " + Size(header.width, header.height) + "; both sides must be at least 1";
        return false;
    }
    if (header.dataOffset < kHeaderSize || header.dataOffset > size) {
        error = "the data offset " + Hex(header.dataOffset) + " is outside the " + std::to_string(size) +
                "-byte file or inside the header";
        return false;
    }
    const size_t available = size - header.dataOffset;
    if (header.compressed && available < kYkcmpHeaderSize) {
        error = "the YKCMP payload at " + Hex(header.dataOffset) + " is cut short";
        return false;
    }
    if (!header.compressed && header.rawSize > available) {
        error = "the stored payload of " + std::to_string(header.rawSize) + " bytes passes the end of the file";
        return false;
    }
    return true;
}

bool DecodePayload(const uint8_t* file, size_t size, Header& header, std::vector<uint8_t>& raw, std::string& error) {
    if (!ParseHeader(file, size, header, error)) {
        return false;
    }
    const uint8_t* payload = file + header.dataOffset;
    if (!header.compressed) {
        // Readable here, but the engine uploads such a file from its first
        // byte, header included.
        raw.assign(payload, payload + header.rawSize);
        return true;
    }
    return YkcmpUnpack(payload, size - header.dataOffset, raw, error);
}

bool Decode(
    const uint8_t* file, size_t size, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& error
) {
    Header header;
    if (!ParseHeader(file, size, header, error)) {
        return false;
    }
    if (header.format != kFormatBgra8 && header.format != kFormatBc7) {
        error = "format " + Hex(header.format) + " (kind " + std::to_string(header.kind) + ", " +
                std::to_string(header.bpp) + " bpp) is not supported, only kind 1 BGRA8 and kind 6 BC7 are";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!DecodePayload(file, size, header, raw, error)) {
        return false;
    }
    width = header.width;
    height = header.height;
    if (header.format == kFormatBc7) {
        return DecodeBc7(raw.data(), raw.size(), width, height, pixels, error);
    }
    const size_t needed = static_cast<size_t>(width) * height * 4;
    if (raw.size() < needed) {
        error = "the payload has " + std::to_string(raw.size()) + " bytes, a " + Size(width, height) +
                " BGRA8 image needs " + std::to_string(needed);
        return false;
    }
    pixels.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(needed));
    return true;
}

bool Encode(
    uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, std::vector<uint8_t>& file, std::string& error,
    Packing packing
) {
    if (width == 0 || height == 0 || width > 0xFFFF || height > 0xFFFF) {
        error = "the size " + Size(width, height) + " is outside 1 to 65535 per side";
        return false;
    }
    const uint64_t rawSize = static_cast<uint64_t>(width) * height * 4;
    if (rawSize > kMaxRawSize) {
        error = "a " + Size(width, height) + " image is over the 1 GiB limit";
        return false;
    }
    if (pixels.size() != rawSize) {
        error = std::to_string(pixels.size()) + " bytes of pixels given, a " + Size(width, height) +
                " BGRA8 image has " + std::to_string(rawSize);
        return false;
    }
    std::vector<uint8_t> block;
    if (packing == Packing::Lz4) {
        Lz4Encode(pixels.data(), pixels.size(), block);
        bool good = block.size() < pixels.size();
        if (good) {
            std::vector<uint8_t> check(pixels.size());
            size_t used = 0;
            std::string ignored;
            good = Lz4Decode(block.data(), block.size(), check.data(), check.size(), used, ignored) &&
                   used == block.size() && check == pixels;
        }
        if (!good) {
            block.clear();
        }
    }
    if (block.empty()) {
        Lz4EncodeLiterals(pixels.data(), pixels.size(), block);
    }
    std::vector<uint8_t> container;
    YkcmpPack(block, static_cast<uint32_t>(rawSize), container);

    file.assign(kHeaderSize, 0);
    std::memcpy(file.data(), kNltxMagic, kMagicSize);
    WriteU32(file.data(), 0x10, kVersion);
    WriteU32(file.data(), 0x14, kFormatBgra8);
    WriteU32(file.data(), 0x18, width);
    WriteU32(file.data(), 0x1C, height);
    std::memcpy(file.data() + 0x20, kHeaderTemplate, sizeof(kHeaderTemplate));
    WriteU32(file.data(), 0x2C, static_cast<uint32_t>(rawSize));
    WriteU32(file.data(), 0x30, static_cast<uint32_t>(container.size()));
    WriteU32(file.data(), 0x34, static_cast<uint32_t>(kHeaderSize));
    file.insert(file.end(), container.begin(), container.end());
    return true;
}

}  // namespace nltx
