#include "vanta/renderer/D3D12Renderer.hpp"
#include "vanta/renderer/SceneGeometry.hpp"

#include "vanta/core/Log.hpp"
#include "vanta/world/ProceduralWorld.hpp"
#include "vanta/assets/GltfLoader.hpp"
#include "vanta/assets/ObjLoader.hpp"
#include "vanta/assets/WicTextureLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <vector>
#include <filesystem>
#include <string>
#include <limits>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <array>

namespace vanta
{
namespace
{
using math::Mat4;
using math::Vec3;

struct D3D12WorldVertex
{
    float position[3];
    float normal[3];
    float color[4];
    float uv[2];
    float material[4];
};
static_assert(sizeof(D3D12WorldVertex) == 64);

struct SceneConstants
{
    Mat4 viewProjection;
    float cameraPosition[4];
    float lighting[4];
    Mat4 lightViewProjection[3];
    float cascadeSplits[4];
};
static_assert(sizeof(SceneConstants) == 304);

struct TemporalConstants
{
    Mat4 inverseViewProjection;
    Mat4 previousViewProjection;
    float parameters[4];
    float sunParameters[4];
};
static_assert(sizeof(TemporalConstants) == 160);

struct GpuDrawChunk
{
    float centerRadius[4]{};
    UINT startIndex[5]{};
    UINT indexCount[5]{};
};
static_assert(sizeof(GpuDrawChunk) == 56);

struct GpuCullConstants
{
    Mat4 cullMatrix{};
    float cameraPosition[4]{};
    float cameraForward[4]{};
    float cameraRight[4]{};
    float cameraUp[4]{};
    float parameters[4]{};
    UINT chunkCount = 0;
    UINT useHiZOcclusion = 0;
    UINT padding[2]{};
};
static_assert(sizeof(GpuCullConstants) == 160);

struct HiZBuildConstants
{
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    UINT destinationWidth = 0;
    UINT destinationHeight = 0;
    UINT sourceMip = 0;
    UINT padding[3]{};
};
static_assert(sizeof(HiZBuildConstants) == 32);

constexpr UINT kShadowResolution = 2048;
constexpr UINT64 kShadowConstantOffset = 512;
constexpr UINT64 kShadowConstantStride = 256;

constexpr char kGpuCullShader[] = R"(
cbuffer CullConstants : register(b0)
{
    float4x4 cullMatrix;
    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 parameters;
    uint chunkCount;
    uint useHiZOcclusion;
    uint2 cullPadding;
};

ByteAddressBuffer chunkData : register(t0);
Texture2D<float> previousHiZ : register(t1);
RWByteAddressBuffer drawArguments : register(u0);
RWByteAddressBuffer drawCounter : register(u1);

bool IsOccludedByPreviousHiZ(float3 center, float radius)
{
    if (useHiZOcclusion == 0U)
        return false;

    const float4 clip = mul(cullMatrix, float4(center, 1.0F));
    const float radiusX = radius * length(cullMatrix[0].xyz);
    const float radiusY = radius * length(cullMatrix[1].xyz);
    const float radiusZ = radius * length(cullMatrix[2].xyz);
    const float radiusW = radius * length(cullMatrix[3].xyz);
    const float minimumW = clip.w - radiusW;
    if (minimumW <= 0.05F || clip.w <= 0.0F)
        return false;

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float inverseDenominator = 1.0F / (clip.w * minimumW);
    const float extentX = (radiusX * clip.w + abs(clip.x) * radiusW) * inverseDenominator;
    const float extentY = (radiusY * clip.w + abs(clip.y) * radiusW) * inverseDenominator;

    uint baseWidth, baseHeight, mipCount;
    previousHiZ.GetDimensions(0U, baseWidth, baseHeight, mipCount);
    const float2 pixelExpansion = 2.0F / float2(baseWidth, baseHeight);
    const float2 uvMinimum = float2(
        (ndcX - extentX) * 0.5F + 0.5F,
        0.5F - (ndcY + extentY) * 0.5F) - pixelExpansion;
    const float2 uvMaximum = float2(
        (ndcX + extentX) * 0.5F + 0.5F,
        0.5F - (ndcY - extentY) * 0.5F) + pixelExpansion;
    if (any(uvMinimum < 0.0F) || any(uvMaximum > 1.0F))
        return false;

    const float nearestDepth = (clip.z - radiusZ) / (clip.w + radiusW);
    if (nearestDepth <= 0.0F || nearestDepth >= 1.0F)
        return false;

    const float2 rectanglePixels = max((uvMaximum - uvMinimum) * float2(baseWidth, baseHeight), 1.0F);
    const uint mipLevel = min((uint)max(floor(log2(max(rectanglePixels.x, rectanglePixels.y))), 0.0F),
                              max(mipCount, 1U) - 1U);
    uint mipWidth, mipHeight, ignoredLevels;
    previousHiZ.GetDimensions(mipLevel, mipWidth, mipHeight, ignoredLevels);
    const int2 minimumCell = clamp(int2(floor(uvMinimum * float2(mipWidth, mipHeight))),
                                   int2(0, 0), int2(mipWidth - 1U, mipHeight - 1U));
    const int2 maximumCell = clamp(int2(floor(uvMaximum * float2(mipWidth, mipHeight))),
                                   int2(0, 0), int2(mipWidth - 1U, mipHeight - 1U));
    if (any(maximumCell - minimumCell > 1))
        return false;

    float farthestOccluderDepth = previousHiZ.Load(int3(minimumCell, mipLevel));
    farthestOccluderDepth = max(farthestOccluderDepth,
        previousHiZ.Load(int3(maximumCell.x, minimumCell.y, mipLevel)));
    farthestOccluderDepth = max(farthestOccluderDepth,
        previousHiZ.Load(int3(minimumCell.x, maximumCell.y, mipLevel)));
    farthestOccluderDepth = max(farthestOccluderDepth,
        previousHiZ.Load(int3(maximumCell, mipLevel)));

    // Max-reduced depth plus a conservative near bound can only reject a chunk when
    // every HZB tile covering its projected sphere is closer than the sphere itself.
    return farthestOccluderDepth + 0.0025F < nearestDepth;
}

[numthreads(64, 1, 1)]
void CullCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint chunkIndex = dispatchThreadId.x;
    if (chunkIndex >= chunkCount)
        return;

    const uint chunkOffset = chunkIndex * 56U;
    const float4 bounds = asfloat(chunkData.Load4(chunkOffset));
    const float3 center = bounds.xyz;
    const float radius = bounds.w;
    const float3 toChunk = center - cameraPosition.xyz;

    uint lod = 0U;
    if (parameters.x > 0.5F)
    {
        if (length(toChunk) > parameters.y + radius * 1.5F)
            return;

        const float4 clip = mul(cullMatrix, float4(center, 1.0F));
        const float radiusX = radius * length(cullMatrix[0].xyz);
        const float radiusY = radius * length(cullMatrix[1].xyz);
        const float radiusZ = radius * length(cullMatrix[2].xyz);
        if (clip.x + radiusX < -1.0F || clip.x - radiusX > 1.0F ||
            clip.y + radiusY < -1.0F || clip.y - radiusY > 1.0F ||
            clip.z + radiusZ < 0.0F || clip.z - radiusZ > 1.0F)
            return;

        lod = (uint)parameters.z;
    }
    else
    {
        const float distanceToChunk = length(toChunk);
        if (distanceToChunk > 270.0F + radius)
            return;

        const float forwardDistance = dot(toChunk, cameraForward.xyz);
        if (forwardDistance < -radius)
            return;

        const float horizontalLimit =
            max(forwardDistance, 0.0F) * parameters.z * parameters.w + radius;
        if (abs(dot(toChunk, cameraRight.xyz)) > horizontalLimit)
            return;

        const float verticalLimit = max(forwardDistance, 0.0F) * parameters.z + radius;
        if (abs(dot(toChunk, cameraUp.xyz)) > verticalLimit)
            return;
        if (IsOccludedByPreviousHiZ(center, radius + 0.25F)) {
            uint ignoredOcclusionCount = 0U;
            drawCounter.InterlockedAdd(8U, 1U, ignoredOcclusionCount);
            return;
        }

        lod = distanceToChunk < 65.0F * parameters.y ? 0U :
            (distanceToChunk < 140.0F * parameters.y ? 1U : 2U);
    }

    const uint indexCount = chunkData.Load(chunkOffset + 36U + lod * 4U);
    if (indexCount == 0U)
        return;

    uint drawSlot = 0U;
    drawCounter.InterlockedAdd(0U, 1U, drawSlot);
    uint ignoredTriangleValue = 0U;
    drawCounter.InterlockedAdd(4U, indexCount / 3U, ignoredTriangleValue);
    if (drawSlot >= chunkCount)
        return;

    const uint startIndex = chunkData.Load(chunkOffset + 16U + lod * 4U);
    const uint argumentOffset = drawSlot * 20U;
    drawArguments.Store(argumentOffset + 0U, indexCount);
    drawArguments.Store(argumentOffset + 4U, 1U);
    drawArguments.Store(argumentOffset + 8U, startIndex);
    drawArguments.Store(argumentOffset + 12U, 0U);
    drawArguments.Store(argumentOffset + 16U, 0U);
}
)";

Mat4 OrthographicLH(float width, float height, float nearZ, float farZ)
{
    Mat4 result = Mat4::Identity();
    result.At(0, 0) = 2.0F / width;
    result.At(1, 1) = 2.0F / height;
    result.At(2, 2) = 1.0F / (farZ - nearZ);
    result.At(2, 3) = -nearZ / (farZ - nearZ);
    return result;
}

float Halton(std::uint32_t index, std::uint32_t base)
{
    float result = 0.0F;
    float fraction = 1.0F / static_cast<float>(base);
    while (index > 0U) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

bool InvertMatrix(const Mat4& input, Mat4& output)
{
    float augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) augmented[row][column] = input.At(row, column);
        augmented[row][row + 4] = 1.0F;
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        if (std::abs(augmented[pivot][column]) < 1.0e-8F) return false;
        if (pivot != column)
            for (int k = 0; k < 8; ++k) std::swap(augmented[pivot][k], augmented[column][k]);
        const float divisor = augmented[column][column];
        for (int k = 0; k < 8; ++k) augmented[column][k] /= divisor;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const float factor = augmented[row][column];
            for (int k = 0; k < 8; ++k) augmented[row][k] -= factor * augmented[column][k];
        }
    }
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            output.At(row, column) = augmented[row][column + 4];
    return true;
}


float TerrainHeight(float x, float z)
{
    return world::TerrainHeightAt(x, z);
}

void AppendTerrain(std::vector<world::WorldVertex>& vertices, std::vector<unsigned int>& indices)
{
    // A half-metre grid keeps the shared noise terrain silhouette smooth at close range.
    constexpr int side = 257;
    constexpr float extent = 72.0F;
    const unsigned int base = static_cast<unsigned int>(vertices.size());
    vertices.reserve(vertices.size() + side * side);
    indices.reserve(indices.size() + (side - 1) * (side - 1) * 6);
    for (int z = 0; z < side; ++z)
    {
        for (int x = 0; x < side; ++x)
        {
            const float wx = -extent + (2.0F * extent * x) / (side - 1);
            const float wz = -extent + (2.0F * extent * z) / (side - 1);
            const float y = TerrainHeight(wx, wz);
            const float dx = (TerrainHeight(wx - 0.15F, wz) - TerrainHeight(wx + 0.15F, wz)) / 0.3F;
            const float dz = (TerrainHeight(wx, wz - 0.15F) - TerrainHeight(wx, wz + 0.15F)) / 0.3F;
            const float invLength = 1.0F / std::sqrt(dx * dx + 1.0F + dz * dz);
            const float slope = std::sqrt(dx * dx + dz * dz);
            const float grass = std::clamp(0.76F - slope * 0.42F + (y + 1.4F) * 0.035F, 0.18F, 0.82F);
            const float macroVariation = std::sin(wx * 0.11F + std::sin(wz * 0.07F)) *
                std::sin(wz * 0.13F - wx * 0.035F);
            const float detailVariation = std::sin(wx * 0.47F + wz * 0.31F) * 0.035F;
            const float variation = std::clamp(0.94F + macroVariation * 0.07F + detailVariation, 0.78F, 1.08F);
            vertices.push_back({{wx, y, wz}, {dx * invLength, invLength, dz * invLength},
                {0.19F * variation, grass * variation, 0.13F * variation, 1.0F},
                {wx * 0.15F, wz * 0.15F}});
        }
    }
    for (int z = 0; z < side - 1; ++z)
    {
        for (int x = 0; x < side - 1; ++x)
        {
            const unsigned int a = base + static_cast<unsigned int>(z * side + x);
            const unsigned int b = a + 1U;
            const unsigned int c = a + static_cast<unsigned int>(side);
            const unsigned int d = c + 1U;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

bool CreateUploadBuffer(ID3D12Device* device, UINT64 size, Microsoft::WRL::ComPtr<ID3D12Resource>& resource)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(resource.GetAddressOf())));
}

bool CreateDefaultBuffer(ID3D12Device* device, UINT64 size,
                         Microsoft::WRL::ComPtr<ID3D12Resource>& resource)
{
    if (device == nullptr || size == 0U) return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(resource.GetAddressOf())));
}

