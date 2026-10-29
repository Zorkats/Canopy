#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "image_io.h"
#include "nltx.h"

// Checks of the CanopyCore texture codec and image I/O. Exits nonzero when
// any check fails. Samples under _work are private research data: a missing
// folder prints a note and is never a failure.
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED line %d: %s\n", line, text);
    }
    return passed;
}

const fs::path kSourceDir = fs::u8path(CANOPY_SOURCE_DIR);
const fs::path kCoreRef = kSourceDir / "_work" / "scripts" / "studio_research" / "out" / "core_ref";

Bytes ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const Bytes& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes Text(const char* text) {
    return Bytes(text, text + std::strlen(text));
}

uint32_t U32(const Bytes& data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, 4);
    return value;
}

// The offset of the first difference, or -1 when equal.
long long FirstDifference(const Bytes& left, const Bytes& right) {
    const size_t count = std::min(left.size(), right.size());
    for (size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return static_cast<long long>(index);
        }
    }
    return left.size() == right.size() ? -1 : static_cast<long long>(count);
}

Bytes Gradient(uint32_t width, uint32_t height) {
    Bytes pixels(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* pixel = &pixels[(static_cast<size_t>(y) * width + x) * 4];
            pixel[0] = static_cast<uint8_t>(x);
            pixel[1] = static_cast<uint8_t>(y);
            pixel[2] = static_cast<uint8_t>((x + y) / 2);
            pixel[3] = static_cast<uint8_t>(255 - (x ^ y));
        }
    }
    return pixels;
}

Bytes Random(size_t size, uint32_t seed) {
    std::mt19937 engine(seed);
    Bytes data(size);
    for (uint8_t& byte : data) {
        byte = static_cast<uint8_t>(engine() & 0xFF);
    }
    return data;
}

bool Lz4RoundTrip(const Bytes& data) {
    Bytes block;
    nltx::Lz4Encode(data.data(), data.size(), block);
    Bytes back(data.size());
    size_t used = 0;
    std::string error;
    return nltx::Lz4Decode(block.data(), block.size(), back.data(), back.size(), used, error) &&
           used == block.size() && back == data;
}

// Writes values from bit 0 of byte 0 up, the BC7 bit order.
struct BitWriter {
    uint8_t bytes[16] = {};
    int position = 0;

    void Write(uint32_t value, int count) {
        for (int bit = 0; bit < count; ++bit, ++position) {
            if ((value >> bit) & 1) {
                bytes[position >> 3] |= static_cast<uint8_t>(1 << (position & 7));
            }
        }
    }
};

// ---- LZ4 and YKCMP -------------------------------------------------------

