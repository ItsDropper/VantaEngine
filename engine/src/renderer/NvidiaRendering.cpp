#include "vanta/renderer/NvidiaRendering.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <iomanip>
#include <sstream>

namespace vanta::renderer
{
NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities() noexcept
{
    NvidiaRenderingCapabilities result{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    const HRESULT factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(factoryResult)) {
        result.diagnostic = "DXGI factory creation failed (HRESULT " + std::to_string(
            static_cast<unsigned long>(factoryResult)) + ").";
        return result;
    }

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapterResult = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()));
        if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(adapterResult)) continue;

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(device.GetAddressOf())))) {
            continue;
        }

        result.d3d12Available = true;
        result.nvidiaAdapter = description.VendorId == 0x10DE;
        result.adapterName.assign(description.Description);
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))) {
            result.raytracingTier = static_cast<std::uint32_t>(options.RaytracingTier);
            result.raytracingSupported = options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        }

        std::ostringstream message;
        message << "D3D12 adapter: " << result.adapterName
                << "; NVIDIA: " << (result.nvidiaAdapter ? "yes" : "no")
                << "; DXR: " << (result.raytracingSupported ? "supported (tier " +
                    std::to_string(result.raytracingTier) + ")" : "not supported");
        result.diagnostic = message.str();
        return result;
    }

    result.diagnostic = "No hardware D3D12 adapter was found.";
    return result;
}
} // namespace vanta::renderer