bool CreateDefaultUavBuffer(ID3D12Device* device, UINT64 size, D3D12_RESOURCE_STATES initialState,
                            Microsoft::WRL::ComPtr<ID3D12Resource>& resource)
{
    if (device == nullptr || size == 0U) return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        initialState, nullptr, IID_PPV_ARGS(resource.GetAddressOf())));
}

std::vector<std::uint8_t> ResampleTexture(const assets::CpuTexture& source, UINT width, UINT height, bool srgb)
{
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4U);
    const auto toLinear = [](float c) {
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    const auto toSrgb = [](float c) {
        c = std::clamp(c, 0.0F, 1.0F);
        return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
    };
    for (UINT y = 0; y < height; ++y) {
        const float sy = std::clamp((static_cast<float>(y) + 0.5F) * source.height / height - 0.5F,
            0.0F, static_cast<float>(source.height - 1U));
        const UINT y0 = static_cast<UINT>(sy);
        const UINT y1 = std::min(source.height - 1U, y0 + 1U);
        const float fy = sy - static_cast<float>(y0);
        for (UINT x = 0; x < width; ++x) {
            const float sx = std::clamp((static_cast<float>(x) + 0.5F) * source.width / width - 0.5F,
                0.0F, static_cast<float>(source.width - 1U));
            const UINT x0 = static_cast<UINT>(sx);
            const UINT x1 = std::min(source.width - 1U, x0 + 1U);
            const float fx = sx - static_cast<float>(x0);
            for (UINT channel = 0; channel < 4; ++channel) {
                auto sample = [&](UINT px, UINT py) {
                    float value = static_cast<float>(source.rgba[(static_cast<std::size_t>(py) * source.width + px) * 4U + channel]) / 255.0F;
                    return srgb && channel < 3U ? toLinear(value) : value;
                };
                const float top = sample(x0, y0) * (1.0F - fx) + sample(x1, y0) * fx;
                const float bottom = sample(x0, y1) * (1.0F - fx) + sample(x1, y1) * fx;
                float value = top * (1.0F - fy) + bottom * fy;
                if (srgb && channel < 3U) value = toSrgb(value);
                result[(static_cast<std::size_t>(y) * width + x) * 4U + channel] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0F), 0L, 255L));
            }
        }
    }
    return result;
}
} // namespace

bool D3D12Renderer::PrepareSubmittedMaterialTextures(
    const renderer::SubmittedSceneGeometry& geometry, std::string& error)
{
    if (geometry.draws.empty()) {
        m_submittedMaterialCacheKey.clear();
        m_submittedMaterialLayerCount = 0U;
        error.clear();
        return true;
    }
    if (geometry.draws.size() > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) {
        error = "Submitted scene exceeds the Direct3D 12 material texture-array layer limit.";
        return false;
    }

    const std::array<std::array<std::uint8_t, 4>, 5> neutralPixels = {{
        {{255U, 255U, 255U, 255U}},
        {{128U, 128U, 255U, 255U}},
        {{255U, 255U, 255U, 255U}},
        {{255U, 255U, 255U, 255U}},
        {{255U, 255U, 255U, 255U}}
    }};
    std::array<bool, 5> usedSlots{};
    std::string cacheKey = std::to_string(geometry.draws.size());
    for (const renderer::SubmittedDrawRange& draw : geometry.draws) {
        const std::array<std::filesystem::path, 5> paths = {
            draw.material.baseColorTexture,
            draw.material.normalTexture,
            draw.material.roughnessTexture,
            draw.material.metallicTexture,
            draw.material.ambientOcclusionTexture
        };
        for (std::size_t slot = 0; slot < paths.size(); ++slot) {
            cacheKey.push_back('|');
            cacheKey += paths[slot].generic_string();
            usedSlots[slot] = usedSlots[slot] || !paths[slot].empty();
        }
    }
    if (cacheKey == m_submittedMaterialCacheKey &&
        m_submittedMaterialLayerCount == geometry.draws.size()) {
        error.clear();
        return true;
    }

    for (UINT slot = 0; slot < usedSlots.size(); ++slot) {
        if (!usedSlots[slot])
            continue;
        std::vector<assets::CpuTexture> materialMaps;
        materialMaps.reserve(geometry.draws.size());
        UINT textureResolution = 1U;
        for (const renderer::SubmittedDrawRange& draw : geometry.draws) {
            const std::array<std::filesystem::path, 5> paths = {
                draw.material.baseColorTexture,
                draw.material.normalTexture,
                draw.material.roughnessTexture,
                draw.material.metallicTexture,
                draw.material.ambientOcclusionTexture
            };
            assets::CpuTexture texture;
            texture.width = 1U;
            texture.height = 1U;
            texture.rgba.assign(neutralPixels[slot].begin(), neutralPixels[slot].end());
            if (!paths[slot].empty()) {
                if (!assets::LoadWicTexture(paths[slot], texture) ||
                    texture.width == 0U || texture.height == 0U ||
                    texture.rgba.size() < static_cast<std::size_t>(texture.width) *
                        texture.height * 4U) {
                    error = "Could not decode submitted material texture: " + paths[slot].string();
                    return false;
                }
                textureResolution = std::max(textureResolution,
                    std::max(texture.width, texture.height));
            }
            materialMaps.push_back(std::move(texture));
        }
        textureResolution = std::clamp(textureResolution, 1U, 2048U);
        const std::size_t layerBytes = static_cast<std::size_t>(textureResolution) *
            textureResolution * 4U;
        std::vector<std::uint8_t> textureLayers;
        textureLayers.reserve(layerBytes * materialMaps.size());
        for (const assets::CpuTexture& texture : materialMaps) {
            const auto pixels = ResampleTexture(texture, textureResolution,
                textureResolution, slot == 0U);
            textureLayers.insert(textureLayers.end(), pixels.begin(), pixels.end());
        }
        if (!CreateModelTextureArray(textureResolution, textureResolution,
                static_cast<UINT>(materialMaps.size()), textureLayers,
                kSubmittedMaterialBaseSrvIndex + slot, slot == 0U)) {
            error = "Could not upload a submitted material texture array to Direct3D 12.";
            return false;
        }
    }

    m_submittedMaterialCacheKey = std::move(cacheKey);
    m_submittedMaterialLayerCount = static_cast<UINT>(geometry.draws.size());
    error.clear();
    return true;
}

D3D12Renderer::~D3D12Renderer()
{
    (void)WaitForGpu();
    for (UINT frame = 0; frame < kFrameCount; ++frame) {
        for (UINT stream = 0; stream < kIndirectStreamCount; ++stream) {
            if (m_mappedIndirectArguments[frame][stream] != nullptr && m_indirectArgumentBuffers[frame][stream]) {
                m_indirectArgumentBuffers[frame][stream]->Unmap(0, nullptr);
                m_mappedIndirectArguments[frame][stream] = nullptr;
            }
        }
    }
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (m_mappedHiZConstants[i] != nullptr && m_hiZConstantBuffers[i]) {
            m_hiZConstantBuffers[i]->Unmap(0, nullptr);
            m_mappedHiZConstants[i] = nullptr;
        }
        if (m_mappedGpuCullConstants[i] != nullptr && m_gpuCullConstantBuffers[i]) {
            m_gpuCullConstantBuffers[i]->Unmap(0, nullptr);
            m_mappedGpuCullConstants[i] = nullptr;
        }
        if (m_mappedGpuCullStats[i] != nullptr && m_gpuCullStatsReadbackBuffers[i]) {
            m_gpuCullStatsReadbackBuffers[i]->Unmap(0, nullptr);
            m_mappedGpuCullStats[i] = nullptr;
        }
        if (m_mappedConstants[i] != nullptr && m_constantBuffers[i]) {
            m_constantBuffers[i]->Unmap(0, nullptr);
            m_mappedConstants[i] = nullptr;
        }
        if (m_mappedTemporalConstants[i] != nullptr && m_temporalConstantBuffers[i]) {
            m_temporalConstantBuffers[i]->Unmap(0, nullptr);
            m_mappedTemporalConstants[i] = nullptr;
        }
    }
    if (m_mappedTimestamps != nullptr && m_timestampReadback) {
        m_timestampReadback->Unmap(0, nullptr);
        m_mappedTimestamps = nullptr;
    }
    if (m_fenceEvent != nullptr) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

bool D3D12Renderer::Initialize(HWND window, int width, int height)
{
    if (window == nullptr || width <= 0 || height <= 0) return false;
    m_window = window;
    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);

    UINT factoryFlags = 0;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
        debugController->EnableDebugLayer();
        Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
        if (SUCCEEDED(debugController.As(&debugController1))) {
            // GPU-based validation catches invalid descriptor use, resource states,
            // and indirect-argument hazards that ordinary CPU validation cannot see.
            debugController1->SetEnableGPUBasedValidation(TRUE);
            debugController1->SetEnableSynchronizedCommandQueueValidation(TRUE);
            LogInfo("Direct3D 12 Debug: GPU-based and synchronized command-queue validation enabled.");
        } else {
            LogInfo("Direct3D 12 Debug: GPU-based validation is unavailable in this SDK/runtime.");
        }
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    } else {
        LogInfo("Direct3D 12 Debug: debug layer is unavailable; continuing without validation.");
    }
#endif
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.GetAddressOf())))) {
        LogError("Direct3D 12: DXGI factory creation failed.");
        return false;
    }

    for (UINT index = 0; ; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (m_factory->EnumAdapters1(index, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.GetAddressOf())))) {
            LogInfo("Direct3D 12 hardware adapter selected.");
            break;
        }
    }
#if defined(_DEBUG)
    if (m_device) {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            (void)infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            (void)infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            LogInfo("Direct3D 12 Debug: breaking on GPU validation errors and device corruption.");
        }
    }
#endif
    if (!m_device) {
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        if (FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))) ||
            FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.GetAddressOf())))) {
            LogError("Direct3D 12: no compatible hardware or WARP device was found.");
            return false;
        }
        LogInfo("Direct3D 12: using the WARP software adapter.");
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(m_commandQueue.GetAddressOf())))) {
        LogError("Direct3D 12: command queue creation failed.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapDescription{};
    swapDescription.Width = m_width;
    swapDescription.Height = m_height;
    swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = kFrameCount;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDescription.Scaling = DXGI_SCALING_STRETCH;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_window, &swapDescription,
            nullptr, nullptr, swapChain.GetAddressOf())) || FAILED(swapChain.As(&m_swapChain))) {
        LogError("Direct3D 12: swap-chain creation failed.");
        return false;
    }
    (void)m_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.NumDescriptors = kFrameCount + 5;
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(m_rtvHeap.GetAddressOf())))) {
        LogError("Direct3D 12: RTV descriptor heap creation failed.");
        return false;
    }
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
    dsvHeapDescription.NumDescriptors = 4;
    dsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDescription, IID_PPV_ARGS(m_dsvHeap.GetAddressOf())))) {
        LogError("Direct3D 12: DSV descriptor heap creation failed.");
        return false;
    }

    for (auto& allocator : m_commandAllocators) {
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.GetAddressOf())))) {
            LogError("Direct3D 12: command allocator creation failed.");
            return false;
        }
    }
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(),
            nullptr, IID_PPV_ARGS(m_commandList.GetAddressOf()))) || FAILED(m_commandList->Close())) {
        LogError("Direct3D 12: command-list creation failed.");
        return false;
    }
    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf())))) {
        LogError("Direct3D 12: fence creation failed.");
        return false;
    }
    (void)InitializeGpuProfiler();
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        LogError("Direct3D 12: fence event creation failed.");
        return false;
    }

    if (!CreateRenderTargets() || !CreateDepthTarget() || !CreateShadowResources() || !CreateHdrTarget() ||
        !CreateTemporalTargets() || !CreateSceneGeometry() || !CreateIndirectDrawResources()) return false;
    if (CreateHiZResources() && CreateHiZPipeline()) {
        if (!CreateGpuCullResources())
            LogInfo("Direct3D 12: GPU compute culling unavailable; retaining the CPU culling fallback.");
    } else {
        LogInfo("Direct3D 12: Hi-Z setup unavailable; retaining the CPU culling fallback.");
    }
    if (!CreatePipeline() || !CreateShadowPipeline() || !CreateTemporalPipeline() || !CreatePostProcessPipeline())
        return false;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    LogInfo("Direct3D 12 scene renderer initialized: HDR PBR shading, three 2048px cascaded shadow maps with 3x3 PCF, ACES tone mapping, highlight bloom, depth-aware contact occlusion, procedural sky and atmospheric fog.");
    LogInfo("Camera controls: W/A/S/D move, Q/E descend/ascend, arrow keys look, Escape exits.");
    return true;
}