void TestLz4Vectors() {
    std::printf("LZ4 and YKCMP vectors\n");
    std::string error;
    size_t used = 0;

    // One literal-only sequence.
    const Bytes hello = {0x50, 'h', 'e', 'l', 'l', 'o'};
    Bytes out(5);
    CHECK(nltx::Lz4Decode(hello.data(), hello.size(), out.data(), out.size(), used, error));
    CHECK(out == Text("hello") && used == 6);

    // An overlapping match: offset 2, length 10.
    const Bytes overlap = {0x26, 'a', 'b', 0x02, 0x00, 0x50, 'h', 'e', 'l', 'l', 'o'};
    out.assign(17, 0);
    CHECK(nltx::Lz4Decode(overlap.data(), overlap.size(), out.data(), out.size(), used, error));
    CHECK(out == Text("abababababab" "hello") && used == overlap.size());

    // Extended lengths: 16 literals (15 + 1), a match of 15 + 255 + 0 + 4.
    Bytes extended = {0xFF, 0x01};
    for (char c = 'A'; c <= 'P'; ++c) {
        extended.push_back(static_cast<uint8_t>(c));
    }
    for (const uint8_t byte : Bytes{0x01, 0x00, 0xFF, 0x00, 0x50, 't', 'a', 'i', 'l', '!'}) {
        extended.push_back(byte);
    }
    Bytes expected = Text("ABCDEFGHIJKLMNOP");
    expected.insert(expected.end(), 274, 'P');
    for (const uint8_t byte : Text("tail!")) {
        expected.push_back(byte);
    }
    out.assign(expected.size(), 0);
    CHECK(nltx::Lz4Decode(extended.data(), extended.size(), out.data(), out.size(), used, error));
    CHECK(out == expected && used == extended.size());

    // Broken blocks: offset 0, offset past the output, cut input, a match
    // in the last 5 bytes, literals past the output.
    const Bytes brokenBlocks[] = {
        {0x10, 'a', 0x00, 0x00, 0x50, 'h', 'e', 'l', 'l', 'o'},
        {0x10, 'a', 0x02, 0x00, 0x50, 'h', 'e', 'l', 'l', 'o'},
        {0xF0},
        {0x84, '0', '1', '2', '3', '4', '5', '6', '7', 0x01, 0x00, 0x40, 'w', 'x', 'y', 'z'},
        {0x60, 'a', 'b', 'c', 'd', 'e', 'f'},
    };
    for (const Bytes& broken : brokenBlocks) {
        out.assign(broken.size() == 7 ? 5 : 20, 0);
        error.clear();
        CHECK(!nltx::Lz4Decode(broken.data(), broken.size(), out.data(), out.size(), used, error) && !error.empty());
    }

    // YKCMP type 9 around the overlap block, then with one stray byte that
    // the game would reject.
    Bytes container;
    nltx::YkcmpPack(overlap, 17, container);
    CHECK(container.size() == 0x14 + overlap.size() && std::memcmp(container.data(), "YKCMP_V1", 8) == 0);
    CHECK(U32(container, 8) == 9 && U32(container, 12) == container.size() && U32(container, 16) == 17);
    Bytes raw;
    CHECK(nltx::YkcmpUnpack(container.data(), container.size(), raw, error) && raw == Text("abababababab" "hello"));
    Bytes stray = container;
    stray.push_back(0);
    stray[12] = static_cast<uint8_t>(stray.size());
    CHECK(!nltx::YkcmpUnpack(stray.data(), stray.size(), raw, error));
    std::printf("  stray byte: %s\n", error.c_str());
    Bytes unknown = container;
    unknown[8] = 7;
    CHECK(!nltx::YkcmpUnpack(unknown.data(), unknown.size(), raw, error));
    std::printf("  type 7: %s\n", error.c_str());
    CHECK(!nltx::YkcmpUnpack(container.data(), 0x13, raw, error));

    // Type 4: 3 literals, then a short copy, a one-byte-distance copy and a
    // two-byte one.
    Bytes type4 = {'Y', 'K', 'C', 'M', 'P', '_', 'V', '1', 4, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0};
    for (const uint8_t byte : Bytes{0x03, 'a', 'b', 'c', 0x92, 0xC1, 0x04, 0xE0, 0x10, 0x07}) {
        type4.push_back(byte);
    }
    type4[12] = static_cast<uint8_t>(type4.size());
    CHECK(nltx::YkcmpUnpack(type4.data(), type4.size(), raw, error) && raw == Text("abcababcabca"));

    // Encoder edge sizes: every length from 0 to 80 in four patterns.
    bool all = true;
    for (size_t size = 0; size <= 80; ++size) {
        Bytes zeros(size, 0);
        Bytes cycle(size);
        Bytes mixed = Random(size, static_cast<uint32_t>(size));
        Bytes noise = Random(size, 1000 + static_cast<uint32_t>(size));
        for (size_t index = 0; index < size; ++index) {
            cycle[index] = static_cast<uint8_t>(index % 3);
            if (index % 7 < 4) {
                mixed[index] = 0x55;
            }
        }
        all = all && Lz4RoundTrip(zeros) && Lz4RoundTrip(cycle) && Lz4RoundTrip(mixed) && Lz4RoundTrip(noise);
    }
    CHECK(all);
    // A 70000-byte period puts every repeat past the 64 KiB window.
    const Bytes period = Random(70000, 5);
    Bytes distant = period;
    distant.insert(distant.end(), period.begin(), period.end());
    CHECK(Lz4RoundTrip(distant));
}

// ---- Header --------------------------------------------------------------

