#include "vanta/renderer/D3D12RaytracingScene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>
#include <limits>
#include <sstream>

namespace vanta::renderer
{
namespace
{
std::string HResultMessage(const char* operation, const HRESULT result)
{
    std::ostringstream message;
    message << operation << " failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(result) << ").";
    return message.str();
}

D3D12_HEAP_PROPERTIES DefaultHeapProperties() noexcept
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(const std::uint64_t size) noexcept
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

void AddUavBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource) noexcept
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commandList->ResourceBarrier(1, &barrier);
}
} // namespace

bool D3D12RaytracingScene::Initialize(ID3D12Device* device, std::string& error)
{
    m_device5.Reset();
    m_bottomLevels.clear();
    m_topLevel = {};
    m_instanceUpload.Reset();
    if (device == nullptr)
    {
        error = "DXR initialization requires a valid D3D12 device.";
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) ||
        options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
    {
        error = "The selected D3D12 device does not expose hardware ray tracing.";
        return false;
    }

    const HRESULT queryResult = device->QueryInterface(IID_PPV_ARGS(m_device5.GetAddressOf()));
    if (FAILED(queryResult))
    {
        error = HResultMessage("QueryInterface(ID3D12Device5)", queryResult);
        return false;
    }
    error.clear();
    return true;
}

bool D3D12RaytracingScene::CreateAccelerationStructureBuffer(
    const std::uint64_t size, const D3D12_RESOURCE_STATES state,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource, std::string& error) const
{
    if (m_device5 == nullptr || size == 0U)
    {
        error = "Cannot allocate a zero-sized acceleration-structure buffer.";
        return false;
    }
    const D3D12_HEAP_PROPERTIES heap = DefaultHeapProperties();
    const D3D12_RESOURCE_DESC description = BufferDescription(size);
    const HRESULT result = m_device5->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
        IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(result))
    {
        error = HResultMessage("CreateCommittedResource(acceleration structure)", result);
        return false;
    }
    return true;
}