bool D3D12Renderer::CreateSceneGeometry()
{
    std::vector<world::WorldVertex> vertices;
    std::vector<unsigned int> indices;
    AppendTerrain(vertices, indices);
    const std::size_t terrainVertexCount = vertices.size();
    world::BuildDetailedWorld(vertices, indices);
    std::vector<std::array<float, 4>> materialParameters(vertices.size(), std::array<float, 4>{0.78F, 0.0F, 0.0F, 0.0F});
    // Reserve material flag bit 6 for terrain-only procedural shading. This lets the
    // pixel shader skip terrain noise on foliage, cabins, rocks and other scene meshes.
    for (std::size_t i = 0; i < terrainVertexCount; ++i) materialParameters[i][3] = 64.0F;

    const std::filesystem::path modelDirectory = std::filesystem::path{VANTA_ASSET_DIR} / "models";
    const std::array<std::filesystem::path, 3> modelCandidates = {
        modelDirectory / "stone_house.glb", modelDirectory / "stone_house.gltf", modelDirectory / "stone_house.obj"
    };
    assets::CpuModel model;
    std::string importError;
    bool modelLoaded = false;
    for (const auto& path : modelCandidates) {
        if (!std::filesystem::exists(path)) continue;
        const std::string extension = path.extension().string();
        const bool loaded = (extension == ".glb" || extension == ".gltf")
            ? assets::LoadGltf(path, model, importError)
            : assets::LoadWavefrontObj(path, model, importError);
        if (loaded) {
            modelLoaded = true;
            break;
        }
        LogError(importError.c_str());
    }

    if (modelLoaded) {
        if (model.materials.empty()) {
            assets::CpuMaterial material;
            material.diffuseTexture = model.diffuseTexture;
            material.normalTexture = model.normalTexture;
            material.roughnessTexture = model.roughnessTexture;
            material.metallicTexture = model.metallicTexture;
            material.ambientOcclusionTexture = model.ambientOcclusionTexture;
            material.baseColor = model.baseColor;
            material.roughness = model.roughness;
            material.metallic = model.metallic;
            model.materials.push_back(material);
        }
        if (model.submeshes.empty())
            model.submeshes.push_back({0U, static_cast<std::uint32_t>(model.indices.size()), 0U});
    }
    std::size_t materialCount = modelLoaded ? model.materials.size() : 1U;
    if (materialCount == 0U || materialCount > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) {
        LogError("Direct3D 12: imported asset exceeds the supported PBR material-layer limit; using the procedural scene.");
        modelLoaded = false;
        model = {};
        materialCount = 1U;
    }
    m_modelMaterialLayerCount = static_cast<UINT>(materialCount);
    UINT materialTextureResolution = 1U;
    const std::array<assets::CpuTexture, 5> neutralTextures = {{
        {1, 1, {255, 255, 255, 255}},
        {1, 1, {128, 128, 255, 255}},
        {1, 1, {255, 255, 255, 255}},
        {1, 1, {0, 0, 0, 255}},
        {1, 1, {255, 255, 255, 255}}
    }};
    std::array<std::vector<assets::CpuTexture>, 5> materialMaps;
    std::array<std::vector<bool>, 5> materialMapPresent;
    std::vector<std::array<std::filesystem::path, 5>> materialPaths(materialCount);
    std::vector<UINT> materialTextureFlags(materialCount, 0U);
    for (UINT slot = 0; slot < materialMaps.size(); ++slot) {
        materialMaps[slot].resize(materialCount, neutralTextures[slot]);
        materialMapPresent[slot].resize(materialCount, false);
    }
    for (std::size_t materialIndex = 0; materialIndex < materialCount && modelLoaded; ++materialIndex) {
        const assets::CpuMaterial& material = model.materials[materialIndex];
        materialPaths[materialIndex][0] = material.diffuseTexture;
        materialPaths[materialIndex][1] = material.normalTexture;
        materialPaths[materialIndex][2] = material.roughnessTexture;
        materialPaths[materialIndex][3] = material.packedMetallicRoughness
            ? std::filesystem::path{} : material.metallicTexture;
        materialPaths[materialIndex][4] = material.ambientOcclusionTexture;
        for (UINT slot = 0; slot < materialMaps.size(); ++slot) {
            const auto& path = materialPaths[materialIndex][slot];
            if (path.empty() || !std::filesystem::exists(path)) continue;
            assets::CpuTexture loadedTexture;
            if (assets::LoadWicTexture(path, loadedTexture) &&
                loadedTexture.width > 0 && loadedTexture.height > 0 &&
                loadedTexture.rgba.size() >= static_cast<std::size_t>(loadedTexture.width) * loadedTexture.height * 4U) {
                materialMaps[slot][materialIndex] = std::move(loadedTexture);
                materialMapPresent[slot][materialIndex] = true;
            } else {
                LogError("Direct3D 12: failed to decode an imported material texture; using a neutral fallback for that map.");
            }
        }
        if (materialMapPresent[0][materialIndex]) materialTextureFlags[materialIndex] |= 1U;
        if (materialMapPresent[1][materialIndex]) materialTextureFlags[materialIndex] |= 2U;
        if (materialMapPresent[2][materialIndex]) materialTextureFlags[materialIndex] |= 4U;
        if (materialMapPresent[3][materialIndex]) materialTextureFlags[materialIndex] |= 8U;
        if (materialMapPresent[4][materialIndex]) materialTextureFlags[materialIndex] |= 16U;
        if (material.packedMetallicRoughness && materialMapPresent[2][materialIndex])
            materialTextureFlags[materialIndex] |= 32U;
    }
    const UINT textureResolutionCap = materialCount <= 8U ? 1024U :
        (materialCount <= 24U ? 512U : (materialCount <= 64U ? 256U : 128U));
    for (UINT slot = 0; slot < materialMaps.size(); ++slot) {
        for (std::size_t layer = 0; layer < materialCount; ++layer) {
            if (materialMapPresent[slot][layer]) {
                materialTextureResolution = std::max(materialTextureResolution,
                    std::max(materialMaps[slot][layer].width, materialMaps[slot][layer].height));
            }
        }
    }
    materialTextureResolution = std::clamp(materialTextureResolution, 1U, textureResolutionCap);
    for (UINT slot = 0; slot < materialMaps.size(); ++slot) {
        std::vector<std::uint8_t> textureLayers;
        const std::size_t layerBytes = static_cast<std::size_t>(materialTextureResolution) * materialTextureResolution * 4U;
        textureLayers.reserve(layerBytes * materialCount);
        for (std::size_t layer = 0; layer < materialCount; ++layer) {
            auto resampled = ResampleTexture(materialMaps[slot][layer], materialTextureResolution,
                materialTextureResolution, slot == 0U);
            textureLayers.insert(textureLayers.end(), resampled.begin(), resampled.end());
        }
        if (!CreateModelTextureArray(materialTextureResolution, materialTextureResolution,
                static_cast<UINT>(materialCount), textureLayers, 7U + slot, slot == 0U)) return false;
    }

    if (modelLoaded) {
        constexpr float modelScale = 1.8F;
        constexpr float modelX = 8.0F;
        constexpr float modelZ = 7.0F;
        const float modelY = TerrainHeight(modelX, modelZ) + 0.025F;
        bool validModel = !model.vertices.empty() && !model.indices.empty();
        for (const auto& submesh : model.submeshes) {
            if (submesh.materialIndex >= model.materials.size() ||
                static_cast<std::uint64_t>(submesh.firstIndex) + submesh.indexCount > model.indices.size() ||
                (submesh.indexCount % 3U) != 0U) {
                validModel = false;
                break;
            }
        }
        if (validModel) {
            for (const assets::CpuSubmesh& submesh : model.submeshes) {
                const assets::CpuMaterial& material = model.materials[submesh.materialIndex];
                const UINT flags = materialTextureFlags[submesh.materialIndex];
                for (UINT offset = 0; offset < submesh.indexCount; ++offset) {
                    const unsigned int sourceIndex = model.indices[submesh.firstIndex + offset];
                    if (sourceIndex >= model.vertices.size()) { validModel = false; break; }
                    const assets::ModelVertex& source = model.vertices[sourceIndex];
                    const unsigned int destinationIndex = static_cast<unsigned int>(vertices.size());
                    vertices.push_back({
                        {source.position.x * modelScale + modelX, source.position.y * modelScale + modelY,
                         source.position.z * modelScale + modelZ},
                        {source.normal.x, source.normal.y, source.normal.z},
                        {material.baseColor.x, material.baseColor.y, material.baseColor.z, static_cast<float>(submesh.materialIndex)},
                        {source.u, 1.0F - source.v}
                    });
                    materialParameters.push_back({std::clamp(material.roughness, 0.04F, 1.0F),
                        std::clamp(material.metallic, 0.0F, 1.0F), 1.0F, static_cast<float>(flags)});
                    indices.push_back(destinationIndex);
                }
                if (!validModel) break;
            }
            if (!validModel) LogError("Direct3D 12: imported model contained an invalid index; some malformed triangles were skipped.");
        } else {
            LogError("Direct3D 12: imported model has invalid geometry or material ranges; omitting it.");
        }
    }

    if (vertices.empty() || indices.empty() || indices.size() > UINT_MAX ||
        vertices.size() != materialParameters.size() || (indices.size() % 3U) != 0U) {
        LogError("Direct3D 12: procedural/imported scene mesh is empty or too large.");
        return false;
    }

    // Spatially sort triangles into 24 m cells. Each cell stores three camera LODs and two
    // independent shadow LODs. Vertex data stays shared, so LODs only duplicate indices.
    constexpr float chunkSize = 24.0F;
    float minX = vertices.front().position[0], maxX = minX;
    float minZ = vertices.front().position[2], maxZ = minZ;
    for (const auto& vertex : vertices) {
        minX = std::min(minX, vertex.position[0]); maxX = std::max(maxX, vertex.position[0]);
        minZ = std::min(minZ, vertex.position[2]); maxZ = std::max(maxZ, vertex.position[2]);
    }
    const int chunksX = std::max(1, static_cast<int>(std::ceil((maxX - minX) / chunkSize)));
    const int chunksZ = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / chunkSize)));
    struct CpuChunk {
        std::array<std::vector<unsigned int>, 5> lodIndices;
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        bool occupied = false;
    };
    std::vector<CpuChunk> cpuChunks(static_cast<std::size_t>(chunksX) * chunksZ);
    for (std::size_t tri = 0; tri < indices.size(); tri += 3) {
        const unsigned int ia = indices[tri], ib = indices[tri + 1], ic = indices[tri + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            LogError("Direct3D 12: generated mesh contained an out-of-range index.");
            return false;
        }
        const auto& a = vertices[ia]; const auto& b = vertices[ib]; const auto& c = vertices[ic];
        const float cx = (a.position[0] + b.position[0] + c.position[0]) / 3.0F;
        const float cz = (a.position[2] + b.position[2] + c.position[2]) / 3.0F;
        const int chunkX = std::clamp(static_cast<int>((cx - minX) / chunkSize), 0, chunksX - 1);
        const int chunkZ = std::clamp(static_cast<int>((cz - minZ) / chunkSize), 0, chunksZ - 1);
        CpuChunk& chunk = cpuChunks[static_cast<std::size_t>(chunkZ) * chunksX + chunkX];
        chunk.occupied = true;
        chunk.minY = std::min({chunk.minY, a.position[1], b.position[1], c.position[1]});
        chunk.maxY = std::max({chunk.maxY, a.position[1], b.position[1], c.position[1]});
        const Vec3 edgeA{b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const Vec3 edgeB{c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const float triangleArea = edgeA.Cross(edgeB).Length() * 0.5F;
        const float red = (a.color[0] + b.color[0] + c.color[0]) / 3.0F;
        const float green = (a.color[1] + b.color[1] + c.color[1]) / 3.0F;
        const float blue = (a.color[2] + b.color[2] + c.color[2]) / 3.0F;
        // Terrain is also predominantly green, so color alone must never classify it
        // as foliage: doing so keeps the terrain in every LOD only by accident, and makes
        // stronger far-vegetation culling capable of punching holes in the ground.
        const bool terrainTriangle = ia < terrainVertexCount && ib < terrainVertexCount && ic < terrainVertexCount;
        const bool foliageTriangle = !terrainTriangle && green > red * 1.12F && green > blue * 1.10F;
        chunk.lodIndices[0].insert(chunk.lodIndices[0].end(), {ia, ib, ic});
        // Camera LODs preserve the existing scene detail policy.
        if (!foliageTriangle || triangleArea >= 0.10F)
            chunk.lodIndices[1].insert(chunk.lodIndices[1].end(), {ia, ib, ic});
        if (!foliageTriangle || triangleArea >= 0.50F)
            chunk.lodIndices[2].insert(chunk.lodIndices[2].end(), {ia, ib, ic});

        // Shadow-only LODs are intentionally independent of camera LODs. Fine leaf,
        // twig and trim triangles often cover less than a shadow texel in the mid/far
        // cascades; submitting them costs GPU time without adding stable shadow detail.
        // Keep the terrain intact so the ground never develops holes.
        if (terrainTriangle || triangleArea >= (foliageTriangle ? 0.22F : 0.08F))
            chunk.lodIndices[3].insert(chunk.lodIndices[3].end(), {ia, ib, ic});
        if (terrainTriangle || triangleArea >= (foliageTriangle ? 0.75F : 0.32F))
            chunk.lodIndices[4].insert(chunk.lodIndices[4].end(), {ia, ib, ic});
    }

    std::vector<unsigned int> sortedIndices;
    sortedIndices.reserve(indices.size());
    m_drawChunks.clear();
    for (int z = 0; z < chunksZ; ++z) {
        for (int x = 0; x < chunksX; ++x) {
            CpuChunk& sourceChunk = cpuChunks[static_cast<std::size_t>(z) * chunksX + x];
            if (!sourceChunk.occupied || sourceChunk.lodIndices[0].empty()) continue;
            DrawChunk chunk{};
            chunk.centerX = minX + (static_cast<float>(x) + 0.5F) * chunkSize;
            chunk.centerY = (sourceChunk.minY + sourceChunk.maxY) * 0.5F;
            chunk.centerZ = minZ + (static_cast<float>(z) + 0.5F) * chunkSize;
            const float halfHeight = (sourceChunk.maxY - sourceChunk.minY) * 0.5F;
            chunk.radius = std::sqrt(chunkSize * chunkSize * 0.5F + halfHeight * halfHeight) + 2.0F;
            for (UINT lod = 0; lod < 5; ++lod) {
                chunk.startIndex[lod] = static_cast<UINT>(sortedIndices.size());
                chunk.indexCount[lod] = static_cast<UINT>(sourceChunk.lodIndices[lod].size());
                sortedIndices.insert(sortedIndices.end(), sourceChunk.lodIndices[lod].begin(), sourceChunk.lodIndices[lod].end());
            }
            m_drawChunks.push_back(chunk);
        }
    }
    indices = std::move(sortedIndices);
    if (indices.empty() || indices.size() > UINT_MAX || vertices.size() > UINT_MAX / sizeof(D3D12WorldVertex)) {
        LogError("Direct3D 12: spatial LOD index generation produced an invalid mesh.");
        return false;
    }

    std::vector<D3D12WorldVertex> gpuVertices;
    gpuVertices.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const world::WorldVertex& source = vertices[i];
        D3D12WorldVertex vertex{};
        std::copy(std::begin(source.position), std::end(source.position), std::begin(vertex.position));
        std::copy(std::begin(source.normal), std::end(source.normal), std::begin(vertex.normal));
        std::copy(std::begin(source.color), std::end(source.color), std::begin(vertex.color));
        std::copy(std::begin(source.uv), std::end(source.uv), std::begin(vertex.uv));
        std::copy(materialParameters[i].begin(), materialParameters[i].end(), std::begin(vertex.material));
        gpuVertices.push_back(vertex);
    }
    const UINT64 vertexBytes = static_cast<UINT64>(gpuVertices.size()) * sizeof(D3D12WorldVertex);
    const UINT64 indexBytes = static_cast<UINT64>(indices.size()) * sizeof(unsigned int);
    if (vertexBytes == 0U || indexBytes == 0U || vertexBytes > UINT_MAX || indexBytes > UINT_MAX ||
        !CreateDefaultBuffer(m_device.Get(), vertexBytes, m_vertexBuffer) ||
        !CreateDefaultBuffer(m_device.Get(), indexBytes, m_indexBuffer)) {
        LogError("Direct3D 12: device-local scene geometry buffer allocation failed.");
        return false;
    }

    // Keep static geometry in DEFAULT heaps so the GPU reads it from device-local memory.
    // UPLOAD heaps are used only as short-lived staging resources during this initialization.
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexUpload;
    if (!CreateUploadBuffer(m_device.Get(), vertexBytes, vertexUpload) ||
        !CreateUploadBuffer(m_device.Get(), indexBytes, indexUpload)) {
        LogError("Direct3D 12: scene geometry staging-buffer allocation failed.");
        return false;
    }

    const D3D12_RANGE noCpuRead{0, 0};
    void* mapped = nullptr;
    if (FAILED(vertexUpload->Map(0, &noCpuRead, &mapped)) || mapped == nullptr) {
        LogError("Direct3D 12: vertex staging-buffer mapping failed.");
        return false;
    }
    std::memcpy(mapped, gpuVertices.data(), static_cast<std::size_t>(vertexBytes));
    const D3D12_RANGE vertexWritten{0, static_cast<SIZE_T>(vertexBytes)};
    vertexUpload->Unmap(0, &vertexWritten);

    mapped = nullptr;
    if (FAILED(indexUpload->Map(0, &noCpuRead, &mapped)) || mapped == nullptr) {
        LogError("Direct3D 12: index staging-buffer mapping failed.");
        return false;
    }
    std::memcpy(mapped, indices.data(), static_cast<std::size_t>(indexBytes));
    const D3D12_RANGE indexWritten{0, static_cast<SIZE_T>(indexBytes)};
    indexUpload->Unmap(0, &indexWritten);

    // Both copies share one upload submission and one fence wait. The staging resources
    // remain alive until the fence completes, then their ComPtrs release them automatically.
    if (FAILED(m_commandAllocators[0]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr))) {
        LogError("Direct3D 12: scene geometry upload command reset failed.");
        return false;
    }
    m_commandList->CopyBufferRegion(m_vertexBuffer.Get(), 0, vertexUpload.Get(), 0, vertexBytes);
    m_commandList->CopyBufferRegion(m_indexBuffer.Get(), 0, indexUpload.Get(), 0, indexBytes);

    D3D12_RESOURCE_BARRIER geometryBarriers[2]{};
    geometryBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    geometryBarriers[0].Transition.pResource = m_vertexBuffer.Get();
    geometryBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    geometryBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    geometryBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    geometryBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    geometryBarriers[1].Transition.pResource = m_indexBuffer.Get();
    geometryBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    geometryBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    geometryBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(2, geometryBarriers);

    if (FAILED(m_commandList->Close())) {
        LogError("Direct3D 12: scene geometry upload command-list close failed.");
        return false;
    }
    ID3D12CommandList* uploadLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, uploadLists);
    if (!WaitForGpu()) {
        LogError("Direct3D 12: scene geometry upload fence wait failed.");
        return false;
    }

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
    m_vertexBufferView.StrideInBytes = sizeof(D3D12WorldVertex);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = static_cast<UINT>(indexBytes);
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexCount = static_cast<UINT>(indices.size());

    char uploadSummary[192]{};
    std::snprintf(uploadSummary, sizeof(uploadSummary),
        "Direct3D 12: static geometry now resides in DEFAULT heaps (%.2f MiB vertices, %.2f MiB indices).",
        static_cast<double>(vertexBytes) / (1024.0 * 1024.0),
        static_cast<double>(indexBytes) / (1024.0 * 1024.0));
    LogInfo(uploadSummary);
    LogInfo("Direct3D 12: loaded procedural environment and imported base-color PBR model geometry.");
    return true;
}