void TestHeaderTemplate() {
    std::printf("Kind 1 header template\n");
    const Bytes pixels = Gradient(3, 5);
    Bytes file;
    std::string error;
    CHECK(nltx::Encode(3, 5, pixels, file, error, nltx::Packing::Literals));

    // The game's kind 1 header, byte for byte.
    Bytes expected(0x80, 0);
    std::memcpy(expected.data(), "NMPLTEX1", 8);
    const uint32_t words[] = {0x66, 0x02000001, 3, 5};
    std::memcpy(expected.data() + 0x10, words, sizeof(words));
    const uint8_t fields[12] = {0x04, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::memcpy(expected.data() + 0x20, fields, sizeof(fields));
    const uint32_t raw = 3 * 5 * 4;
    const uint32_t total = 0x14 + 1 + 1 + raw;  // token, one length byte (60 - 15 = 45)
    const uint32_t sizes[] = {raw, total, 0x80};
    std::memcpy(expected.data() + 0x2C, sizes, sizeof(sizes));
    CHECK(file.size() == 0x80 + total);
    CHECK(Bytes(file.begin(), file.begin() + 0x80) == expected);

    // The literal-only block: 0xF0, (n - 15) in 255s, the bytes.
    CHECK(std::memcmp(file.data() + 0x80, "YKCMP_V1", 8) == 0);
    CHECK(U32(file, 0x88) == 9 && U32(file, 0x8C) == total && U32(file, 0x90) == raw);
    CHECK(file[0x94] == 0xF0 && file[0x95] == raw - 15);
    CHECK(Bytes(file.begin() + 0x96, file.end()) == pixels);

    Bytes big(300 * 4, 7);
    CHECK(nltx::Encode(300, 1, big, file, error, nltx::Packing::Literals));
    // 1200 - 15 = 1185 = 4 * 255 + 165.
    CHECK(file[0x94] == 0xF0 && file[0x95] == 255 && file[0x98] == 255 && file[0x99] == 165);

    nltx::Header header;
    CHECK(nltx::ParseHeader(file.data(), file.size(), header, error));
    CHECK(header.version == 0x66 && header.format == nltx::kFormatBgra8 && header.kind == 1 && header.bpp == 32);
    CHECK(header.width == 300 && header.height == 1 && header.mips == 1 && header.compressed == 1);
    CHECK(header.arrays == 0 && header.paletteCount == 0xFFFF && header.field20 == 4 && !header.dynamic);
    CHECK(header.rawSize == 1200 && header.dataOffset == 0x80 && header.payloadSize + 0x80 == file.size());

    // Header failures.
    CHECK(!nltx::ParseHeader(file.data(), 0x7F, header, error));
    Bytes bad = file;
    bad[0] = 'X';
    CHECK(!nltx::ParseHeader(bad.data(), bad.size(), header, error));
    bad = file;
    bad[0x10] = 100;
    CHECK(!nltx::ParseHeader(bad.data(), bad.size(), header, error));
    bad = file;
    bad[0x34] = 0x10;
    CHECK(!nltx::ParseHeader(bad.data(), bad.size(), header, error));
    bad = file;
    bad[0x18] = 0;
    bad[0x19] = 0;
    CHECK(!nltx::ParseHeader(bad.data(), bad.size(), header, error));
    std::printf("  zero width: %s\n", error.c_str());
    bad = file;
    bad[0x14] = 2;
    uint32_t width = 0;
    uint32_t height = 0;
    Bytes decoded;
    CHECK(!nltx::Decode(bad.data(), bad.size(), width, height, decoded, error));
    std::printf("  kind 2: %s\n", error.c_str());
    bad = file;
    bad.resize(bad.size() - 1);
    CHECK(!nltx::Decode(bad.data(), bad.size(), width, height, decoded, error));
    std::printf("  cut file: %s\n", error.c_str());

    // Encode refusals.
    CHECK(!nltx::Encode(0, 5, Bytes(), file, error));
    CHECK(!nltx::Encode(65536, 1, Bytes(65536 * 4), file, error));
    CHECK(!nltx::Encode(3, 5, Bytes(59), file, error));
}

// ---- Round trips ---------------------------------------------------------

void RoundTrip(const char* label, uint32_t width, uint32_t height, const Bytes& pixels) {
    for (const nltx::Packing packing : {nltx::Packing::Literals, nltx::Packing::Lz4}) {
        const auto start = std::chrono::steady_clock::now();
        Bytes file;
        std::string error;
        const bool encoded = nltx::Encode(width, height, pixels, file, error, packing);
        const auto middle = std::chrono::steady_clock::now();
        uint32_t decodedWidth = 0;
        uint32_t decodedHeight = 0;
        Bytes decoded;
        const bool ok = nltx::Decode(file.data(), file.size(), decodedWidth, decodedHeight, decoded, error);
        const auto end = std::chrono::steady_clock::now();
        CHECK(encoded && ok);
        CHECK(decodedWidth == width && decodedHeight == height && decoded == pixels);
        CHECK(U32(file, 0x2C) == pixels.size() && U32(file, 0x30) + 0x80 == file.size());
        const auto encodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(middle - start).count();
        const auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - middle).count();
        if (width * height >= 1024 * 1024) {
            CHECK(encodeMs < 20000 && decodeMs < 5000);
        }
        std::printf(
            "  %-9s %4ux%-4u %-8s %9zu -> %9zu bytes, encode %lld ms, decode %lld ms\n", label, width, height,
            packing == nltx::Packing::Lz4 ? "lz4" : "literals", pixels.size(), file.size(),
            static_cast<long long>(encodeMs), static_cast<long long>(decodeMs)
        );
    }
}

