#define NOMINMAX
#include "image_io.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <utility>

namespace image_io {
namespace {

using Microsoft::WRL::ComPtr;

// The largest image either way, 1 GiB of pixels, which also keeps every
// size WIC takes inside a UINT.
constexpr uint64_t kMaxBytes = 1ull << 30;

// COM for the length of one call. RPC_E_CHANGED_MODE means the thread
// already runs COM in the other apartment model, which WIC works with, so
// it is not a failure. Only a call that started COM ends it.
class ComScope {
public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    bool Ready() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }
    HRESULT Result() const {
        return result_;
    }

private:
    HRESULT result_;
};

std::string Failure(const char* what, HRESULT result) {
    char code[16] = {};
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
    return std::string(what) + " (HRESULT " + code + ")";
}

bool CreateFactory(ComPtr<IWICImagingFactory>& factory, std::string& error) {
    const HRESULT result =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        error = Failure("WIC is not available", result);
        return false;
    }
    return true;
}

// Every step of writing the PNG. created turns true once the file exists,
// so the caller can remove a partial one after these objects are released.
bool WritePng(
    IWICImagingFactory* factory, const std::wstring& path, uint32_t width, uint32_t height,
    const std::vector<uint8_t>& pixels, bool& created, std::string& error
) {
    ComPtr<IWICStream> stream;
    HRESULT result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) {
        result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    }
    if (FAILED(result)) {
        error = Failure("the file could not be created", result);
        return false;
    }
    created = true;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(result)) {
        result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(result)) {
        result = encoder->CreateNewFrame(&frame, &options);
    }
    if (SUCCEEDED(result)) {
        result = frame->Initialize(options.Get());
    }
    if (SUCCEEDED(result)) {
        result = frame->SetSize(width, height);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) {
        result = frame->SetPixelFormat(&format);
    }
    if (FAILED(result)) {
        error = Failure("the PNG encoder could not be set up", result);
        return false;
    }
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        error = "the PNG encoder does not take 32-bit BGRA pixels";
        return false;
    }
    // WritePixels takes a non-const pointer but only reads from it.
    const UINT stride = width * 4;
    result = frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()));
    if (SUCCEEDED(result)) {
        result = frame->Commit();
    }
    if (SUCCEEDED(result)) {
        result = encoder->Commit();
    }
    if (FAILED(result)) {
        error = Failure("the PNG could not be written", result);
        return false;
    }
    return true;
}

}  // namespace

bool Load(
    const std::wstring& path, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& error
) {
    ComScope com;
    if (!com.Ready()) {
        error = Failure("COM could not start", com.Result());
        return false;
    }
    ComPtr<IWICImagingFactory> factory;
    if (!CreateFactory(factory, error)) {
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT result =
        factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(result)) {
        error = Failure("the image could not be opened, or WIC does not know its format", result);
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    result = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(result)) {
        result = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(result)) {
        // 32bppBGRA is straight alpha; the premultiplied one is 32bppPBGRA.
        result = converter->Initialize(
            frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom
        );
    }
    if (FAILED(result)) {
        error = Failure("the image could not be converted to BGRA8", result);
        return false;
    }
    UINT frameWidth = 0;
    UINT frameHeight = 0;
    result = converter->GetSize(&frameWidth, &frameHeight);
    if (FAILED(result)) {
        error = Failure("the image size could not be read", result);
        return false;
    }
    const uint64_t bytes = static_cast<uint64_t>(frameWidth) * frameHeight * 4;
    if (bytes == 0 || bytes > kMaxBytes) {
        error = "the image is " + std::to_string(frameWidth) + "x" + std::to_string(frameHeight) +
                ", which is empty or over the 1 GiB limit";
        return false;
    }
    std::vector<uint8_t> buffer(static_cast<size_t>(bytes));
    result = converter->CopyPixels(nullptr, frameWidth * 4, static_cast<UINT>(bytes), buffer.data());
    if (FAILED(result)) {
        error = Failure("the image pixels could not be decoded", result);
        return false;
    }
    width = frameWidth;
    height = frameHeight;
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
    ComScope com;
    if (!com.Ready()) {
        error = Failure("COM could not start", com.Result());
        return false;
    }
    ComPtr<IWICImagingFactory> factory;
    if (!CreateFactory(factory, error)) {
        return false;
    }
    bool created = false;
    if (!WritePng(factory.Get(), path, width, height, pixels, created, error)) {
        if (created) {
            DeleteFileW(path.c_str());
        }
        return false;
    }
    return true;
}

}  // namespace image_io