bool D3D12Renderer::CreateIndirectDrawResources()
{
    if (m_drawChunks.empty() || m_drawChunks.size() > UINT_MAX) {
        LogError("Direct3D 12: indirect draw setup requires a valid chunk count.");
        return false;
    }

    D3D12_INDIRECT_ARGUMENT_DESC drawArgument{};
    drawArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC signatureDescription{};
    signatureDescription.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signatureDescription.NumArgumentDescs = 1;
    signatureDescription.pArgumentDescs = &drawArgument;
    if (FAILED(m_device->CreateCommandSignature(&signatureDescription, nullptr,
            IID_PPV_ARGS(m_drawCommandSignature.GetAddressOf())))) {
        LogError("Direct3D 12: indexed-draw command signature creation failed.");
        return false;
    }

    const UINT64 commandBufferBytes =
        static_cast<UINT64>(m_drawChunks.size()) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    const D3D12_RANGE noCpuRead{0, 0};
    for (UINT frame = 0; frame < kFrameCount; ++frame) {
        for (UINT stream = 0; stream < kIndirectStreamCount; ++stream) {
            auto& resource = m_indirectArgumentBuffers[frame][stream];
            if (!CreateUploadBuffer(m_device.Get(), commandBufferBytes, resource)) {
                LogError("Direct3D 12: indirect argument buffer allocation failed.");
                return false;
            }

            void* mapped = nullptr;
            if (FAILED(resource->Map(0, &noCpuRead, &mapped)) || mapped == nullptr) {
                LogError("Direct3D 12: indirect argument buffer mapping failed.");
                return false;
            }
            m_mappedIndirectArguments[frame][stream] =
                static_cast<D3D12_DRAW_INDEXED_ARGUMENTS*>(mapped);
        }
    }

    char message[192]{};
    std::snprintf(message, sizeof(message),
        "Direct3D 12: ExecuteIndirect batching enabled (%zu chunks, %llu bytes per stream, %u frame resources).",
        m_drawChunks.size(), static_cast<unsigned long long>(commandBufferBytes),
        kFrameCount * kIndirectStreamCount);
    LogInfo(message);
    return true;
}