void TestRoundTrips() {
    std::printf("Encode and decode round trips\n");
    const uint32_t sizes[][2] = {{1, 1}, {3, 5}, {128, 128}, {333, 217}, {2048, 2048}};
    for (const auto& size : sizes) {
        RoundTrip("gradient", size[0], size[1], Gradient(size[0], size[1]));
        RoundTrip("random", size[0], size[1], Random(static_cast<size_t>(size[0]) * size[1] * 4, size[0]));
    }
}

// ---- BC7 -----------------------------------------------------------------

void TestBc7Blocks() {
    std::printf("BC7 hand-built blocks\n");
    uint8_t pixels[64] = {};

    // Mode 6, both endpoints (0x80, 0x42, 0x10, 0xFE) with p-bits 0: solid.
    BitWriter solid;
    solid.Write(1 << 6, 7);
    for (const uint32_t value : {0x40u, 0x21u, 0x08u, 0x7Fu}) {
        solid.Write(value, 7);
        solid.Write(value, 7);
    }
    solid.Write(0, 2);
    solid.Write(0x5A5A5A5A, 32);
    solid.Write(0x1234567, 31);
    CHECK(solid.position == 128);
    nltx::DecodeBc7Block(solid.bytes, pixels);
    bool same = true;
    for (int pixel = 0; pixel < 16; ++pixel) {
        same = same && pixels[pixel * 4] == 0x10 && pixels[pixel * 4 + 1] == 0x42 && pixels[pixel * 4 + 2] == 0x80 &&
               pixels[pixel * 4 + 3] == 0xFE;
    }
    CHECK(same);

    // Mode 6 ramp: endpoint 0 is 0 (p 0), endpoint 1 is 255 (127, p 1), and
    // pixel i uses index i, so every channel is (w4[i] * 255 + 32) >> 6.
    BitWriter ramp;
    ramp.Write(1 << 6, 7);
    for (int channel = 0; channel < 4; ++channel) {
        ramp.Write(0, 7);
        ramp.Write(127, 7);
    }
    ramp.Write(0, 1);
    ramp.Write(1, 1);
    ramp.Write(0, 3);
    for (uint32_t index = 1; index < 16; ++index) {
        ramp.Write(index, 4);
    }
    CHECK(ramp.position == 128);
    nltx::DecodeBc7Block(ramp.bytes, pixels);
    const int weights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    bool ramped = true;
    for (int pixel = 0; pixel < 16; ++pixel) {
        const uint8_t value = static_cast<uint8_t>((weights[pixel] * 255 + 32) >> 6);
        ramped = ramped && std::memcmp(pixels + pixel * 4, Bytes(4, value).data(), 4) == 0;
    }
    CHECK(ramped);
    CHECK(pixels[0] == 0 && pixels[60] == 255);

    // Mode 5, rotation 1 swaps red and alpha: red 0x7F (255), alpha 0x20.
    BitWriter rotated;
    rotated.Write(1 << 5, 6);
    rotated.Write(1, 2);
    for (const uint32_t value : {0x7Fu, 0u, 0u}) {
        rotated.Write(value, 7);
        rotated.Write(value, 7);
    }
    rotated.Write(0x20, 8);
    rotated.Write(0x20, 8);
    rotated.Write(0, 62);
    CHECK(rotated.position == 128);
    nltx::DecodeBc7Block(rotated.bytes, pixels);
    CHECK(pixels[0] == 0x00 && pixels[1] == 0x00 && pixels[2] == 0x20 && pixels[3] == 0xFF);
    CHECK(std::memcmp(pixels, pixels + 60, 4) == 0);

    // The reserved mode decodes to transparent black.
    const uint8_t reserved[16] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::memset(pixels, 0xAA, sizeof(pixels));
    nltx::DecodeBc7Block(reserved, pixels);
    CHECK(Bytes(pixels, pixels + 64) == Bytes(64, 0));

    // A surface crops partial blocks: 5x3 from 2x1 blocks.
    Bytes blocks(solid.bytes, solid.bytes + 16);
    blocks.insert(blocks.end(), rotated.bytes, rotated.bytes + 16);
    Bytes surface;
    std::string error;
    CHECK(nltx::DecodeBc7(blocks.data(), blocks.size(), 5, 3, surface, error) && surface.size() == 5 * 3 * 4);
    CHECK(surface[0] == 0x10 && surface[4 * 4 + 2] == 0x20 && surface[(2 * 5 + 4) * 4 + 3] == 0xFF);
    CHECK(!nltx::DecodeBc7(blocks.data(), 16, 5, 3, surface, error));
}

