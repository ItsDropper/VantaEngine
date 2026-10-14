#pragma once

#include <cstdint>
#include <string>

namespace vanta::renderer
{
struct NvidiaRenderingCapabilities
{
    bool d3d12Available = false;
    bool nvidiaAdapter = false;
    bool raytracingSupported = false;
    std::uint32_t raytracingTier = 0;
    std::string adapterName;
    std::string diagnostic;
};

// Queries the high-performance adapter through DXGI/D3D12. This reports hardware/runtime
// capability only; it does not claim that a DXR renderer or DLSS integration is active.
[[nodiscard]] NvidiaRenderingCapabilities QueryNvidiaRenderingCapabilities() noexcept;
} // namespace vanta::renderer