bool D3D12Renderer::CreateGpuCullResources()
{
    const auto disableGpuCulling = [this](const char* reason) {
        LogError(reason);
        m_gpuCullingEnabled = false;
        for (UINT frame = 0; frame < kFrameCount; ++frame) {
            if (m_mappedGpuCullConstants[frame] != nullptr && m_gpuCullConstantBuffers[frame]) {
                m_gpuCullConstantBuffers[frame]->Unmap(0, nullptr);
                m_mappedGpuCullConstants[frame] = nullptr;
            }
            if (m_mappedGpuCullStats[frame] != nullptr && m_gpuCullStatsReadbackBuffers[frame]) {
                m_gpuCullStatsReadbackBuffers[frame]->Unmap(0, nullptr);
                m_mappedGpuCullStats[frame] = nullptr;
            }
            m_gpuCullConstantBuffers[frame].Reset();
            m_gpuCullStatsReadbackBuffers[frame].Reset();
            for (UINT stream = 0; stream < kIndirectStreamCount; ++stream) {
                m_gpuCullArgumentBuffers[frame][stream].Reset();
                m_gpuCullCounterBuffers[frame][stream].Reset();
            }
        }
        m_gpuCullChunkBuffer.Reset();
        m_zeroCounterBuffer.Reset();
        m_gpuCullRootSignature.Reset();
        m_gpuCullPipelineState.Reset();
        m_gpuCullStatsPending.fill(false);
        return false;
    };

    if (m_drawChunks.empty() || m_drawChunks.size() > UINT_MAX)
        return disableGpuCulling("Direct3D 12: GPU culling rejected an invalid chunk count.");

    Microsoft::WRL::ComPtr<ID3DBlob> computeShader;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT result = D3DCompile(kGpuCullShader, sizeof(kGpuCullShader) - 1,
        "vanta_d3d12_gpu_cull", nullptr, nullptr, "CullCS", "cs_5_0",
        compileFlags, 0, computeShader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        return disableGpuCulling("Direct3D 12: GPU culling compute shader compilation failed.");
    }

    D3D12_DESCRIPTOR_RANGE hiZRange{};
    hiZRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    hiZRange.NumDescriptors = 1;
    hiZRange.BaseShaderRegister = 1;
    hiZRange.RegisterSpace = 0;
    hiZRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rootParameters[5]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameters[2].Descriptor.ShaderRegister = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rootParameters[3].Descriptor.ShaderRegister = 1;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &hiZRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = static_cast<UINT>(std::size(rootParameters));
    rootDescription.pParameters = rootParameters;
    rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    errors.Reset();
    result = D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRoot.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        return disableGpuCulling("Direct3D 12: GPU culling root-signature serialization failed.");
    }
    if (FAILED(m_device->CreateRootSignature(0, serializedRoot->GetBufferPointer(),
            serializedRoot->GetBufferSize(), IID_PPV_ARGS(m_gpuCullRootSignature.GetAddressOf())))) {
        return disableGpuCulling("Direct3D 12: GPU culling root-signature creation failed.");
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDescription{};
    computeDescription.pRootSignature = m_gpuCullRootSignature.Get();
    computeDescription.CS.pShaderBytecode = computeShader->GetBufferPointer();
    computeDescription.CS.BytecodeLength = computeShader->GetBufferSize();
    if (FAILED(m_device->CreateComputePipelineState(&computeDescription,
            IID_PPV_ARGS(m_gpuCullPipelineState.GetAddressOf())))) {
        return disableGpuCulling("Direct3D 12: GPU culling compute pipeline creation failed.");
    }

    const UINT64 argumentBytes =
        static_cast<UINT64>(m_drawChunks.size()) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    constexpr UINT64 counterBytes = sizeof(UINT) * 3U;
    const UINT64 statsBytes = static_cast<UINT64>(kIndirectStreamCount) * counterBytes;
    const D3D12_RANGE noCpuRead{0, 0};
    for (UINT frame = 0; frame < kFrameCount; ++frame) {
        if (!CreateUploadBuffer(m_device.Get(),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * kIndirectStreamCount,
                m_gpuCullConstantBuffers[frame])) {
            return disableGpuCulling("Direct3D 12: GPU culling constant-buffer allocation failed.");
        }
        void* constants = nullptr;
        if (FAILED(m_gpuCullConstantBuffers[frame]->Map(0, &noCpuRead, &constants)) || constants == nullptr)
            return disableGpuCulling("Direct3D 12: GPU culling constant-buffer mapping failed.");
        m_mappedGpuCullConstants[frame] = constants;

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        readbackHeap.CreationNodeMask = 1;
        readbackHeap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC readbackDescription{};
        readbackDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDescription.Width = statsBytes;
        readbackDescription.Height = 1;
        readbackDescription.DepthOrArraySize = 1;
        readbackDescription.MipLevels = 1;
        readbackDescription.SampleDesc.Count = 1;
        readbackDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
                &readbackDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(m_gpuCullStatsReadbackBuffers[frame].GetAddressOf())))) {
            return disableGpuCulling("Direct3D 12: GPU culling statistics readback allocation failed.");
        }
        D3D12_RANGE statsReadRange{0, static_cast<SIZE_T>(statsBytes)};
        void* stats = nullptr;
        if (FAILED(m_gpuCullStatsReadbackBuffers[frame]->Map(0, &statsReadRange, &stats)) || stats == nullptr)
            return disableGpuCulling("Direct3D 12: GPU culling statistics readback mapping failed.");
        m_mappedGpuCullStats[frame] = static_cast<UINT*>(stats);

        for (UINT stream = 0; stream < kIndirectStreamCount; ++stream) {
            if (!CreateDefaultUavBuffer(m_device.Get(), argumentBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    m_gpuCullArgumentBuffers[frame][stream]) ||
                !CreateDefaultUavBuffer(m_device.Get(), counterBytes, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    m_gpuCullCounterBuffers[frame][stream])) {
                return disableGpuCulling("Direct3D 12: GPU-generated indirect buffer allocation failed.");
            }
        }
    }

    if (!CreateUploadBuffer(m_device.Get(), counterBytes, m_zeroCounterBuffer))
        return disableGpuCulling("Direct3D 12: GPU culling counter-reset buffer allocation failed.");
    void* zeroBytes = nullptr;
    if (FAILED(m_zeroCounterBuffer->Map(0, &noCpuRead, &zeroBytes)) || zeroBytes == nullptr)
        return disableGpuCulling("Direct3D 12: GPU culling counter-reset buffer mapping failed.");
    std::memset(zeroBytes, 0, static_cast<std::size_t>(counterBytes));
    const D3D12_RANGE zeroWritten{0, static_cast<SIZE_T>(counterBytes)};
    m_zeroCounterBuffer->Unmap(0, &zeroWritten);

    std::vector<GpuDrawChunk> gpuChunks;
    gpuChunks.reserve(m_drawChunks.size());
    for (const DrawChunk& source : m_drawChunks) {
        GpuDrawChunk chunk{};
        chunk.centerRadius[0] = source.centerX;
        chunk.centerRadius[1] = source.centerY;
        chunk.centerRadius[2] = source.centerZ;
        chunk.centerRadius[3] = source.radius;
        std::copy(source.startIndex.begin(), source.startIndex.end(), std::begin(chunk.startIndex));
        std::copy(source.indexCount.begin(), source.indexCount.end(), std::begin(chunk.indexCount));
        gpuChunks.push_back(chunk);
    }
    const UINT64 chunkBytes = static_cast<UINT64>(gpuChunks.size()) * sizeof(GpuDrawChunk);
    Microsoft::WRL::ComPtr<ID3D12Resource> chunkUpload;
    if (!CreateDefaultBuffer(m_device.Get(), chunkBytes, m_gpuCullChunkBuffer) ||
        !CreateUploadBuffer(m_device.Get(), chunkBytes, chunkUpload)) {
        return disableGpuCulling("Direct3D 12: GPU culling chunk-data allocation failed.");
    }
    void* mappedChunks = nullptr;
    if (FAILED(chunkUpload->Map(0, &noCpuRead, &mappedChunks)) || mappedChunks == nullptr)
        return disableGpuCulling("Direct3D 12: GPU culling chunk-data mapping failed.");
    std::memcpy(mappedChunks, gpuChunks.data(), static_cast<std::size_t>(chunkBytes));
    const D3D12_RANGE chunkWritten{0, static_cast<SIZE_T>(chunkBytes)};
    chunkUpload->Unmap(0, &chunkWritten);

    if (FAILED(m_commandAllocators[0]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr))) {
        return disableGpuCulling("Direct3D 12: GPU culling upload command reset failed.");
    }
    m_commandList->CopyBufferRegion(m_gpuCullChunkBuffer.Get(), 0, chunkUpload.Get(), 0, chunkBytes);
    D3D12_RESOURCE_BARRIER chunkBarrier{};
    chunkBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    chunkBarrier.Transition.pResource = m_gpuCullChunkBuffer.Get();
    chunkBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    chunkBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    chunkBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &chunkBarrier);
    if (FAILED(m_commandList->Close()))
        return disableGpuCulling("Direct3D 12: GPU culling upload command-list close failed.");
    ID3D12CommandList* uploadLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, uploadLists);
    if (!WaitForGpu())
        return disableGpuCulling("Direct3D 12: GPU culling chunk upload fence wait failed.");

    m_gpuCullingEnabled = true;
    m_gpuCullStatsPending.fill(false);
    char message[192]{};
    std::snprintf(message, sizeof(message),
        "Direct3D 12: GPU compute culling enabled (%zu chunks, 4 per-frame argument/counter streams).",
        m_drawChunks.size());
    LogInfo(message);
    return true;
}



void D3D12Renderer::BuildHiZPyramid(UINT frameIndex)
{
    if (!m_gpuCullingEnabled || m_hiZMipLevels == 0U ||
        !m_hiZTextures[frameIndex] || !m_hiZConstantBuffers[frameIndex] ||
        m_mappedHiZConstants[frameIndex] == nullptr)
        return;

    D3D12_RESOURCE_BARRIER toUav{};
    toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toUav.Transition.pResource = m_hiZTextures[frameIndex].Get();
    toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toUav);

    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetComputeRootSignature(m_hiZRootSignature.Get());
    const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

    const auto writeConstants = [this, frameIndex](UINT mip, UINT sourceWidth, UINT sourceHeight,
                                                    UINT destinationWidth, UINT destinationHeight,
                                                    UINT sourceMip) {
        HiZBuildConstants constants{};
        constants.sourceWidth = sourceWidth;
        constants.sourceHeight = sourceHeight;
        constants.destinationWidth = destinationWidth;
        constants.destinationHeight = destinationHeight;
        constants.sourceMip = sourceMip;
        auto* destination = static_cast<unsigned char*>(m_mappedHiZConstants[frameIndex]) +
            static_cast<std::size_t>(mip) * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        std::memcpy(destination, &constants, sizeof(constants));
        return m_hiZConstantBuffers[frameIndex]->GetGPUVirtualAddress() +
            static_cast<UINT64>(mip) * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    };

    D3D12_GPU_DESCRIPTOR_HANDLE destinationHandle = heapStart;
    destinationHandle.ptr += static_cast<UINT64>(kHiZBaseUavIndex +
        frameIndex * kHiZMipCapacity) * m_srvDescriptorSize;
    m_commandList->SetPipelineState(m_hiZCopyPipelineState.Get());
    m_commandList->SetComputeRootConstantBufferView(0,
        writeConstants(0U, m_width, m_height, m_width, m_height, 0U));
    D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle = heapStart;
    sourceHandle.ptr += static_cast<UINT64>(1U) * m_srvDescriptorSize; // Scene depth SRV.
    m_commandList->SetComputeRootDescriptorTable(1, sourceHandle);
    m_commandList->SetComputeRootDescriptorTable(2, destinationHandle);
    m_commandList->Dispatch((m_width + 7U) / 8U, (m_height + 7U) / 8U, 1U);

    D3D12_RESOURCE_BARRIER mipReady{};
    mipReady.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    mipReady.Transition.pResource = m_hiZTextures[frameIndex].Get();
    mipReady.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    mipReady.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    mipReady.Transition.Subresource = 0U;
    m_commandList->ResourceBarrier(1, &mipReady);

    UINT sourceWidth = m_width;
    UINT sourceHeight = m_height;
    for (UINT mip = 1U; mip < m_hiZMipLevels; ++mip) {
        const UINT destinationWidth = std::max(1U, sourceWidth >> 1U);
        const UINT destinationHeight = std::max(1U, sourceHeight >> 1U);
        const UINT64 constantAddress = writeConstants(mip, sourceWidth, sourceHeight,
                                                        destinationWidth, destinationHeight, mip - 1U);
        sourceHandle = heapStart;
        sourceHandle.ptr += static_cast<UINT64>(kHiZBaseSrvIndex + frameIndex) * m_srvDescriptorSize;
        destinationHandle = heapStart;
        destinationHandle.ptr += static_cast<UINT64>(kHiZBaseUavIndex +
            frameIndex * kHiZMipCapacity + mip) * m_srvDescriptorSize;

        m_commandList->SetPipelineState(m_hiZDownsamplePipelineState.Get());
        m_commandList->SetComputeRootConstantBufferView(0, constantAddress);
        m_commandList->SetComputeRootDescriptorTable(1, sourceHandle);
        m_commandList->SetComputeRootDescriptorTable(2, destinationHandle);
        m_commandList->Dispatch((destinationWidth + 7U) / 8U,
                                (destinationHeight + 7U) / 8U, 1U);

        mipReady.Transition.Subresource = mip;
        m_commandList->ResourceBarrier(1, &mipReady);
        sourceWidth = destinationWidth;
        sourceHeight = destinationHeight;
    }
}


bool D3D12Renderer::WaitForFrame(UINT frameIndex)
{
    const UINT64 completedValue = m_fence->GetCompletedValue();
    if (completedValue == std::numeric_limits<UINT64>::max()) {
        m_deviceLost = true;
        return false;
    }
    if (completedValue >= m_fenceValues[frameIndex]) return true;
    if (FAILED(m_fence->SetEventOnCompletion(m_fenceValues[frameIndex], m_fenceEvent))) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(m_fenceEvent, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        return false;
    }
    return true;
}

bool D3D12Renderer::WaitForGpu()
{
    if (!m_commandQueue || !m_fence || m_fenceEvent == nullptr) return true;
    const UINT64 value = m_nextFenceValue++;
    if (FAILED(m_commandQueue->Signal(m_fence.Get(), value))) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        return false;
    }
    if (FAILED(m_fence->SetEventOnCompletion(value, m_fenceEvent))) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        return false;
    }
    if (WaitForSingleObject(m_fenceEvent, INFINITE) != WAIT_OBJECT_0) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        return false;
    }
    m_fenceValues.fill(value);
    return true;
}

bool D3D12Renderer::Resize(int width, int height)
{
    if (m_failed) return false;
    if (width <= 0 || height <= 0 ||
        (m_width == static_cast<UINT>(width) && m_height == static_cast<UINT>(height))) return true;
    if (!WaitForGpu()) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        LogError("Direct3D 12: GPU wait failed before resize.");
        return false;
    }
    for (auto& target : m_renderTargets) target.Reset();
    m_depthTexture.Reset();
    m_hdrTexture.Reset();
    for (auto& history : m_historyColorTextures) history.Reset();
    for (auto& history : m_historyDepthTextures) history.Reset();
    const HRESULT resizeResult = m_swapChain->ResizeBuffers(kFrameCount, static_cast<UINT>(width),
            static_cast<UINT>(height), DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(resizeResult)) {
        m_deviceLost = resizeResult == DXGI_ERROR_DEVICE_REMOVED || resizeResult == DXGI_ERROR_DEVICE_RESET ||
            (m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason()));
        char message[160]{};
        std::snprintf(message, sizeof(message), "Direct3D 12: swap-chain resize failed (HRESULT 0x%08lX).",
            static_cast<unsigned long>(resizeResult));
        LogError(message);
        return false;
    }
    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);
    if (!CreateRenderTargets() || !CreateDepthTarget() || !CreateHdrTarget() || !CreateTemporalTargets()) return false;
    if (!CreateHiZResources()) {
        m_gpuCullingEnabled = false;
        LogInfo("Direct3D 12: Hi-Z resize allocation failed; falling back to CPU culling.");
    }
    m_hasPreviousHiZ = false;
    m_historyValid = false;
    m_historyWriteIndex = 0;
    m_previousViewProjection = Mat4::Identity();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}


bool D3D12Renderer::InitializeGpuProfiler() noexcept
{
    if (FAILED(m_commandQueue->GetTimestampFrequency(&m_timestampFrequency)) || m_timestampFrequency == 0) {
        LogError("D3D12 GPU profiler unavailable: timestamp frequency query failed.");
        return false;
    }
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Count = kFrameCount * 7U;
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    if (FAILED(m_device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(m_timestampQueryHeap.GetAddressOf())))) {
        LogError("D3D12 GPU profiler unavailable: timestamp query heap creation failed.");
        m_timestampFrequency = 0;
        return false;
    }
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC bufferDescription{};
    bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDescription.Width = static_cast<UINT64>(kFrameCount * 7U) * sizeof(UINT64);
    bufferDescription.Height = 1;
    bufferDescription.DepthOrArraySize = 1;
    bufferDescription.MipLevels = 1;
    bufferDescription.SampleDesc.Count = 1;
    bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
            &bufferDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(m_timestampReadback.GetAddressOf())))) {
        LogError("D3D12 GPU profiler unavailable: readback buffer creation failed.");
        m_timestampQueryHeap.Reset();
        m_timestampFrequency = 0;
        return false;
    }
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(kFrameCount * 7U * sizeof(UINT64))};
    void* mapped = nullptr;
    if (FAILED(m_timestampReadback->Map(0, &readRange, &mapped)) || mapped == nullptr) {
        LogError("D3D12 GPU profiler unavailable: readback buffer mapping failed.");
        m_timestampReadback.Reset();
        m_timestampQueryHeap.Reset();
        m_timestampFrequency = 0;
        return false;
    }
    m_mappedTimestamps = static_cast<UINT64*>(mapped);
    LogInfo("D3D12 GPU profiler enabled: per-cascade shadows, scene, temporal resolve, post-process and total GPU timings (averaged every 120 completed frames).");
    return true;
}

