#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vanta::renderer
{
struct D3D12RaytracingTriangleGeometry
{
    D3D12_GPU_VIRTUAL_ADDRESS vertexBuffer = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t vertexStride = 0;
    D3D12_GPU_VIRTUAL_ADDRESS indexBuffer = 0;
    std::uint32_t indexCount = 0;
    D3D12_RAYTRACING_GEOMETRY_FLAGS flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
};

struct D3D12RaytracingInstance
{
    std::uint32_t bottomLevelIndex = 0;
    std::array<float, 12> transform{};
    std::uint32_t instanceId = 0;
    std::uint8_t mask = 0xFFU;
    std::uint32_t hitGroupContribution = 0;
    D3D12_RAYTRACING_INSTANCE_FLAGS flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
};

// Owns real DXR bottom-level and top-level acceleration structures. Keep one
// instance per in-flight frame slot when rebuilding TLAS data every frame.
class D3D12RaytracingScene final
{
public:
    D3D12RaytracingScene() = default;
    ~D3D12RaytracingScene() = default;

    D3D12RaytracingScene(const D3D12RaytracingScene&) = delete;
    D3D12RaytracingScene& operator=(const D3D12RaytracingScene&) = delete;

    [[nodiscard]] bool Initialize(ID3D12Device* device, std::string& error);
    [[nodiscard]] bool BuildBottomLevel(
        ID3D12GraphicsCommandList* commandList,
        std::span<const D3D12RaytracingTriangleGeometry> geometries,
        std::string& error);
    [[nodiscard]] bool BuildTopLevel(
        ID3D12GraphicsCommandList* commandList,
        std::span<const D3D12RaytracingInstance> instances,
        std::string& error);

    [[nodiscard]] bool IsSupported() const noexcept { return m_device5 != nullptr; }
    [[nodiscard]] std::size_t BottomLevelCount() const noexcept { return m_bottomLevels.size(); }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS TopLevelAddress() const noexcept;
    [[nodiscard]] ID3D12Resource* TopLevelResource() const noexcept { return m_topLevel.Get(); }

private:
    struct AccelerationStructure
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> result;
        Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
    };

    [[nodiscard]] bool CreateAccelerationStructureBuffer(
        std::uint64_t size, D3D12_RESOURCE_STATES state,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource, std::string& error) const;

    Microsoft::WRL::ComPtr<ID3D12Device5> m_device5;
    std::vector<AccelerationStructure> m_bottomLevels;
    AccelerationStructure m_topLevel;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceUpload;
};
} // namespace vanta::renderer
