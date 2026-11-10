#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ID3D12Device;

namespace vanta::renderer
{
struct NvidiaRenderingCapabilities
{
    bool d3d12Available = false;
    bool nvidiaAdapter = false;
    bool amdAdapter = false;
    bool intelAdapter = false;
    bool raytracingSupported = false;
    std::uint32_t raytracingTier = 0;
    bool variableRateShadingSupported = false;
    std::uint32_t variableRateShadingTier = 0;
    bool meshShadersSupported = false;
    std::uint32_t meshShaderTier = 0;
    bool samplerFeedbackSupported = false;
    std::uint32_t samplerFeedbackTier = 0;
    std::uint32_t shaderModel = 0;
    std::string adapterName;
    std::vector<std::string> availableVendors;
    std::string diagnostic;
};

// Queries feature levels on the actual D3D12 device selected by the renderer.
// These are hardware/API capabilities, not claims that an optional NVIDIA SDK
// feature (DLSS, Reflex, NRD, RTXDI, etc.) has been integrated.
[[nodiscard]] NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities(
    ID3D12Device* device, std::uint32_t vendorId, std::string adapterName);

// Enumerates installed hardware adapters and reports the highest-performance
// NVIDIA-capable adapter first where one exists. This does not change the
// renderer's adapter-selection policy.
[[nodiscard]] NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities();
} // namespace vanta::renderer
