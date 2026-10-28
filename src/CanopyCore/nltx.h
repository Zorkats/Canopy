#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The game's texture file, NMPLTEX1 (.nltx): a 128-byte header, then the
// pixel payload at the data offset, normally wrapped in a YKCMP_V1
// container. Decodes kind 1 (BGRA8) and kind 6 (BC7) to BGRA8, and writes
// kind 1 files the way the game loads them. Pixels everywhere are 4 bytes in
// B, G, R, A order, straight alpha, rows top first with no padding. Nothing
// here touches game memory, so the desktop tools link it too. Every function
// that can fail returns false and says why in error.
namespace nltx {

constexpr size_t kHeaderSize = 0x80;
constexpr size_t kYkcmpHeaderSize = 0x14;
// Format codes: kind in the low 20 bits, bits per pixel above.
constexpr uint32_t kFormatBgra8 = 0x02000001;
constexpr uint32_t kFormatBc7 = 0x00800006;
// The largest raw size Decode accepts, 1 GiB.
constexpr uint32_t kMaxRawSize = 1u << 30;

// Every header field the engine's parser reads, plus the two it skips.
struct Header {
    uint32_t version = 0;       // +0x10, 0x66 in vanilla files
    uint32_t format = 0;        // +0x14
    uint32_t kind = 0;          // format & 0xFFFFF
    uint32_t bpp = 0;           // format >> 20
    uint32_t width = 0;         // +0x18, read as u16
    uint32_t height = 0;        // +0x1C, read as u16
    uint8_t field20 = 0;        // +0x20, not read, 4 in vanilla files
    uint8_t field21 = 0;        // +0x21
    uint16_t paletteCount = 0;  // +0x22, CLUT formats only, 255 means 256
    uint8_t field24 = 0;        // +0x24
    uint8_t mips = 0;           // +0x25, 0 and 1 both mean one level
    uint8_t compressed = 0;     // +0x26, 1 when the payload is YKCMP
    uint8_t arrays = 0;         // +0x27, 0 and 1 both mean one layer
    uint16_t field28 = 0;       // +0x28
    uint32_t rawSize = 0;       // +0x2C, decompressed payload bytes
    uint32_t payloadSize = 0;   // +0x30, not read, the YKCMP total in vanilla
    uint32_t dataOffset = 0;    // +0x34, forced to 0x80 by version 101
    bool dynamic = false;       // +0x38 nonzero, DYNAMIC usage
};

// How Encode packs the payload. Lz4 falls back to Literals when the
// compressed block is not smaller or does not decode back.
enum class Packing { Literals, Lz4 };

// ---- LZ4 blocks ----------------------------------------------------------
// The raw block format of types 8 and 9, held to the end-of-block rules of
// LZ4_decompress_fast, which the engine calls.

// Decodes until rawSize bytes are written to output, which has room for
// rawSize. used is the input bytes read; YKCMP requires all of them.
bool Lz4Decode(
    const uint8_t* input, size_t inputSize, uint8_t* output, size_t rawSize, size_t& used, std::string& error
);
// One sequence holding every byte as a literal. Always valid.
void Lz4EncodeLiterals(const uint8_t* input, size_t size, std::vector<uint8_t>& block);
// Greedy matches from a hash chain over a 64 KiB window.
void Lz4Encode(const uint8_t* input, size_t size, std::vector<uint8_t>& block);

// ---- YKCMP_V1 ------------------------------------------------------------
// char[8] magic, u32 type, u32 total size with the 20-byte header, u32 raw
// size. Types 8 and 9 are LZ4 blocks, type 4 is the opcode stream of the
// save files. The game also knows 2, 7 and 10, which are not supported.

bool YkcmpUnpack(const uint8_t* data, size_t size, std::vector<uint8_t>& raw, std::string& error);
// A type 9 container of block, which decodes to rawSize bytes.
void YkcmpPack(const std::vector<uint8_t>& block, uint32_t rawSize, std::vector<uint8_t>& container);

// ---- BC7 -----------------------------------------------------------------

// One 16-byte block to 4x4 pixels, 64 bytes, rows top first. The reserved
// mode (a zero first byte) decodes to transparent black.
void DecodeBc7Block(const uint8_t* block, uint8_t* pixels);
// A whole surface of ceil(width/4) x ceil(height/4) blocks, cropped to
// width x height.
bool DecodeBc7(
    const uint8_t* data, size_t size, uint32_t width, uint32_t height, std::vector<uint8_t>& pixels, std::string& error
);

// ---- Files ---------------------------------------------------------------

// Reads and checks the header of the size bytes at file.
bool ParseHeader(const uint8_t* file, size_t size, Header& header, std::string& error);
// The decompressed payload: every subresource, array-major then by mip.
bool DecodePayload(const uint8_t* file, size_t size, Header& header, std::vector<uint8_t>& raw, std::string& error);
// The top level of the first layer as BGRA8. Kind 1 and kind 6 only.
bool Decode(
    const uint8_t* file, size_t size, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& error
);
// A kind 1 file of width x height BGRA8 pixels, payload in YKCMP type 9.
// Both sides run from 1 to 65535.
bool Encode(
    uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, std::vector<uint8_t>& file, std::string& error,
    Packing packing = Packing::Lz4
);

}  // namespace nltx