// ---- Image I/O -----------------------------------------------------------

void TestImageIo() {
#ifdef _WIN32
    std::printf("Image I/O through WIC\n");
#else
    std::printf("Image I/O through stb\n");
#endif
    const fs::path folder = fs::temp_directory_path() / "canopy_core_tests";
    fs::create_directories(folder);
    const fs::path png = folder / "round trip.png";
    std::string error;

    // Straight alpha: color under alpha 0 must come back unchanged.
    Bytes pixels = Random(67 * 41 * 4, 77);
    for (size_t index = 3; index < pixels.size(); index += 16) {
        pixels[index] = 0;
    }
    CHECK(image_io::SavePng(png.wstring(), 67, 41, pixels, error));
    uint32_t width = 0;
    uint32_t height = 0;
    Bytes loaded;
    CHECK(image_io::Load(png.wstring(), width, height, loaded, error));
    CHECK(width == 67 && height == 41 && loaded == pixels);

    // Replacing a bigger file leaves no tail behind.
    CHECK(image_io::SavePng(png.wstring(), 2, 2, Bytes(16, 0x80), error));
    CHECK(image_io::Load(png.wstring(), width, height, loaded, error) && width == 2 && loaded == Bytes(16, 0x80));

#ifdef _WIN32
    // A thread that already runs COM in the single-threaded model.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CHECK(SUCCEEDED(com));
    CHECK(image_io::Load(png.wstring(), width, height, loaded, error) && width == 2);
    CHECK(image_io::SavePng((folder / "apartment.png").wstring(), 1, 1, Bytes(4, 9), error));
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
#else
    // No COM here: the same read and write, and the written file read back.
    CHECK(image_io::Load(png.wstring(), width, height, loaded, error) && width == 2);
    CHECK(image_io::SavePng((folder / "apartment.png").wstring(), 1, 1, Bytes(4, 9), error));
    CHECK(image_io::Load((folder / "apartment.png").wstring(), width, height, loaded, error) && loaded == Bytes(4, 9));
#endif

    CHECK(!image_io::Load((folder / "missing.png").wstring(), width, height, loaded, error) && !error.empty());
    std::printf("  missing file: %s\n", error.c_str());
    WriteBytes(folder / "garbage.png", Text("not an image at all"));
    CHECK(!image_io::Load((folder / "garbage.png").wstring(), width, height, loaded, error));
    CHECK(!image_io::SavePng(png.wstring(), 2, 2, Bytes(15), error));

    // A 2x2 24-bit BMP, its bottom row stored first: B, G, R in, opaque out.
    const Bytes bmp = {
        'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 16, 0, 0, 0,
        0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        7, 8, 9, 10, 11, 12, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0,
    };
    WriteBytes(folder / "small.bmp", bmp);
    CHECK(image_io::Load((folder / "small.bmp").wstring(), width, height, loaded, error) && width == 2 &&
          height == 2 && loaded == Bytes({1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255}));
#ifndef _WIN32
    // A 2x1 32-bit TGA, top row first, which stb reads and WIC does not.
    Bytes tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 0x28};
    for (uint8_t value = 1; value <= 8; ++value) {
        tga.push_back(value);
    }
    WriteBytes(folder / "small.tga", tga);
    CHECK(image_io::Load((folder / "small.tga").wstring(), width, height, loaded, error) && width == 2 &&
          height == 1 && loaded == Bytes({1, 2, 3, 4, 5, 6, 7, 8}));