void D3D12Renderer::ConsumeGpuProfile(UINT frameIndex) noexcept
{
    if (m_gpuCullingEnabled && m_gpuCullStatsPending[frameIndex] &&
        m_mappedGpuCullStats[frameIndex] != nullptr) {
        const UINT* stats = m_mappedGpuCullStats[frameIndex];
        for (UINT stream = 0; stream < kIndirectStreamCount; ++stream) {
            const std::uint64_t draws = stats[stream * 3U];
            const std::uint64_t triangles = stats[stream * 3U + 1U];
            const std::uint64_t hiZOccluded = stats[stream * 3U + 2U];
            if (stream < 3U) {
                m_shadowDrawCallsAccumulated[stream] += draws;
                m_shadowTrianglesAccumulated[stream] += triangles;
            } else {
                m_sceneDrawCallsAccumulated += draws;
                m_sceneHiZOcclusionRejectedAccumulated += hiZOccluded;
            }
        }
        m_gpuCullStatsPending[frameIndex] = false;
    }

    if (m_timestampFrequency == 0 || m_mappedTimestamps == nullptr || !m_profilePending[frameIndex])
        return;
    const std::size_t base = static_cast<std::size_t>(frameIndex) * 7U;
    const auto milliseconds = [this](UINT64 ticks) {
        return static_cast<double>(ticks) * 1000.0 / static_cast<double>(m_timestampFrequency);
    };
    for (std::size_t pass = 0; pass < 6U; ++pass)
        m_profileAccumulatedMs[pass] += milliseconds(m_mappedTimestamps[base + pass + 1U] - m_mappedTimestamps[base + pass]);
    const double totalGpuMilliseconds =
        milliseconds(m_mappedTimestamps[base + 6U] - m_mappedTimestamps[base]);
    (void)m_worldBudget.UpdateForGpuFrameSeconds(
        static_cast<float>(totalGpuMilliseconds / 1000.0));
    m_profileAccumulatedMs[6] += totalGpuMilliseconds;
    ++m_profileSampleCount;
    m_profilePending[frameIndex] = false;
    if (m_profileSampleCount >= 120U) {
        char message[420]{};
        const double divisor = static_cast<double>(m_profileSampleCount);
        std::snprintf(message, sizeof(message),
            "D3D12 GPU ms (avg %u frames): shadow[near %.2f, mid %.2f, far %.2f] | scene %.2f | temporal %.2f | post %.2f | total %.2f (60 FPS budget 16.67 ms)",
            m_profileSampleCount, m_profileAccumulatedMs[0] / divisor, m_profileAccumulatedMs[1] / divisor,
            m_profileAccumulatedMs[2] / divisor, m_profileAccumulatedMs[3] / divisor,
            m_profileAccumulatedMs[4] / divisor, m_profileAccumulatedMs[5] / divisor,
            m_profileAccumulatedMs[6] / divisor);
        LogInfo(message);
        char shadowStats[448]{};
        std::snprintf(shadowStats, sizeof(shadowStats),
            "D3D12 GPU-cull averages (%u frames): scene %llu draws | Hi-Z rejected %llu chunks, active %llu frames | shadow near %llu/%llu tris | mid %llu/%llu | far %llu/%llu",
            m_profileSampleCount,
            static_cast<unsigned long long>(m_sceneDrawCallsAccumulated / m_profileSampleCount),
            static_cast<unsigned long long>(m_sceneHiZOcclusionRejectedAccumulated / m_profileSampleCount),
            static_cast<unsigned long long>(m_hiZOcclusionActiveFrameAccumulated / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowDrawCallsAccumulated[0] / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowTrianglesAccumulated[0] / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowDrawCallsAccumulated[1] / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowTrianglesAccumulated[1] / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowDrawCallsAccumulated[2] / m_profileSampleCount),
            static_cast<unsigned long long>(m_shadowTrianglesAccumulated[2] / m_profileSampleCount));
        LogInfo(shadowStats);
        m_profileAccumulatedMs.fill(0.0);
        m_sceneDrawCallsAccumulated = 0;
        m_sceneHiZOcclusionRejectedAccumulated = 0;
        m_hiZOcclusionActiveFrameAccumulated = 0;
        m_shadowDrawCallsAccumulated.fill(0);
        m_shadowTrianglesAccumulated.fill(0);
        m_profileSampleCount = 0;
    }
}

void D3D12Renderer::Render(float elapsedSeconds, float cameraX, float cameraY, float cameraZ,
                           float cameraYaw, float cameraPitch)
{
    RenderInternal(elapsedSeconds, cameraX, cameraY, cameraZ, cameraYaw, cameraPitch, nullptr);
}

bool D3D12Renderer::Render(const renderer::SceneFrame& submittedScene)
{
    const float forwardX = submittedScene.view.view.At(2, 0);
    const float forwardY = std::clamp(submittedScene.view.view.At(2, 1), -1.0F, 1.0F);
    const float forwardZ = submittedScene.view.view.At(2, 2);
    RenderInternal(submittedScene.view.elapsedSeconds,
        submittedScene.view.cameraPosition.x, submittedScene.view.cameraPosition.y,
        submittedScene.view.cameraPosition.z, std::atan2(forwardX, forwardZ),
        std::asin(forwardY), &submittedScene);
    return m_lastFrameSucceeded;
}

