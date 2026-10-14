#include "vanta/renderer/WicTextureLoader.hpp"

#include "vanta/core/Log.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <utility>

namespace vanta::renderer
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
        const unsigned long long byteCount = static_cast<unsigned long long>(width) * height * 4ULL;
        if (byteCount > std::numeric_limits<UINT>::max() ||
            byteCount > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) break;

        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))) break;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;

        CpuTexture texture;
        texture.width = width;
        texture.height = height;
        texture.rgba.resize(static_cast<std::size_t>(byteCount));
        if (FAILED(converter->CopyPixels(nullptr, width * 4U, static_cast<UINT>(byteCount),
                texture.rgba.data()))) break;
        output = std::move(texture);
        success = true;
    } while (false);

    if (!success) LogError(("Could not decode texture: " + path.string()).c_str());
    return success;
}
} // namespace vanta::renderer
