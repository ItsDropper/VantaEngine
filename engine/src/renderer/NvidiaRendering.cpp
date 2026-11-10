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
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.emplace_back(value);
}

void QueryDeviceFeatures(ID3D12Device* device, NvidiaRenderingCapabilities& result)
{
    if (device == nullptr) return;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
            &options5, sizeof(options5)))) {
        result.raytracingTier = static_cast<std::uint32_t>(options5.RaytracingTier);
        result.raytracingSupported =
            options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,
            &options6, sizeof(options6)))) {
        result.variableRateShadingTier =
            static_cast<std::uint32_t>(options6.VariableShadingRateTier);
        result.variableRateShadingSupported =
            options6.VariableShadingRateTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
            &options7, sizeof(options7)))) {
        result.meshShaderTier = static_cast<std::uint32_t>(options7.MeshShaderTier);
        result.meshShadersSupported =
            options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        result.samplerFeedbackTier =
            static_cast<std::uint32_t>(options7.SamplerFeedbackTier);
        result.samplerFeedbackSupported =
            options7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
    }

    constexpr std::uint32_t shaderModelCandidates[] = {
        0x69U, 0x68U, 0x67U, 0x66U, 0x65U, 0x64U, 0x63U, 0x62U, 0x61U, 0x60U
    };
    for (const std::uint32_t candidate : shaderModelCandidates) {
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
        shaderModel.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(candidate);
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
                &shaderModel, sizeof(shaderModel)))) {
            result.shaderModel = static_cast<std::uint32_t>(shaderModel.HighestShaderModel);
            break;
        }
    }
}

std::string BuildDiagnostic(const NvidiaRenderingCapabilities& result)
{
    std::ostringstream message;
    message << result.adapterName << " [" << (result.nvidiaAdapter ? "NVIDIA" :
        result.amdAdapter ? "AMD" : result.intelAdapter ? "Intel" : "other")
        << ", D3D12=" << (result.d3d12Available ? "yes" : "no")
        << ", DXR=" << (result.raytracingSupported ? "yes" : "no")
        << ", VRS=" << (result.variableRateShadingSupported ? "yes" : "no")
        << ", MeshShaders=" << (result.meshShadersSupported ? "yes" : "no")
        << ", SamplerFeedback=" << (result.samplerFeedbackSupported ? "yes" : "no")
        << ", ShaderModel=0x" << std::hex << result.shaderModel << "].";
    message << " These are D3D12 hardware capabilities, not proof of DLSS, Reflex, NRD, RTXDI, or RTXPT SDK integration.";
    return message.str();
}
} // namespace

NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities(
    ID3D12Device* device, const std::uint32_t vendorId, std::string adapterName)
{
    NvidiaRenderingCapabilities result{};
    result.d3d12Available = device != nullptr;
    result.nvidiaAdapter = vendorId == 0x10DE;
    result.amdAdapter = vendorId == 0x1002 || vendorId == 0x1022;
    result.intelAdapter = vendorId == 0x8086;
    result.adapterName = std::move(adapterName);
    if (device != nullptr)
        QueryDeviceFeatures(device, result);
    result.diagnostic = BuildDiagnostic(result);
    return result;
}

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

    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;
    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapterResult = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()));
        if (adapterResult == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(adapterResult)) continue;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;
        adapters.push_back(std::move(adapter));
    }

    // Prefer an NVIDIA adapter for the NVIDIA capability report, but retain a
    // usable non-NVIDIA result when the machine has no NVIDIA hardware.
    std::stable_sort(adapters.begin(), adapters.end(), [](const auto& left, const auto& right) {
        DXGI_ADAPTER_DESC1 leftDescription{};
        DXGI_ADAPTER_DESC1 rightDescription{};
        (void)left->GetDesc1(&leftDescription);
        (void)right->GetDesc1(&rightDescription);
        return (leftDescription.VendorId == 0x10DE) > (rightDescription.VendorId == 0x10DE);
    });

    for (const auto& adapter : adapters) {
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) continue;
        const char* vendor = VendorName(description.VendorId);
        if (description.VendorId == 0x10DE) AddUnique(result.availableVendors, vendor);
        else if (description.VendorId == 0x1002 || description.VendorId == 0x1022)
            AddUnique(result.availableVendors, vendor);
        else if (description.VendorId == 0x8086) AddUnique(result.availableVendors, vendor);
        else AddUnique(result.availableVendors, vendor);

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(device.GetAddressOf()))))
            continue;

        char adapterName[128]{};
        (void)WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            adapterName, static_cast<int>(sizeof(adapterName)), nullptr, nullptr);
        const std::vector<std::string> detectedVendors = result.availableVendors;
        result = QueryNvidiaRenderingCapabilities(device.Get(), description.VendorId, adapterName);
        result.availableVendors = detectedVendors;
        break;
    }

    if (!result.d3d12Available)
        result.diagnostic = "No hardware D3D12 adapter was found.";
    return result;
}
} // namespace vanta::renderer
