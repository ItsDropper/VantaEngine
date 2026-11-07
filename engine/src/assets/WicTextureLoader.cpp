#include "vanta/assets/WicTextureLoader.hpp"

#include "vanta/core/Log.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <utility>

namespace vanta::assets
{
bool LoadWicTexture(const std::filesystem::path& path, CpuTexture& output)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsComInitialization = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        LogError("Could not initialize COM for image decoding.");
        return false;
    }
    struct ApartmentScope {
        bool owns;
        ~ApartmentScope() { if (owns) CoUninitialize(); }
    } apartment{ownsComInitialization};

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    bool success = false;

    do {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf())))) break;
        if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) break;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) break;

        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) break;
        const unsigned long long rowBytes = static_cast<unsigned long long>(width) * 4ULL;
        if (rowBytes > std::numeric_limits<UINT>::max() ||
            static_cast<unsigned long long>(height) >
                static_cast<unsigned long long>(std::numeric_limits<UINT>::max()) / rowBytes)
            break;
        const unsigned long long byteCount = rowBytes * height;
        if (byteCount > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) break;

        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))) break;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;

        CpuTexture texture;
        texture.width = width;
        texture.height = height;
        texture.rgba.resize(static_cast<std::size_t>(byteCount));
        if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(rowBytes), static_cast<UINT>(byteCount),
                texture.rgba.data()))) break;
        output = std::move(texture);
        success = true;
    } while (false);

    if (!success) LogError(("Could not decode texture: " + path.string()).c_str());
    return success;
}

bool LoadWicTexture(std::span<const std::uint8_t> encodedBytes, CpuTexture& output,
                   std::string& error)
{
    error.clear();
    if (encodedBytes.empty() || encodedBytes.size() > std::numeric_limits<DWORD>::max())
    {
        error = "Embedded image payload is empty or exceeds the WIC stream size limit.";
        return false;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsComInitialization = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
    {
        error = "Could not initialize COM for embedded image decoding.";
        return false;
    }
    struct ApartmentScope
    {
        bool owns;
        ~ApartmentScope() { if (owns) CoUninitialize(); }
    } apartment{ownsComInitialization};

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(stream.GetAddressOf())) ||
        FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(encodedBytes.data())),
            static_cast<DWORD>(encodedBytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
            WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) ||
        FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
    {
        error = "WIC could not open the embedded image stream.";
        return false;
    }

    UINT width = 0U;
    UINT height = 0U;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0U || height == 0U)
    {
        error = "Embedded image has invalid dimensions.";
        return false;
    }
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 4ULL;
    if (rowBytes > std::numeric_limits<UINT>::max() ||
        static_cast<std::uint64_t>(height) >
            static_cast<std::uint64_t>(std::numeric_limits<UINT>::max()) / rowBytes)
    {
        error = "Embedded image dimensions exceed WIC RGBA8 limits.";
        return false;
    }
    const std::uint64_t byteCount = rowBytes * height;
    if (byteCount > std::numeric_limits<std::size_t>::max())
    {
        error = "Embedded image allocation exceeds the addressable memory range.";
        return false;
    }
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        error = "WIC could not convert embedded image pixels to RGBA8.";
        return false;
    }

    CpuTexture candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.rgba.resize(static_cast<std::size_t>(byteCount));
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(rowBytes),
                                     static_cast<UINT>(byteCount), candidate.rgba.data())))
    {
        error = "WIC failed while decoding embedded image pixels.";
        return false;
    }
    output = std::move(candidate);
    return true;
}

} // namespace vanta::assets
