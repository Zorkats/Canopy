#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Image files. On Windows through the Windows Imaging Component
// (image_io.cpp): each call initializes COM for itself and works on a thread
// that already has COM in either apartment model. Elsewhere through
// stb_image and stb_image_write (image_io_stb.cpp). Pixels are 4 bytes in
// B, G, R, A order, straight alpha, rows top first with no padding, the same
// layout nltx uses, and an image holds at most 1 GiB of them. Every function
// returns false and says why in error when it fails.
namespace image_io {

// Windows: any format WIC can decode (PNG, BMP, JPEG, TIFF, GIF and so on).
// Elsewhere: PNG, BMP, JPEG, TGA and GIF. The first frame only, converted
// to straight-alpha BGRA8.
bool Load(
    const std::wstring& path, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& error
);

// Writes a 32-bit PNG with alpha, replacing any file at path.
bool SavePng(
    const std::wstring& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, std::string& error
);

}  // namespace image_io