void D3D12Renderer::RenderInternal(float elapsedSeconds, float cameraX, float cameraY, float cameraZ,
                           float cameraYaw, float cameraPitch,
                           const renderer::SceneFrame* submittedScene)
{
    m_lastFrameSucceeded = false;
    if (m_failed || m_deviceLost) return;
    if (!WaitForFrame(m_frameIndex)) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        LogError("Direct3D 12: waiting for a reusable frame failed.");
        m_failed = true;
        return;
    }

    renderer::SubmittedSceneGeometry submittedGeometry;
    const bool hasSubmittedScene = submittedScene != nullptr;
    D3D12_VERTEX_BUFFER_VIEW submittedVertexView{};
    D3D12_INDEX_BUFFER_VIEW submittedIndexView{};
    if (hasSubmittedScene) {
        std::string geometryError;
        if (!renderer::BuildSubmittedSceneGeometry(*submittedScene, submittedGeometry, geometryError)) {
            LogError(geometryError.c_str());
            return;
        }
        if (!PrepareSubmittedMaterialTextures(submittedGeometry, geometryError)) {
            LogError(geometryError.c_str());
            return;
        }
        if (!submittedGeometry.vertices.empty()) {
            const std::size_t vertexBytes = submittedGeometry.vertices.size() *
                sizeof(renderer::SubmittedSceneVertex);
            const std::size_t indexBytes = submittedGeometry.indices.size() * sizeof(std::uint32_t);
            if (vertexBytes > UINT_MAX || indexBytes > UINT_MAX) {
                LogError("Direct3D 12: submitted scene exceeds GPU buffer view limits.");
                return;
            }
            const auto createUploadBuffer = [this](const UINT64 byteCount,
                Microsoft::WRL::ComPtr<ID3D12Resource>& resource, UINT64& capacity,
                const void* source) {
                if (resource.Get() == nullptr || capacity < byteCount) {
                    UINT64 newCapacity = capacity == 0U ? 65536U : capacity;
                    while (newCapacity < byteCount && newCapacity <= UINT_MAX / 2U)
                        newCapacity *= 2U;
                    if (newCapacity < byteCount || newCapacity > UINT_MAX)
                        newCapacity = byteCount;

                    resource.Reset();
                    capacity = 0U;
                    D3D12_HEAP_PROPERTIES heap{};
                    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                    heap.CreationNodeMask = 1;
                    heap.VisibleNodeMask = 1;
                    D3D12_RESOURCE_DESC description{};
                    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    description.Width = newCapacity;
                    description.Height = 1;
                    description.DepthOrArraySize = 1;
                    description.MipLevels = 1;
                    description.SampleDesc.Count = 1;
                    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                        &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                        IID_PPV_ARGS(resource.GetAddressOf()))))
                        return false;
                    capacity = newCapacity;
                }

                void* mapped = nullptr;
                const D3D12_RANGE readRange{0, 0};
                if (FAILED(resource->Map(0, &readRange, &mapped)))
                    return false;
                std::memcpy(mapped, source, static_cast<std::size_t>(byteCount));
                const D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(byteCount)};
                resource->Unmap(0, &writtenRange);
                return true;
            };
            if (!createUploadBuffer(static_cast<UINT64>(vertexBytes),
                    m_submittedVertexBuffers[m_frameIndex],
                    m_submittedVertexBufferCapacities[m_frameIndex], submittedGeometry.vertices.data()) ||
                !createUploadBuffer(static_cast<UINT64>(indexBytes),
                    m_submittedIndexBuffers[m_frameIndex],
                    m_submittedIndexBufferCapacities[m_frameIndex], submittedGeometry.indices.data())) {
                LogError("Direct3D 12: submitted scene upload buffer creation or mapping failed.");
                return;
            }
            submittedVertexView.BufferLocation =
                m_submittedVertexBuffers[m_frameIndex]->GetGPUVirtualAddress();
            submittedVertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
            submittedVertexView.StrideInBytes = sizeof(renderer::SubmittedSceneVertex);
            submittedIndexView.BufferLocation =
                m_submittedIndexBuffers[m_frameIndex]->GetGPUVirtualAddress();
            submittedIndexView.SizeInBytes = static_cast<UINT>(indexBytes);
            submittedIndexView.Format = DXGI_FORMAT_R32_UINT;
        }
    }

    // Timestamp readback is consumed only after this frame's fence completes.
    // Prefer measured GPU work; use CPU cadence only when timestamp profiling is unavailable.
    ConsumeGpuProfile(m_frameIndex);
    if (m_timestampFrequency == 0) (void)m_worldBudget.Update();
    const float adaptiveDetailScale = m_worldBudget.DetailScale();
    const Vec3 eye{cameraX, cameraY, cameraZ};
    const Vec3 forward{std::sin(cameraYaw) * std::cos(cameraPitch), std::sin(cameraPitch),
                       std::cos(cameraYaw) * std::cos(cameraPitch)};
    Mat4 viewMatrix;
    if (submittedScene != nullptr) {
        viewMatrix = submittedScene->view.view;
    } else {
        const auto view = Mat4::LookAtLH(eye, eye + forward, {0.0F, 1.0F, 0.0F});
        if (!view) return;
        viewMatrix = *view;
    }
    Mat4 projection = submittedScene != nullptr ? submittedScene->view.projection :
        Mat4::Perspective(math::DegreesToRadians(68.0F),
            static_cast<float>(m_width) / static_cast<float>(m_height), 0.08F, 260.0F);
    const std::uint32_t jitterIndex = static_cast<std::uint32_t>(m_renderFrameNumber % 8U) + 1U;
    projection.At(0, 2) += (Halton(jitterIndex, 2U) - 0.5F) * 2.0F / static_cast<float>(m_width);
    projection.At(1, 2) += (Halton(jitterIndex, 3U) - 0.5F) * 2.0F / static_cast<float>(m_height);
    SceneConstants constants{};
    constants.viewProjection = projection * viewMatrix;
    // A slow, deterministic day cycle. The same sun vector drives direct lighting and
    // every cascade's light camera, so the visible sun and cast shadows stay in sync.
    constexpr float kDayCycleRadiansPerSecond = 0.012F;
    const float sunAngle = 0.95F + elapsedSeconds * kDayCycleRadiansPerSecond;
    const Vec3 lightDirection = Vec3{0.72F * std::cos(sunAngle), std::sin(sunAngle), -0.35F}
        .Normalized().value_or(Vec3{0.0F, 1.0F, 0.0F});
    const float sunIntensity = std::clamp((lightDirection.y + 0.10F) / 0.38F, 0.0F, 1.0F);
    constexpr float cascadeSplits[] = {28.0F, 80.0F, 220.0F};
    float previousSplit = 0.08F;
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        const float split = cascadeSplits[cascade];
        const float midpoint = (previousSplit + split) * 0.5F;
        const Vec3 center = eye + forward * midpoint;
        const float radius = split * (cascade == 0 ? 0.92F : 0.82F);
        const Vec3 lightForward = -lightDirection;
        const Vec3 worldUp{0.0F, 1.0F, 0.0F};
        const Vec3 lightRight = worldUp.Cross(lightForward).Normalized().value_or(Vec3{1.0F, 0.0F, 0.0F});
        const Vec3 lightUp = lightForward.Cross(lightRight).Normalized().value_or(worldUp);
        const UINT cascadeResolution = cascade == 0 ? kShadowResolution : kShadowResolution / 2U;
        const float shadowTexelWorldSize = (radius * 2.0F) / static_cast<float>(cascadeResolution);
        const float centerRight = center.Dot(lightRight);
        const float centerUp = center.Dot(lightUp);
        const Vec3 snappedCenter = center +
            lightRight * (std::round(centerRight / shadowTexelWorldSize) * shadowTexelWorldSize - centerRight) +
            lightUp * (std::round(centerUp / shadowTexelWorldSize) * shadowTexelWorldSize - centerUp);
        const Vec3 lightEye = snappedCenter + lightDirection * (radius * 2.4F + 30.0F);
        const auto lightView = Mat4::LookAtLH(lightEye, snappedCenter, worldUp);
        if (!lightView) return;
        const Mat4 lightProjection = OrthographicLH(radius * 2.0F, radius * 2.0F, 0.1F, radius * 5.0F + 40.0F);
        constants.lightViewProjection[cascade] = lightProjection * *lightView;
        previousSplit = split;
    }
    constants.cascadeSplits[0] = cascadeSplits[0];
    constants.cascadeSplits[1] = cascadeSplits[1];
    constants.cascadeSplits[2] = cascadeSplits[2];
    constants.cameraPosition[0] = cameraX;
    constants.cameraPosition[1] = cameraY;
    constants.cameraPosition[2] = cameraZ;
    // Reuse the otherwise-unused W component as deterministic shader wind time.
    constants.cameraPosition[3] = elapsedSeconds;
    constants.lighting[0] = lightDirection.x;
    constants.lighting[1] = lightDirection.y;
    constants.lighting[2] = lightDirection.z;
    constants.lighting[3] = sunIntensity;
    const float cameraDeltaX = cameraX - m_previousCameraX;
    const float cameraDeltaY = cameraY - m_previousCameraY;
    const float cameraDeltaZ = cameraZ - m_previousCameraZ;
    const bool cameraCut = (cameraDeltaX * cameraDeltaX + cameraDeltaY * cameraDeltaY + cameraDeltaZ * cameraDeltaZ > 64.0F) ||
        std::abs(cameraYaw - m_previousCameraYaw) > 0.7F || std::abs(cameraPitch - m_previousCameraPitch) > 0.7F;
    const bool hiZStableCamera = m_hasPreviousHiZ && !cameraCut &&
        cameraDeltaX * cameraDeltaX + cameraDeltaY * cameraDeltaY + cameraDeltaZ * cameraDeltaZ <= 0.000001F &&
        std::abs(cameraYaw - m_previousCameraYaw) <= 0.0005F &&
        std::abs(cameraPitch - m_previousCameraPitch) <= 0.0005F;
    if (m_gpuCullingEnabled && hiZStableCamera)
        ++m_hiZOcclusionActiveFrameAccumulated;
    if (cameraCut) m_historyValid = false;
    TemporalConstants temporalConstants{};
    if (!InvertMatrix(constants.viewProjection, temporalConstants.inverseViewProjection)) return;
    temporalConstants.previousViewProjection = m_previousViewProjection;
    temporalConstants.parameters[0] = m_historyValid ? 1.0F : 0.0F;
    temporalConstants.parameters[1] = static_cast<float>(1U - m_historyWriteIndex);
    temporalConstants.parameters[2] = static_cast<float>(m_historyWriteIndex);
    temporalConstants.parameters[3] = 0.88F;
    // Project the world-space sun direction into this camera's screen space for the sky pass.
    const Vec3 cameraRightForSky{std::cos(cameraYaw), 0.0F, -std::sin(cameraYaw)};
    const Vec3 cameraUpForSky = forward.Cross(cameraRightForSky).Normalized().value_or(Vec3{0.0F, 1.0F, 0.0F});
    const float sunForward = lightDirection.Dot(forward);
    const float sunDenominator = std::max(sunForward, 0.05F);
    const float tanHalfFov = std::tan(math::DegreesToRadians(34.0F));
    const float sunNdcX = lightDirection.Dot(cameraRightForSky) / (sunDenominator * tanHalfFov *
        (static_cast<float>(m_width) / static_cast<float>(m_height)));
    const float sunNdcY = lightDirection.Dot(cameraUpForSky) / (sunDenominator * tanHalfFov);
    temporalConstants.sunParameters[0] = sunForward > 0.0F ? 0.5F + 0.5F * sunNdcX : 2.0F;
    temporalConstants.sunParameters[1] = 0.5F - 0.5F * sunNdcY;
    temporalConstants.sunParameters[2] = sunIntensity;
    temporalConstants.sunParameters[3] = std::clamp(lightDirection.y * 2.5F + 0.35F, 0.0F, 1.0F);
    std::memcpy(m_mappedTemporalConstants[m_frameIndex], &temporalConstants, sizeof(temporalConstants));
    std::memcpy(m_mappedConstants[m_frameIndex], &constants, sizeof(constants));
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        auto* shadowConstants = static_cast<unsigned char*>(m_mappedConstants[m_frameIndex]) +
            kShadowConstantOffset + cascade * kShadowConstantStride;
        std::memcpy(shadowConstants, &constants.lightViewProjection[cascade], sizeof(Mat4));
        std::memcpy(shadowConstants + sizeof(Mat4), &elapsedSeconds, sizeof(elapsedSeconds));
    }

    if (FAILED(m_commandAllocators[m_frameIndex]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()))) {
        LogError("Direct3D 12: command recording reset failed.");
        m_failed = true;
        return;
    }

    const UINT timestampBase = m_frameIndex * 7U;
    if (m_timestampFrequency != 0)
        m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase);
    D3D12_RESOURCE_BARRIER shadowToDepth{};
    shadowToDepth.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    shadowToDepth.Transition.pResource = m_shadowTexture.Get();
    shadowToDepth.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    shadowToDepth.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowToDepth.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &shadowToDepth);
    // Near shadows retain 2048² detail. Mid/far cascades render at 1024² to
    // cut their raster workload while retaining enough resolution at their larger ranges.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_shadowPipelineState.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    const Vec3 cameraRight{std::cos(cameraYaw), 0.0F, -std::sin(cameraYaw)};
    const auto normalizedUp = forward.Cross(cameraRight).Normalized();
    const Vec3 cameraUp = normalizedUp.value_or(Vec3{0.0F, 1.0F, 0.0F});
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const float projectionScaleY = projection.At(1, 1);
    const float tanHalfVerticalFov = std::isfinite(projectionScaleY) &&
        std::abs(projectionScaleY) > 1.0e-6F
        ? 1.0F / std::abs(projectionScaleY)
        : std::tan(math::DegreesToRadians(34.0F));
    const auto submitGpuCull = [this](UINT stream, const GpuCullConstants& cull, bool shadowPass) {
        auto* streamConstants = static_cast<unsigned char*>(m_mappedGpuCullConstants[m_frameIndex]) +
            static_cast<std::size_t>(stream) * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        std::memcpy(streamConstants, &cull, sizeof(cull));

        auto& arguments = m_gpuCullArgumentBuffers[m_frameIndex][stream];
        auto& counter = m_gpuCullCounterBuffers[m_frameIndex][stream];
        D3D12_RESOURCE_BARRIER counterToCopyDest{};
        counterToCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        counterToCopyDest.Transition.pResource = counter.Get();
        counterToCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        counterToCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        counterToCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &counterToCopyDest);
        m_commandList->CopyBufferRegion(counter.Get(), 0, m_zeroCounterBuffer.Get(), 0, sizeof(UINT) * 3U);
        D3D12_RESOURCE_BARRIER counterToUav = counterToCopyDest;
        counterToUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        counterToUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_commandList->ResourceBarrier(1, &counterToUav);

        m_commandList->SetComputeRootSignature(m_gpuCullRootSignature.Get());
        m_commandList->SetPipelineState(m_gpuCullPipelineState.Get());
        m_commandList->SetComputeRootConstantBufferView(0,
            m_gpuCullConstantBuffers[m_frameIndex]->GetGPUVirtualAddress() +
            static_cast<UINT64>(stream) * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        m_commandList->SetComputeRootShaderResourceView(1, m_gpuCullChunkBuffer->GetGPUVirtualAddress());
        m_commandList->SetComputeRootUnorderedAccessView(2, arguments->GetGPUVirtualAddress());
        m_commandList->SetComputeRootUnorderedAccessView(3, counter->GetGPUVirtualAddress());
        ID3D12DescriptorHeap* cullHeaps[] = {m_srvHeap.Get()};
        m_commandList->SetDescriptorHeaps(1, cullHeaps);
        D3D12_GPU_DESCRIPTOR_HANDLE hiZHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        hiZHandle.ptr += static_cast<UINT64>(kHiZBaseSrvIndex + m_lastHiZFrameIndex) * m_srvDescriptorSize;
        m_commandList->SetComputeRootDescriptorTable(4, hiZHandle);
        m_commandList->Dispatch((cull.chunkCount + 63U) / 64U, 1U, 1U);

        D3D12_RESOURCE_BARRIER toIndirect[2]{};
        toIndirect[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toIndirect[0].Transition.pResource = arguments.Get();
        toIndirect[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toIndirect[0].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        toIndirect[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toIndirect[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toIndirect[1].Transition.pResource = counter.Get();
        toIndirect[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toIndirect[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        toIndirect[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(2, toIndirect);

        m_commandList->SetPipelineState(shadowPass ? m_shadowPipelineState.Get() : m_pipelineState.Get());
        m_commandList->ExecuteIndirect(m_drawCommandSignature.Get(), cull.chunkCount,
            arguments.Get(), 0, counter.Get(), 0);

        D3D12_RESOURCE_BARRIER afterDraw[2]{};
        afterDraw[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        afterDraw[0].Transition.pResource = arguments.Get();
        afterDraw[0].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        afterDraw[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        afterDraw[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        afterDraw[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        afterDraw[1].Transition.pResource = counter.Get();
        afterDraw[1].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        afterDraw[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        afterDraw[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(2, afterDraw);
        m_commandList->CopyBufferRegion(m_gpuCullStatsReadbackBuffers[m_frameIndex].Get(),
            static_cast<UINT64>(stream) * sizeof(UINT) * 3U, counter.Get(), 0, sizeof(UINT) * 3U);
    };
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        const UINT cascadeResolution = cascade == 0 ? kShadowResolution : kShadowResolution / 2U;
        const float cascadeResolutionF = static_cast<float>(cascadeResolution);
        const D3D12_VIEWPORT shadowViewport{0.0F, 0.0F, cascadeResolutionF, cascadeResolutionF, 0.0F, 1.0F};
        const D3D12_RECT shadowScissor{0, 0, static_cast<LONG>(cascadeResolution), static_cast<LONG>(cascadeResolution)};
        m_commandList->RSSetViewports(1, &shadowViewport);
        m_commandList->RSSetScissorRects(1, &shadowScissor);
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &m_shadowDsvHandles[cascade]);
        m_commandList->ClearDepthStencilView(m_shadowDsvHandles[cascade], D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
        m_commandList->SetGraphicsRootConstantBufferView(0,
            m_constantBuffers[m_frameIndex]->GetGPUVirtualAddress() + kShadowConstantOffset + cascade * kShadowConstantStride);
        const float cascadeRange = cascadeSplits[cascade];
        // Preserve fine leaf shadows nearby, then progressively remove sub-pixel foliage
        // detail as the cascade covers more distant geometry.
        // GPU-driven quality pressure also reduces mid/near shadow geometry detail.
        const UINT shadowLod = m_worldBudget.ShadowLodForCascade(cascade);
        const Mat4& lightViewProjection = constants.lightViewProjection[cascade];
        if (!hasSubmittedScene) {
        if (m_gpuCullingEnabled) {
            GpuCullConstants cull{};
            cull.cullMatrix = lightViewProjection;
            cull.cameraPosition[0] = cameraX;
            cull.cameraPosition[1] = cameraY;
            cull.cameraPosition[2] = cameraZ;
            cull.cameraForward[0] = forward.x;
            cull.cameraForward[1] = forward.y;
            cull.cameraForward[2] = forward.z;
            cull.cameraRight[0] = cameraRight.x;
            cull.cameraRight[1] = cameraRight.y;
            cull.cameraRight[2] = cameraRight.z;
            cull.cameraUp[0] = cameraUp.x;
            cull.cameraUp[1] = cameraUp.y;
            cull.cameraUp[2] = cameraUp.z;
            cull.parameters[0] = 1.0F;
            cull.parameters[1] = cascadeRange;
            cull.parameters[2] = static_cast<float>(shadowLod);
            cull.parameters[3] = aspect;
            cull.chunkCount = static_cast<UINT>(m_drawChunks.size());
            submitGpuCull(cascade, cull, true);
        } else {
            UINT shadowCommandCount = 0;
            std::uint64_t cascadeTriangles = 0;
            D3D12_DRAW_INDEXED_ARGUMENTS* shadowArguments =
                m_mappedIndirectArguments[m_frameIndex][cascade];
            for (const DrawChunk& chunk : m_drawChunks) {
                const Vec3 toChunk{chunk.centerX - cameraX, chunk.centerY - cameraY, chunk.centerZ - cameraZ};
                if (toChunk.Length() > cascadeRange + chunk.radius * 1.5F) continue;
                if (chunk.indexCount[shadowLod] == 0) continue;
                const math::Vec4 clip = lightViewProjection.Transform({chunk.centerX, chunk.centerY, chunk.centerZ, 1.0F});
                const float rx = chunk.radius * std::sqrt(lightViewProjection.At(0,0) * lightViewProjection.At(0,0) + lightViewProjection.At(0,1) * lightViewProjection.At(0,1) + lightViewProjection.At(0,2) * lightViewProjection.At(0,2));
                const float ry = chunk.radius * std::sqrt(lightViewProjection.At(1,0) * lightViewProjection.At(1,0) + lightViewProjection.At(1,1) * lightViewProjection.At(1,1) + lightViewProjection.At(1,2) * lightViewProjection.At(1,2));
                const float rz = chunk.radius * std::sqrt(lightViewProjection.At(2,0) * lightViewProjection.At(2,0) + lightViewProjection.At(2,1) * lightViewProjection.At(2,1) + lightViewProjection.At(2,2) * lightViewProjection.At(2,2));
                if (clip.x + rx < -1.0F || clip.x - rx > 1.0F ||
                    clip.y + ry < -1.0F || clip.y - ry > 1.0F ||
                    clip.z + rz < 0.0F || clip.z - rz > 1.0F) continue;

                shadowArguments[shadowCommandCount++] = {
                    chunk.indexCount[shadowLod], 1U, chunk.startIndex[shadowLod], 0, 0U
                };
                cascadeTriangles += chunk.indexCount[shadowLod] / 3U;
            }
            if (shadowCommandCount != 0U) {
                m_commandList->ExecuteIndirect(m_drawCommandSignature.Get(), shadowCommandCount,
                    m_indirectArgumentBuffers[m_frameIndex][cascade].Get(), 0, nullptr, 0);
            }
            m_shadowDrawCallsAccumulated[cascade] += shadowCommandCount;
            m_shadowTrianglesAccumulated[cascade] += cascadeTriangles;
        }
        }
        if (hasSubmittedScene && m_submittedVertexBuffers[m_frameIndex].Get() != nullptr &&
            m_submittedIndexBuffers[m_frameIndex].Get() != nullptr) {
            m_commandList->SetPipelineState(m_shadowPipelineState.Get());
            m_commandList->IASetVertexBuffers(0, 1, &submittedVertexView);
            m_commandList->IASetIndexBuffer(&submittedIndexView);
            for (const renderer::SubmittedDrawRange& draw : submittedGeometry.draws) {
                m_commandList->DrawIndexedInstanced(draw.indexCount, 1U, draw.firstIndex,
                    static_cast<INT>(draw.firstVertex), 0U);
            }
        }
        if (m_timestampFrequency != 0)
            m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase + cascade + 1U);
    }
    D3D12_RESOURCE_BARRIER shadowToSample = shadowToDepth;
    shadowToSample.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowToSample.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_commandList->ResourceBarrier(1, &shadowToSample);

    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = m_hdrTexture.Get();
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    hdrRtv.ptr += static_cast<SIZE_T>(kFrameCount) * m_rtvDescriptorSize;
    constexpr float skyColor[] = {0.20F, 0.29F, 0.39F, 1.0F};
    m_commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);
    m_commandList->ClearRenderTargetView(hdrRtv, skyColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(m_width), static_cast<float>(m_height), 0.0F, 1.0F};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    // The shadow pass leaves its depth-only PSO active; restore the scene PSO before drawing color.
    m_commandList->SetPipelineState(m_pipelineState.Get());
    ID3D12DescriptorHeap* sceneHeaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, sceneHeaps);
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffers[m_frameIndex]->GetGPUVirtualAddress());
    D3D12_VIEWPORT sceneViewport{0.0F, 0.0F, static_cast<float>(m_width), static_cast<float>(m_height), 0.0F, 1.0F};
    D3D12_RECT sceneScissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &sceneViewport);
    m_commandList->RSSetScissorRects(1, &sceneScissor);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    if (!hasSubmittedScene) {
    if (m_gpuCullingEnabled) {
        GpuCullConstants cull{};
        cull.cullMatrix = m_previousViewProjection;
        cull.cameraPosition[0] = cameraX;
        cull.cameraPosition[1] = cameraY;
        cull.cameraPosition[2] = cameraZ;
        cull.cameraForward[0] = forward.x;
        cull.cameraForward[1] = forward.y;
        cull.cameraForward[2] = forward.z;
        cull.cameraRight[0] = cameraRight.x;
        cull.cameraRight[1] = cameraRight.y;
        cull.cameraRight[2] = cameraRight.z;
        cull.cameraUp[0] = cameraUp.x;
        cull.cameraUp[1] = cameraUp.y;
        cull.cameraUp[2] = cameraUp.z;
        cull.parameters[0] = 0.0F;
        cull.parameters[1] = adaptiveDetailScale;
        cull.parameters[2] = tanHalfVerticalFov;
        cull.parameters[3] = aspect;
        cull.chunkCount = static_cast<UINT>(m_drawChunks.size());
        cull.useHiZOcclusion = hiZStableCamera ? 1U : 0U;
        submitGpuCull(3U, cull, false);
    } else {
        UINT sceneCommandCount = 0;
        D3D12_DRAW_INDEXED_ARGUMENTS* sceneArguments =
            m_mappedIndirectArguments[m_frameIndex][3];
        for (const DrawChunk& chunk : m_drawChunks) {
            const Vec3 toChunk{chunk.centerX - cameraX, chunk.centerY - cameraY, chunk.centerZ - cameraZ};
            const float distance = toChunk.Length();
            if (distance > 270.0F + chunk.radius) continue;
            const float forwardDistance = toChunk.Dot(forward);
            if (forwardDistance < -chunk.radius) continue;
            const float horizontalLimit = std::max(forwardDistance, 0.0F) * tanHalfVerticalFov * aspect + chunk.radius;
            if (std::abs(toChunk.Dot(cameraRight)) > horizontalLimit) continue;
            const float verticalLimit = std::max(forwardDistance, 0.0F) * tanHalfVerticalFov + chunk.radius;
            if (std::abs(toChunk.Dot(cameraUp)) > verticalLimit) continue;
            const UINT lod = distance < 65.0F * adaptiveDetailScale ? 0U :
                (distance < 140.0F * adaptiveDetailScale ? 1U : 2U);
            if (chunk.indexCount[lod] != 0U) {
                sceneArguments[sceneCommandCount++] = {
                    chunk.indexCount[lod], 1U, chunk.startIndex[lod], 0, 0U
                };
            }
        }
        if (sceneCommandCount != 0U) {
            m_commandList->ExecuteIndirect(m_drawCommandSignature.Get(), sceneCommandCount,
                m_indirectArgumentBuffers[m_frameIndex][3].Get(), 0, nullptr, 0);
        }
        m_sceneDrawCallsAccumulated += sceneCommandCount;
    }

    }
    if (hasSubmittedScene && m_submittedVertexBuffers[m_frameIndex].Get() != nullptr &&
        m_submittedIndexBuffers[m_frameIndex].Get() != nullptr) {
        m_commandList->SetPipelineState(m_pipelineState.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0,
            m_constantBuffers[m_frameIndex]->GetGPUVirtualAddress());
        m_commandList->IASetVertexBuffers(0, 1, &submittedVertexView);
        m_commandList->IASetIndexBuffer(&submittedIndexView);
        for (const renderer::SubmittedDrawRange& draw : submittedGeometry.draws) {
            m_commandList->DrawIndexedInstanced(draw.indexCount, 1U, draw.firstIndex,
                static_cast<INT>(draw.firstVertex), 0U);
        }
        m_sceneDrawCallsAccumulated += submittedGeometry.draws.size();
    }
    if (m_timestampFrequency != 0)
        m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase + 4U);
    D3D12_RESOURCE_BARRIER depthToSample{};
    depthToSample.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    depthToSample.Transition.pResource = m_depthTexture.Get();
    depthToSample.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthToSample.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    depthToSample.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &depthToSample);
    BuildHiZPyramid(m_frameIndex);

    D3D12_RESOURCE_BARRIER hdrToSample{};
    hdrToSample.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    hdrToSample.Transition.pResource = m_hdrTexture.Get();
    hdrToSample.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    hdrToSample.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    hdrToSample.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &hdrToSample);

    D3D12_RESOURCE_BARRIER historyColorToTarget{};
    historyColorToTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    historyColorToTarget.Transition.pResource = m_historyColorTextures[m_historyWriteIndex].Get();
    historyColorToTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    historyColorToTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    historyColorToTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    D3D12_RESOURCE_BARRIER historyDepthToTarget = historyColorToTarget;
    historyDepthToTarget.Transition.pResource = m_historyDepthTextures[m_historyWriteIndex].Get();
    m_commandList->ResourceBarrier(1, &historyColorToTarget);
    m_commandList->ResourceBarrier(1, &historyDepthToTarget);
    D3D12_CPU_DESCRIPTOR_HANDLE temporalRtvs[] = {
        m_historyColorRtvs[m_historyWriteIndex], m_historyDepthRtvs[m_historyWriteIndex]
    };
    m_commandList->OMSetRenderTargets(2, temporalRtvs, FALSE, nullptr);
    m_commandList->SetPipelineState(m_temporalPipelineStates[1U - m_historyWriteIndex].Get());
    m_commandList->SetGraphicsRootConstantBufferView(2, m_temporalConstantBuffers[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_VIEWPORT temporalViewport{0.0F, 0.0F, static_cast<float>(m_width), static_cast<float>(m_height), 0.0F, 1.0F};
    D3D12_RECT temporalScissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &temporalViewport);
    m_commandList->RSSetScissorRects(1, &temporalScissor);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
    if (m_timestampFrequency != 0)
        m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase + 5U);
    D3D12_RESOURCE_BARRIER historyColorToSample = historyColorToTarget;
    historyColorToSample.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    historyColorToSample.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    historyColorToSample.Transition.pResource = m_historyColorTextures[m_historyWriteIndex].Get();
    D3D12_RESOURCE_BARRIER historyDepthToSample = historyColorToSample;
    historyDepthToSample.Transition.pResource = m_historyDepthTextures[m_historyWriteIndex].Get();
    m_commandList->ResourceBarrier(1, &historyColorToSample);
    m_commandList->ResourceBarrier(1, &historyDepthToSample);

    D3D12_RESOURCE_BARRIER backToTarget{};
    backToTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    backToTarget.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    backToTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    backToTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    backToTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &backToTarget);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->SetPipelineState(m_postProcessPipelineState.Get());
    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootConstantBufferView(2, m_temporalConstantBuffers[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
    if (m_timestampFrequency != 0) {
        m_commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestampBase + 6U);
        m_commandList->ResolveQueryData(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            timestampBase, 7U, m_timestampReadback.Get(), static_cast<UINT64>(timestampBase) * sizeof(UINT64));
    }
    D3D12_RESOURCE_BARRIER depthToWrite = depthToSample;
    depthToWrite.Transition.StateBefore =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    depthToWrite.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    m_commandList->ResourceBarrier(1, &depthToWrite);
    D3D12_RESOURCE_BARRIER toPresent = backToTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &toPresent);
    if (FAILED(m_commandList->Close())) {
        LogError("Direct3D 12: command-list close failed.");
        m_failed = true;
        return;
    }
    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);
    if (m_gpuCullingEnabled) {
        m_gpuCullStatsPending[m_frameIndex] = true;
        m_lastHiZFrameIndex = m_frameIndex;
        m_hasPreviousHiZ = true;
    }
    if (m_timestampFrequency != 0) m_profilePending[m_frameIndex] = true;
    // Keep presentation uncapped so missed v-sync intervals do not halve apparent frame rate.
    const HRESULT presentResult = m_swapChain->Present(0, 0);
    const UINT64 signalValue = m_nextFenceValue++;
    const HRESULT signalResult = m_commandQueue->Signal(m_fence.Get(), signalValue);
    if (FAILED(signalResult)) {
        m_deviceLost = m_device.Get() != nullptr && FAILED(m_device->GetDeviceRemovedReason());
        LogError("Direct3D 12: frame fence signal failed.");
        m_failed = true;
        return;
    }
    m_fenceValues[m_frameIndex] = signalValue;
    if (FAILED(presentResult)) {
        const HRESULT deviceReason = m_device->GetDeviceRemovedReason();
        char message[240]{};
        std::snprintf(message, sizeof(message),
            "Direct3D 12: Present failed (HRESULT 0x%08lX, device reason 0x%08lX).",
            static_cast<unsigned long>(presentResult), static_cast<unsigned long>(deviceReason));
        LogError(message);
        m_deviceLost = presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET ||
            FAILED(deviceReason);
        m_failed = true;
        return;
    }
    m_historyValid = true;
    m_historyWriteIndex = 1U - m_historyWriteIndex;
    m_previousViewProjection = constants.viewProjection;
    m_previousCameraX = cameraX;
    m_previousCameraY = cameraY;
    m_previousCameraZ = cameraZ;
    m_previousCameraYaw = cameraYaw;
    m_previousCameraPitch = cameraPitch;
    ++m_renderFrameNumber;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_lastFrameSucceeded = true;
}
} // namespace vanta