#endif
    fs::remove_all(folder);
}

// ---- Real data -----------------------------------------------------------

void TestSampleFolders() {
    std::printf("Sample texture folders\n");
    const fs::path folders[] = {
        kSourceDir / "_work" / "x" / "data" / "texture",
        kSourceDir / "_work" / "re_reports" / "content_items" / "tex" / "data" / "texture",
    };
    for (const fs::path& folder : folders) {
        std::error_code code;
        if (!fs::is_directory(folder, code)) {
            std::printf("  skipped, not present: %s\n", folder.u8string().c_str());
            continue;
        }
        int decoded = 0;
        int failed = 0;
        for (const auto& entry : fs::recursive_directory_iterator(folder, code)) {
            if (decoded + failed >= 200 || entry.path().extension() != ".nltx") {
                continue;
            }
            const Bytes file = ReadBytes(entry.path());
            uint32_t width = 0;
            uint32_t height = 0;
            Bytes pixels;
            std::string error;
            if (nltx::Decode(file.data(), file.size(), width, height, pixels, error) &&
                pixels.size() == static_cast<size_t>(width) * height * 4) {
                ++decoded;
            } else {
                ++failed;
                std::printf("  %s: %s\n", entry.path().filename().u8string().c_str(), error.c_str());
            }
        }
        CHECK(failed == 0);
        std::printf("  %d decoded, %d failed in %s\n", decoded, failed, folder.u8string().c_str());
    }
}

// Reference dumps made outside Canopy with a Python YKCMP codec and Pillow,
// kept with the private research data.
void TestCrossCheck() {
    std::printf("Cross-check against ykcmp9.py (core_ref)\n");
    std::ifstream manifest(kCoreRef / "manifest.txt");
    if (!manifest) {
        std::printf("  skipped, run python -I _work/scripts/studio_research/core_ref.py make first\n");
        return;
    }
    int matched = 0;
    int total = 0;
    std::string line;
    while (std::getline(manifest, line)) {
        std::istringstream words(line);
        std::string check;
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        words >> check >> name >> width >> height;
        if (check.empty()) {
            continue;
        }
        Bytes actual;
        Bytes expected;
        std::string error;
        bool ok = false;
        if (check == "bgra") {
            const Bytes file = ReadBytes(kCoreRef / (name + ".nltx"));
            uint32_t decodedWidth = 0;
            uint32_t decodedHeight = 0;
            ok = nltx::Decode(file.data(), file.size(), decodedWidth, decodedHeight, actual, error) &&
                 decodedWidth == width && decodedHeight == height;
            expected = ReadBytes(kCoreRef / (name + ".bgra"));
        } else if (check == "payload") {
            const Bytes file = ReadBytes(kCoreRef / (name + ".nltx"));
            nltx::Header header;
            ok = nltx::DecodePayload(file.data(), file.size(), header, actual, error);
            expected = ReadBytes(kCoreRef / (name + ".payload"));
        } else if (check == "bc7") {
            const Bytes blocks = ReadBytes(kCoreRef / (name + ".blocks"));
            ok = nltx::DecodeBc7(blocks.data(), blocks.size(), width, height, actual, error);
            expected = ReadBytes(kCoreRef / (name + ".bgra"));
        } else if (check == "png") {
            uint32_t loadedWidth = 0;
            uint32_t loadedHeight = 0;
            ok = image_io::Load((kCoreRef / (name + ".png")).wstring(), loadedWidth, loadedHeight, actual, error) &&
                 loadedWidth == width && loadedHeight == height;
            expected = ReadBytes(kCoreRef / (name + ".bgra"));
        }
        const long long difference = FirstDifference(actual, expected);
        const bool equal = ok && !expected.empty() && difference < 0;
        ++total;
        matched += equal ? 1 : 0;
        CHECK(equal);
        if (equal) {
            std::printf("  %-7s %-28s %zu bytes equal\n", check.c_str(), name.c_str(), actual.size());
        } else {
            std::printf(
                "  %-7s %-28s MISMATCH at byte %lld %s\n", check.c_str(), name.c_str(), difference, error.c_str()
            );
        }
    }
    std::printf("  %d of %d reference dumps equal\n", matched, total);

    // Files for core_ref.py verify, which decodes them with ykcmp9.py and
    // the game's lz4.dll.
    const struct {
        const char* name;
        uint32_t width;
        uint32_t height;
        Bytes pixels;
    } encodes[] = {
        {"cpp_encoded_gradient", 300, 200, Gradient(300, 200)},
        {"cpp_encoded_wide", 2048, 512, Gradient(2048, 512)},
        {"cpp_encoded_solid", 7, 3, Bytes(7 * 3 * 4, 0x33)},
        {"cpp_encoded_random", 3, 5, Random(3 * 5 * 4, 3)},
        {"cpp_encoded_real", 256, 512, ReadBytes(kCoreRef / "k6_gim_100170_02.bgra")},
    };
    for (const auto& encode : encodes) {
        Bytes file;
        std::string error;
        if (encode.pixels.size() != static_cast<size_t>(encode.width) * encode.height * 4) {
            continue;
        }
        CHECK(nltx::Encode(encode.width, encode.height, encode.pixels, file, error));
        WriteBytes(kCoreRef / (std::string(encode.name) + ".nltx"), file);
        WriteBytes(kCoreRef / (std::string(encode.name) + ".bgra"), encode.pixels);
    }
    std::printf("  wrote cpp_encoded_*.nltx, check with core_ref.py verify\n");
}

}  // namespace