bool D3D12RaytracingScene::BuildBottomLevel(
    ID3D12GraphicsCommandList* commandList,
    const std::span<const D3D12RaytracingTriangleGeometry> geometries,
    std::string& error)
{
    if (m_device5 == nullptr || commandList == nullptr)
    {
        error = "BLAS build requires an initialized DXR device and an open command list.";
        return false;
    }
    if (geometries.empty())
    {
        error = "BLAS build requires at least one triangle geometry.";
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4;
    const HRESULT queryResult = commandList->QueryInterface(IID_PPV_ARGS(commandList4.GetAddressOf()));
    if (FAILED(queryResult))
    {
        error = HResultMessage("QueryInterface(ID3D12GraphicsCommandList4)", queryResult);
        return false;
    }

    std::vector<AccelerationStructure> newBottomLevels;
    newBottomLevels.reserve(geometries.size());
    for (const D3D12RaytracingTriangleGeometry& geometry : geometries)
    {
        if (geometry.vertexBuffer == 0U || geometry.indexBuffer == 0U ||
            geometry.vertexCount < 3U || geometry.indexCount < 3U ||
            geometry.indexCount % 3U != 0U || geometry.vertexStride < 12U)
        {
            error = "BLAS geometry has invalid GPU addresses, vertex layout, or triangle indices.";
            return false;
        }

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDescription{};
        geometryDescription.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDescription.Flags = geometry.flags;
        geometryDescription.Triangles.Transform3x4 = 0U;
        geometryDescription.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDescription.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDescription.Triangles.IndexCount = geometry.indexCount;
        geometryDescription.Triangles.VertexCount = geometry.vertexCount;
        geometryDescription.Triangles.IndexBuffer = geometry.indexBuffer;
        geometryDescription.Triangles.VertexBuffer.StartAddress = geometry.vertexBuffer;
        geometryDescription.Triangles.VertexBuffer.StrideInBytes = geometry.vertexStride;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = 1U;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = &geometryDescription;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
        m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
        if (prebuild.ResultDataMaxSizeInBytes == 0U || prebuild.ScratchDataSizeInBytes == 0U)
        {
            error = "D3D12 returned invalid BLAS prebuild sizes.";
            return false;
        }

        AccelerationStructure structure;
        if (!CreateAccelerationStructureBuffer(prebuild.ResultDataMaxSizeInBytes,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, structure.result, error) ||
            !CreateAccelerationStructureBuffer(prebuild.ScratchDataSizeInBytes,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, structure.scratch, error))
            return false;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
        build.Inputs = inputs;
        build.DestAccelerationStructureData = structure.result->GetGPUVirtualAddress();
        build.ScratchAccelerationStructureData = structure.scratch->GetGPUVirtualAddress();
        commandList4->BuildRaytracingAccelerationStructure(&build, 0U, nullptr);
        AddUavBarrier(commandList, structure.result.Get());
        newBottomLevels.push_back(std::move(structure));
    }

    m_bottomLevels = std::move(newBottomLevels);
    m_topLevel = {};
    m_instanceUpload.Reset();
    error.clear();
    return true;
}

bool D3D12RaytracingScene::BuildTopLevel(
    ID3D12GraphicsCommandList* commandList,
    const std::span<const D3D12RaytracingInstance> instances,
    std::string& error)
{
    if (m_device5 == nullptr || commandList == nullptr)
    {
        error = "TLAS build requires an initialized DXR device and an open command list.";
        return false;
    }
    if (m_bottomLevels.empty() || instances.empty() ||
        instances.size() > D3D12_RAYTRACING_MAX_INSTANCE_COUNT)
    {
        error = "TLAS build requires valid BLAS data and a supported non-empty instance list.";
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commandList4;
    const HRESULT queryResult = commandList->QueryInterface(IID_PPV_ARGS(commandList4.GetAddressOf()));
    if (FAILED(queryResult))
    {
        error = HResultMessage("QueryInterface(ID3D12GraphicsCommandList4)", queryResult);
        return false;
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descriptions;
    descriptions.reserve(instances.size());
    for (const D3D12RaytracingInstance& source : instances)
    {
        if (source.bottomLevelIndex >= m_bottomLevels.size() ||
            source.instanceId > 0x00FFFFFFU || source.hitGroupContribution > 0x00FFFFFFU)
        {
            error = "TLAS instance references an invalid BLAS or exceeds the 24-bit DXR identifier limit.";
            return false;
        }
        D3D12_RAYTRACING_INSTANCE_DESC description{};
        for (std::size_t row = 0; row < 3U; ++row)
            for (std::size_t column = 0; column < 4U; ++column)
                description.Transform[row][column] = source.transform[row * 4U + column];
        description.InstanceID = source.instanceId;
        description.InstanceMask = source.mask;
        description.InstanceContributionToHitGroupIndex = source.hitGroupContribution;
        description.Flags = source.flags;
        description.AccelerationStructure =
            m_bottomLevels[source.bottomLevelIndex].result->GetGPUVirtualAddress();
        descriptions.push_back(description);
    }

    const std::uint64_t instanceBytes =
        static_cast<std::uint64_t>(descriptions.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    const D3D12_HEAP_PROPERTIES uploadHeap = [] {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }();
    const D3D12_RESOURCE_DESC uploadDescription = BufferDescription(instanceBytes);
    const HRESULT uploadResult = m_device5->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDescription,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(m_instanceUpload.ReleaseAndGetAddressOf()));
    if (FAILED(uploadResult))
    {
        error = HResultMessage("CreateCommittedResource(TLAS instance upload)", uploadResult);
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE noRead{0U, 0U};
    const HRESULT mapResult = m_instanceUpload->Map(0U, &noRead, &mapped);
    if (FAILED(mapResult) || mapped == nullptr)
    {
        error = HResultMessage("Map(TLAS instance upload)", mapResult);
        return false;
    }
    std::memcpy(mapped, descriptions.data(), static_cast<std::size_t>(instanceBytes));
    const D3D12_RANGE written{0U, static_cast<SIZE_T>(instanceBytes)};
    m_instanceUpload->Unmap(0U, &written);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = static_cast<UINT>(descriptions.size());
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = m_instanceUpload->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0U || prebuild.ScratchDataSizeInBytes == 0U)
    {
        error = "D3D12 returned invalid TLAS prebuild sizes.";
        return false;
    }

    AccelerationStructure newTopLevel;
    if (!CreateAccelerationStructureBuffer(prebuild.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, newTopLevel.result, error) ||
        !CreateAccelerationStructureBuffer(prebuild.ScratchDataSizeInBytes,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, newTopLevel.scratch, error))
        return false;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = newTopLevel.result->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = newTopLevel.scratch->GetGPUVirtualAddress();
    commandList4->BuildRaytracingAccelerationStructure(&build, 0U, nullptr);
    AddUavBarrier(commandList, newTopLevel.result.Get());

    m_topLevel = std::move(newTopLevel);
    error.clear();
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS D3D12RaytracingScene::TopLevelAddress() const noexcept
{
    return m_topLevel.result != nullptr ? m_topLevel.result->GetGPUVirtualAddress() : 0U;
}
} // namespace vanta::renderer
