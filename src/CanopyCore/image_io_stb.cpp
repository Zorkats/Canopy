#include "image_io.h"

#include <cstring>
#include <limits>
#include <utility>

#include "platform_files.h"

// image_io off Windows, through stb_image and stb_image_write. This is the
// one file that compiles their implementations, and the warnings of their
// code are silenced here only.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace image_io {
namespace {

// The largest image either way, 1 GiB of pixels, the limit the WIC side
// has too.
constexpr uint64_t kMaxBytes = 1ull << 30;
// stb reads from memory with an int length.
constexpr uint64_t kMaxFileBytes = static_cast<uint64_t>(std::numeric_limits<int>::max());

// R and B traded in place: RGBA8 to BGRA8 and back.
void SwapRedBlue(uint8_t* pixels, size_t bytes) {
    for (size_t at = 0; at + 3 < bytes; at += 4) {
        std::swap(pixels[at], pixels[at + 2]);
    }
}

void Append(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

}  // namespace

bool Load(
    const std::wstring& path, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& error
) {
    std::vector<uint8_t> file;
    if (!platform::ReadWholeFile(path, kMaxFileBytes, file)) {
        error = "the image could not be opened";
        return false;
    }
    int frameWidth = 0;
    int frameHeight = 0;
    int channels = 0;
    // Header only first, so a huge image is refused before it is decoded.
    if (!stbi_info_from_memory(file.data(), static_cast<int>(file.size()), &frameWidth, &frameHeight, &channels)) {
        error = std::string("the image's format is not one this build reads (") + stbi_failure_reason() + ")";
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(frameWidth) * static_cast<uint64_t>(frameHeight) * 4;
    if (frameWidth <= 0 || frameHeight <= 0 || bytes > kMaxBytes) {
        error = "the image is " + std::to_string(frameWidth) + "x" + std::to_string(frameHeight) +
                ", which is empty or over the 1 GiB limit";
        return false;
    }
    // GIF: the first frame. Straight alpha, as stb never premultiplies.
    stbi_uc* decoded = stbi_load_from_memory(
        file.data(), static_cast<int>(file.size()), &frameWidth, &frameHeight, &channels, 4
    );
    if (!decoded) {
        error = std::string("the image pixels could not be decoded (") + stbi_failure_reason() + ")";
        return false;
    }
    std::vector<uint8_t> buffer(decoded, decoded + bytes);
    stbi_image_free(decoded);
    SwapRedBlue(buffer.data(), buffer.size());
    width = static_cast<uint32_t>(frameWidth);
    height = static_cast<uint32_t>(frameHeight);
    pixels = std::move(buffer);
    return true;
}

bool SavePng(
    const std::wstring& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels, std::string& error
) {
    const uint64_t bytes = static_cast<uint64_t>(width) * height * 4;
    if (bytes == 0 || bytes > kMaxBytes) {
        error = "the size " + std::to_string(width) + "x" + std::to_string(height) + " is empty or over the 1 GiB limit";
        return false;
    }
    if (pixels.size() != bytes) {
        error = std::to_string(pixels.size()) + " bytes of pixels given, the size needs " + std::to_string(bytes);
        return false;
    }
    if (width > static_cast<uint32_t>(std::numeric_limits<int>::max() / 4) ||
        height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        error = "the size " + std::to_string(width) + "x" + std::to_string(height) + " is too large for the encoder";
        return false;
    }
    std::vector<uint8_t> rgba = pixels;
    SwapRedBlue(rgba.data(), rgba.size());
    std::vector<uint8_t> png;
    const int stride = static_cast<int>(width * 4);
    if (!stbi_write_png_to_func(
            &Append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(), stride
        ) ||
        png.empty()) {
        error = "the PNG could not be encoded";
        return false;
    }
    // WriteWholeFile removes a file it could not write whole.
    return platform::WriteWholeFile(path, png.data(), png.size(), error);
}

}  // namespace image_io