// The table blob and schema checks, in table_tests.cpp.
void TestTables(int& checks, int& failures);
// The canopy.ini rules checks, in rules_tests.cpp.
void TestRules(int& checks, int& failures);
// The game browse checks, in browse_tests.cpp.
void TestBrowse(int& checks, int& failures);
// The canopy.ini editing checks, in ini_document_tests.cpp.
void TestIniDocument(int& checks, int& failures);
// The game asset checks, in game_assets_tests.cpp.
void TestGameAssets(int& checks, int& failures);
// The turned-off mods list checks, in disabled_mods_tests.cpp.
void TestDisabledMods(int& checks, int& failures);
// The project check, in rules_project_tests.cpp.
void TestRulesProject(int& checks, int& failures);
// The platform layer checks, in platform_tests.cpp.
void TestPlatform(int& checks, int& failures);
// The [patch.] rules and row patching, in patch_tests.cpp.
void TestPatches(int& checks, int& failures);
// The [row.] rules on generated tables, in rows_tests.cpp.
void TestRows(int& checks, int& failures);
// The [offering.], [catch.] and [forage.] list additions, in append_tests.cpp.
void TestAppends(int& checks, int& failures);
// The [sale.] and [upgrade.] rules, in shop_tests.cpp.
void TestShops(int& checks, int& failures);
void TestCrops(int& checks, int& failures);
// The game address resolver, in address_tests.cpp.
void TestAddresses(int& checks, int& failures);

int main() {
    TestLz4Vectors();
    TestHeaderTemplate();
    TestRoundTrips();
    TestBc7Blocks();
    TestImageIo();
    TestSampleFolders();
    TestCrossCheck();
    TestTables(g_checks, g_failures);
    TestRules(g_checks, g_failures);
    TestBrowse(g_checks, g_failures);
    TestIniDocument(g_checks, g_failures);
    TestGameAssets(g_checks, g_failures);
    TestDisabledMods(g_checks, g_failures);
    TestRulesProject(g_checks, g_failures);
    TestPlatform(g_checks, g_failures);
    TestPatches(g_checks, g_failures);
    TestRows(g_checks, g_failures);
    TestAppends(g_checks, g_failures);
    TestShops(g_checks, g_failures);
    TestCrops(g_checks, g_failures);
    TestAddresses(g_checks, g_failures);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
