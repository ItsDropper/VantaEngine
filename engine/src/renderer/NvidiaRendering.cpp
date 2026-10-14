#include "vanta/renderer/NvidiaRendering.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace vanta::renderer
{
namespace
{
const char* VendorName(const UINT vendorId)
{
    switch (vendorId) {
    case 0x10DE: return "NVIDIA";
    case 0x1002:
    case 0x1022: return "AMD";
    case 0x8086: return "Intel";
    case 0x1414: return "Microsoft";
    default: return "Other";
    }
}

void AddUnique(std::vector<std::string>& values, const char* value)
{
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.emplace_back(value);
    }
}
} // namespace

NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities()
{
    NvidiaRenderingCapabilities result{};
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    const HRESULT factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(factoryResult)) {
        result.diagnostic = "DXGI factory creation failed (HRESULT " +
            std::to_string(static_cast<unsigned long>(factoryResult)) + ").";
        return result;
    }

    std::ostringstream adapters;
    bool foundHardwareAdapter = false;
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapterResult = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()));
        if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(adapterResult)) continue;

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        const char* vendor = VendorName(description.VendorId);
        if (description.VendorId == 0x10DE) {
            result.nvidiaAdapter = true;
            AddUnique(result.availableVendors, vendor);
        } else if (description.VendorId == 0x1002 || description.VendorId == 0x1022) {
            result.amdAdapter = true;
            AddUnique(result.availableVendors, vendor);
        } else if (description.VendorId == 0x8086) {
            result.intelAdapter = true;
            AddUnique(result.availableVendors, vendor);
        } else {
            AddUnique(result.availableVendors, vendor);
        }

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(device.GetAddressOf())))) {
            continue;
        }

        char adapterName[128]{};
        (void)WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            adapterName, static_cast<int>(sizeof(adapterName)), nullptr, nullptr);

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        const bool hasOptions5 = SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));
        const bool supportsRaytracing = hasOptions5 &&
            options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

        if (!foundHardwareAdapter) {
            foundHardwareAdapter = true;
            result.d3d12Available = true;
            result.adapterName = adapterName;
            result.raytracingSupported = supportsRaytracing;
            result.raytracingTier = hasOptions5
                ? static_cast<std::uint32_t>(options.RaytracingTier) : 0U;
        }

        if (adapters.tellp() > 0) adapters << "; ";
        adapters << adapterName << " [" << vendor << ", D3D12=yes, DXR="
                 << (supportsRaytracing ? "yes" : "no");
        if (supportsRaytracing) adapters << " tier " << static_cast<unsigned>(options.RaytracingTier);
        adapters << "]";
    }

    std::ostringstream message;
    message << "GPU vendors detected: ";
    if (result.availableVendors.empty()) {
        message << "none";
    } else {
        for (std::size_t i = 0; i < result.availableVendors.size(); ++i) {
            if (i != 0) message << ", ";
            message << result.availableVendors[i];
        }
    }

    if (foundHardwareAdapter) {
        message << ". D3D12 adapter: " << result.adapterName
                << ". Hardware adapters: " << adapters.str()
                << ". DXR on selected adapter: "
                << (result.raytracingSupported ? "supported" : "not supported") << ".";
    } else {
        message << ". No hardware D3D12 adapter was found.";
    }
    message << " Detection does not mean a DXR renderer, DLSS, FSR, or XeSS is integrated.";
    result.diagnostic = message.str();
    return result;
}
} // namespace vanta::renderer
