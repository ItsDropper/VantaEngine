#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    std::string adapterName;
    std::vector<std::string> availableVendors;
    std::string diagnostic;
};

// Queries all hardware adapters through DXGI/D3D12 and reports detected GPU vendors
// and DXR capability. This is capability detection only; vendor-specific upscalers
// and a DXR rendering backend are not implied to be active.
[[nodiscard]] NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities();
} // namespace vanta::renderer
