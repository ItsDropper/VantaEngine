#include "vanta/renderer/D3D12Renderer.hpp"

#include "vanta/core/Log.hpp"
#include "vanta/renderer/ProceduralWorld.hpp"
#include "vanta/renderer/GltfLoader.hpp"
#include "vanta/renderer/ObjLoader.hpp"
#include "vanta/renderer/WicTextureLoader.hpp"

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
static_assert(sizeof(D3D12WorldVertex) == 60);

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
};
static_assert(sizeof(TemporalConstants) == 144);

constexpr UINT kShadowResolution = 2048;
constexpr UINT64 kShadowConstantOffset = 512;
constexpr UINT64 kShadowConstantStride = 256;

Mat4 OrthographicLH(float width, float height, float nearZ, float farZ)
{
    Mat4 result = Mat4::Identity();
    result.At(0, 0) = 2.0F / width;
    result.At(1, 1) = 2.0F / height;
    result.At(2, 2) = 1.0F / (farZ - nearZ);
    result.At(2, 3) = -nearZ / (farZ - nearZ);
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

constexpr char kSceneShader[] = R"(
cbuffer SceneConstants : register(b0)
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lighting;
    float4x4 lightViewProjection[3];
    float4 cascadeSplits;
};
Texture2DArray<float> shadowMap : register(t2);
Texture2D modelAlbedoTexture : register(t7);
Texture2D modelNormalTexture : register(t8);
Texture2D modelRoughnessTexture : register(t9);
Texture2D modelMetallicTexture : register(t10);
Texture2D modelOcclusionTexture : register(t11);
SamplerState linearClamp : register(s0);
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; float2 uv : TEXCOORD0; float4 material : TEXCOORD1; };
struct PSInput { float4 position : SV_POSITION; float3 worldPosition : TEXCOORD0; float3 normal : TEXCOORD1; float4 color : COLOR; float2 uv : TEXCOORD2; float4 material : TEXCOORD3; };
PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(viewProjection, float4(input.position, 1.0));
    o.worldPosition = input.position;
    o.normal = normalize(input.normal);
    o.color = input.color;
    o.uv = input.uv;
    o.material = input.material;
    return o;
}
float DistributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nh = saturate(dot(n, h));
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 0.0001);
}
float GeometrySchlick(float nv, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    return nv / max(nv * (1.0 - k) + k, 0.0001);
}
float3 FresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}
float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float TerrainNoise(float2 p)
{
    float n = ValueNoise(p) * 0.57;
    n += ValueNoise(p * 2.03 + 17.0) * 0.28;
    n += ValueNoise(p * 5.11 - 9.0) * 0.15;
    return n;
}
float CalculateShadow(float3 worldPosition, float3 normal, float3 lightDirection)
{
    float cameraDistance = distance(cameraPosition.xyz, worldPosition);
    int cascade = cameraDistance < cascadeSplits.x ? 0 :
                  (cameraDistance < cascadeSplits.y ? 1 : 2);
    float4 shadowClip = mul(lightViewProjection[cascade], float4(worldPosition, 1.0));
    if (shadowClip.w <= 0.0) return 1.0;
    float3 projected = shadowClip.xyz / shadowClip.w;
    float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(uv <= 0.001) || any(uv >= 0.999) || projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;
    float bias = max(0.00018 * (1.0 - saturate(dot(normal, lightDirection))), 0.000045);
    float visibility = 0.0;
    const float2 texel = 1.0 / 2048.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float storedDepth = shadowMap.SampleLevel(linearClamp,
                float3(uv + float2(x, y) * texel, cascade), 0);
            visibility += projected.z - bias <= storedDepth ? 1.0 : 0.0;
        }
    }
    return lerp(0.58, 1.0, visibility / 9.0);
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    uint materialFlags = (uint)(input.material.w + 0.5);
    if ((materialFlags & 2U) != 0U)
    {
        float3 tangentNormal = modelNormalTexture.Sample(linearClamp, input.uv).xyz * 2.0 - 1.0;
        float3 dp1 = ddx(input.worldPosition);
        float3 dp2 = ddy(input.worldPosition);
        float2 duv1 = ddx(input.uv);
        float2 duv2 = ddy(input.uv);
        float3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
        float3 bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
        n = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + n * tangentNormal.z);
    }
    float3 v = normalize(cameraPosition.xyz - input.worldPosition);
    float3 l = normalize(float3(-0.42, 0.82, -0.36));
    float3 h = normalize(v + l);
    float3 albedo = max(input.color.rgb, 0.0);
    if ((materialFlags & 1U) != 0U)
        albedo *= modelAlbedoTexture.Sample(linearClamp, input.uv).rgb;
    // World-space multi-frequency noise breaks up large flat color regions without
    // relying on tiling textures. A slope/height mask gives the terrain distinct soil,
    // grass and exposed-rock response while leaving trees and buildings mostly intact.
    float2 worldXZ = input.worldPosition.xz;
    float macroNoise = TerrainNoise(worldXZ * 0.115);
    float detailNoise = TerrainNoise(worldXZ * 1.65);
    float fine = detailNoise * 2.0 - 1.0;
    float terrainColorMask = smoothstep(0.055, 0.20, albedo.g - max(albedo.r, albedo.b) * 0.42)
        * (1.0 - smoothstep(0.15, 1.65, input.worldPosition.y));
    float slope = 1.0 - saturate(n.y);
    float rockMask = terrainColorMask * smoothstep(0.28, 0.78, slope + (macroNoise - 0.5) * 0.22);
    float soilMask = terrainColorMask * (1.0 - rockMask) * smoothstep(0.30, 0.67, macroNoise);
    float3 grassAlbedo = albedo * (0.80 + macroNoise * 0.34) * (0.90 + detailNoise * 0.20);
    float3 soilAlbedo = albedo * float3(0.72, 0.59, 0.43) * (0.72 + detailNoise * 0.48);
    float3 rockAlbedo = lerp(albedo * 0.52, float3(0.31, 0.32, 0.30), detailNoise * 0.55);
    albedo = lerp(grassAlbedo, soilAlbedo, soilMask * 0.72);
    albedo = lerp(albedo, rockAlbedo, rockMask * 0.88);
    // Fine world-space breakup adds surface detail at multiple viewing distances.
    albedo *= 0.94 + 0.10 * fine + 0.07 * (macroNoise - 0.5);
    float importedMaterial = saturate(input.material.z);
    float roughness = saturate(lerp(0.72 + 0.16 * fine + terrainColorMask * 0.12,
        input.material.x + 0.05 * fine, importedMaterial));
    float metallic = saturate(lerp(0.025, input.material.y, importedMaterial));
    if ((materialFlags & 4U) != 0U)
    {
        float roughnessSample = modelRoughnessTexture.Sample(linearClamp, input.uv).r;
        if ((materialFlags & 32U) != 0U)
            roughnessSample = modelRoughnessTexture.Sample(linearClamp, input.uv).g;
        roughness = saturate(roughness * roughnessSample);
    }
    if ((materialFlags & 8U) != 0U)
        metallic = saturate(metallic * modelMetallicTexture.Sample(linearClamp, input.uv).r);
    if ((materialFlags & 32U) != 0U)
        metallic = saturate(metallic * modelRoughnessTexture.Sample(linearClamp, input.uv).b);
    float ambientOcclusion = (materialFlags & 16U) != 0U
        ? modelOcclusionTexture.Sample(linearClamp, input.uv).r : 1.0;
    float ndl = saturate(dot(n, l));
    float ndv = max(saturate(dot(n, v)), 0.001);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 f = FresnelSchlick(saturate(dot(h, v)), f0);
    float d = DistributionGGX(n, h, roughness);
    float g = GeometrySchlick(ndv, roughness) * GeometrySchlick(max(ndl, 0.001), roughness);
    float3 specular = d * g * f / max(4.0 * ndv * max(ndl, 0.001), 0.001);
    float3 kD = (1.0 - f) * (1.0 - metallic);
    float shadow = CalculateShadow(input.worldPosition, n, l);
    float3 direct = (kD * albedo / 3.14159265 + specular) * float3(4.2, 3.65, 2.9) * ndl * shadow;
    float3 sky = lerp(float3(0.055, 0.052, 0.048), float3(0.20, 0.31, 0.46), saturate(n.y * 0.5 + 0.5));
    float3 ambient = sky * albedo * (1.0 - metallic) * 1.35 * ambientOcclusion;
    float3 reflectedSky = lerp(float3(0.10, 0.13, 0.18), float3(0.56, 0.64, 0.72), saturate(reflect(-v, n).y * 0.5 + 0.5));
    ambient += reflectedSky * f * (1.0 - roughness * 0.7) * 0.45;
    float distanceToCamera = distance(cameraPosition.xyz, input.worldPosition);
    float fog = saturate(1.0 - exp(-max(distanceToCamera - 18.0, 0.0) * 0.018));
    float3 fogColor = float3(0.31, 0.40, 0.49);
    // Output linear HDR. The post-process pass performs bloom and tone mapping once.
    return float4(lerp(direct + ambient, fogColor * 1.4, fog), 1.0);
}
)";

float TerrainHeight(float x, float z)
{
    return -1.0F + 0.68F * std::sin(x * 0.075F) * std::cos(z * 0.061F)
        + 0.28F * std::sin((x + z) * 0.17F)
        + 0.15F * std::cos(z * 0.29F) * std::sin(x * 0.23F);
}

void AppendTerrain(std::vector<renderer::WorldVertex>& vertices, std::vector<unsigned int>& indices)
{
    constexpr int side = 129;
    constexpr float extent = 65.0F;
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
            const float dx = TerrainHeight(wx + 0.15F, wz) - TerrainHeight(wx - 0.15F, wz);
            const float dz = TerrainHeight(wx, wz + 0.15F) - TerrainHeight(wx, wz - 0.15F);
            const float invLength = 1.0F / std::sqrt(dx * dx + 0.09F + dz * dz);
            const float slope = std::sqrt(dx * dx + dz * dz);
            const float grass = std::clamp(0.72F - slope * 1.7F + (y + 1.4F) * 0.09F, 0.24F, 0.78F);
            const float variation = 0.92F + 0.08F * std::sin(wx * 0.8F) * std::sin(wz * 0.61F);
            vertices.push_back({{wx, y, wz}, {-dx * invLength, 0.3F * invLength, -dz * invLength},
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
} // namespace

D3D12Renderer::~D3D12Renderer()
{
    (void)WaitForGpu();
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (m_mappedConstants[i] != nullptr && m_constantBuffers[i]) {
            m_constantBuffers[i]->Unmap(0, nullptr);
            m_mappedConstants[i] = nullptr;
        }
        if (m_mappedTemporalConstants[i] != nullptr && m_temporalConstantBuffers[i]) {
            m_temporalConstantBuffers[i]->Unmap(0, nullptr);
            m_mappedTemporalConstants[i] = nullptr;
        }
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
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
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
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        LogError("Direct3D 12: fence event creation failed.");
        return false;
    }

    if (!CreateRenderTargets() || !CreateDepthTarget() || !CreateShadowResources() || !CreateHdrTarget() ||
        !CreateTemporalTargets() || !CreateSceneGeometry() || !CreatePipeline() ||
        !CreateShadowPipeline() || !CreateTemporalPipeline() || !CreatePostProcessPipeline()) return false;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    LogInfo("Direct3D 12 scene renderer initialized: HDR PBR shading, three 2048px cascaded shadow maps with 3x3 PCF, ACES tone mapping, highlight bloom, depth-aware contact occlusion, procedural sky and atmospheric fog.");
    LogInfo("Camera controls: W/A/S/D move, Q/E descend/ascend, arrow keys look, Escape exits.");
    return true;
}

bool D3D12Renderer::CreateRenderTargets()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < kFrameCount; ++index) {
        if (FAILED(m_swapChain->GetBuffer(index, IID_PPV_ARGS(m_renderTargets[index].GetAddressOf())))) {
            LogError("Direct3D 12: could not acquire a swap-chain back buffer.");
            return false;
        }
        m_device->CreateRenderTargetView(m_renderTargets[index].Get(), nullptr, handle);
        handle.ptr += m_rtvDescriptorSize;
    }
    return true;
}

bool D3D12Renderer::CreateDepthTarget()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0F;
    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(m_depthTexture.GetAddressOf())))) {
        LogError("Direct3D 12: depth texture creation failed.");
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_depthTexture.Get(), &dsv, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12Renderer::CreateShadowResources()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kShadowResolution;
    desc.Height = kShadowResolution;
    desc.DepthOrArraySize = 3;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0F;
    clear.DepthStencil.Stencil = 0;
    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(m_shadowTexture.GetAddressOf())))) {
        LogError("Direct3D 12: cascaded shadow texture creation failed.");
        return false;
    }
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.MipSlice = 0;
        dsv.Texture2DArray.FirstArraySlice = cascade;
        dsv.Texture2DArray.ArraySize = 1;
        m_shadowDsvHandles[cascade] = heapStart;
        m_shadowDsvHandles[cascade].ptr += static_cast<SIZE_T>(cascade + 1) * m_dsvDescriptorSize;
        m_device->CreateDepthStencilView(m_shadowTexture.Get(), &dsv, m_shadowDsvHandles[cascade]);
    }
    return true;
}

bool D3D12Renderer::CreateHdrTarget()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_width;
    desc.Height = m_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear.Color[0] = 0.18F; clear.Color[1] = 0.28F; clear.Color[2] = 0.40F; clear.Color[3] = 1.0F;
    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(m_hdrTexture.GetAddressOf())))) {
        LogError("Direct3D 12: HDR scene target creation failed.");
        return false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kFrameCount) * m_rtvDescriptorSize;
    m_device->CreateRenderTargetView(m_hdrTexture.Get(), nullptr, rtv);

    D3D12_DESCRIPTOR_HEAP_DESC srvDescription{};
    srvDescription.NumDescriptors = 12;
    srvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&srvDescription, IID_PPV_ARGS(m_srvHeap.GetAddressOf())))) {
        LogError("Direct3D 12: post-process SRV heap creation failed.");
        return false;
    }
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrv{};
    hdrSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    hdrSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    hdrSrv.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_hdrTexture.Get(), &hdrSrv, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE depthHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    depthHandle.ptr += m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_depthTexture.Get(), &depthSrv, depthHandle);
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv{};
    shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrv.Texture2DArray.MipLevels = 1;
    shadowSrv.Texture2DArray.ArraySize = 3;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    shadowHandle.ptr += static_cast<SIZE_T>(2) * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_shadowTexture.Get(), &shadowSrv, shadowHandle);
    return true;
}

bool D3D12Renderer::CreateTemporalTargets()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = m_width;
    colorDesc.Height = m_height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorClear.Color[0] = colorClear.Color[1] = colorClear.Color[2] = 0.0F;
    colorClear.Color[3] = 1.0F;
    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_R32_FLOAT;
    depthClear.Color[0] = 1.0F;
    for (UINT i = 0; i < 2; ++i) {
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &colorDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear,
                IID_PPV_ARGS(m_historyColorTextures[i].GetAddressOf()))) ||
            FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &depthClear,
                IID_PPV_ARGS(m_historyDepthTextures[i].GetAddressOf()))) {
            LogError("Direct3D 12: temporal history texture creation failed.");
            return false;
        }
        m_historyColorRtvs[i] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_historyColorRtvs[i].ptr += static_cast<SIZE_T>(kFrameCount + 1 + i) * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(m_historyColorTextures[i].Get(), nullptr, m_historyColorRtvs[i]);
        m_historyDepthRtvs[i] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_historyDepthRtvs[i].ptr += static_cast<SIZE_T>(kFrameCount + 3 + i) * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(m_historyDepthTextures[i].Get(), nullptr, m_historyDepthRtvs[i]);

        D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv{};
        colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        colorSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        colorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        colorSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE colorHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        colorHandle.ptr += static_cast<SIZE_T>(3 + i) * m_srvDescriptorSize;
        m_device->CreateShaderResourceView(m_historyColorTextures[i].Get(), &colorSrv, colorHandle);
        D3D12_SHADER_RESOURCE_VIEW_DESC historyDepthSrv{};
        historyDepthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        historyDepthSrv.Format = DXGI_FORMAT_R32_FLOAT;
        historyDepthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        historyDepthSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE historyDepthHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        historyDepthHandle.ptr += static_cast<SIZE_T>(5 + i) * m_srvDescriptorSize;
        m_device->CreateShaderResourceView(m_historyDepthTextures[i].Get(), &historyDepthSrv, historyDepthHandle);
    }
    m_historyWriteIndex = 0;
    m_historyValid = false;
    m_previousViewProjection = Mat4::Identity();
    return true;
}

bool D3D12Renderer::CreateSceneGeometry()
{
    std::vector<renderer::WorldVertex> vertices;
    std::vector<unsigned int> indices;
    AppendTerrain(vertices, indices);
    renderer::BuildDetailedWorld(vertices, indices);
    std::vector<std::array<float, 4>> materialParameters(vertices.size(), {0.78F, 0.0F, 0.0F, 0.0F});

    const std::filesystem::path modelDirectory = std::filesystem::path{VANTA_ASSET_DIR} / "models";
    const std::array<std::filesystem::path, 3> modelCandidates = {
        modelDirectory / "stone_house.glb", modelDirectory / "stone_house.gltf", modelDirectory / "stone_house.obj"
    };
    renderer::CpuModel model;
    std::string importError;
    bool modelLoaded = false;
    for (const auto& path : modelCandidates) {
        if (!std::filesystem::exists(path)) continue;
        const std::string extension = path.extension().string();
        const bool loaded = (extension == ".glb" || extension == ".gltf")
            ? renderer::LoadGltf(path, model, importError)
            : renderer::LoadWavefrontObj(path, model, importError);
        if (loaded) {
            modelLoaded = true;
            break;
        }
        LogError(importError.c_str());
    }

    renderer::CpuTexture diffuseTexture;
    bool hasDiffuseTexture = false;
    if (modelLoaded) {
        if (model.materials.empty()) {
            renderer::CpuMaterial material;
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
        const auto texturePath = model.materials.front().diffuseTexture;
        if (!texturePath.empty() && std::filesystem::exists(texturePath)) {
            hasDiffuseTexture = renderer::LoadWicTexture(texturePath, diffuseTexture);
            if (!hasDiffuseTexture) LogError("Direct3D 12: could not decode imported model base-color texture; using material factors.");
        }
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
            for (const renderer::CpuSubmesh& submesh : model.submeshes) {
                const renderer::CpuMaterial& material = model.materials[submesh.materialIndex];
                const bool useDiffuse = hasDiffuseTexture && material.diffuseTexture == texturePath;
                for (UINT offset = 0; offset < submesh.indexCount; ++offset) {
                    const unsigned int sourceIndex = model.indices[submesh.firstIndex + offset];
                    if (sourceIndex >= model.vertices.size()) { validModel = false; break; }
                    const renderer::ModelVertex& source = model.vertices[sourceIndex];
                    const unsigned int destinationIndex = static_cast<unsigned int>(vertices.size());
                    vertices.push_back({
                        {source.position.x * modelScale + modelX, source.position.y * modelScale + modelY,
                         source.position.z * modelScale + modelZ},
                        {source.normal.x, source.normal.y, source.normal.z},
                        {material.baseColor.x, material.baseColor.y, material.baseColor.z, 1.0F},
                        {source.u, source.v}
                    });
                    materialParameters.push_back({std::clamp(material.roughness, 0.04F, 1.0F),
                        std::clamp(material.metallic, 0.0F, 1.0F), useDiffuse ? 1.0F : 0.0F, 0.0F});
                    indices.push_back(destinationIndex);
                }
                if (!validModel) break;
            }
            if (!validModel) LogError("Direct3D 12: imported model contained an invalid index; omitting it.");
        } else {
            LogError("Direct3D 12: imported model has invalid geometry or material ranges; omitting it.");
        }
    }

    if (diffuseTexture.width == 0 || diffuseTexture.height == 0 ||
        diffuseTexture.rgba.size() < static_cast<std::size_t>(diffuseTexture.width) * diffuseTexture.height * 4U) {
        diffuseTexture.width = 1;
        diffuseTexture.height = 1;
        diffuseTexture.rgba = {255, 255, 255, 255};
        hasDiffuseTexture = false;
    }
    if (!CreateModelAlbedoTexture(diffuseTexture.width, diffuseTexture.height, diffuseTexture.rgba.data())) return false;

    if (vertices.empty() || indices.empty() || indices.size() > UINT_MAX ||
        vertices.size() != materialParameters.size() || (indices.size() % 3U) != 0U) {
        LogError("Direct3D 12: procedural/imported scene mesh is empty or too large.");
        return false;
    }

    // Spatially sort triangles into 24 m cells. Each cell stores three index lists:
    // full detail, medium detail, and a far LOD that drops sub-pixel vegetation triangles.
    // Vertex data stays shared, so LOD changes do not duplicate GPU vertex storage.
    constexpr float chunkSize = 24.0F;
    float minX = vertices.front().position[0], maxX = minX;
    float minY = vertices.front().position[1], maxY = minY;
    float minZ = vertices.front().position[2], maxZ = minZ;
    for (const auto& vertex : vertices) {
        minX = std::min(minX, vertex.position[0]); maxX = std::max(maxX, vertex.position[0]);
        minY = std::min(minY, vertex.position[1]); maxY = std::max(maxY, vertex.position[1]);
        minZ = std::min(minZ, vertex.position[2]); maxZ = std::max(maxZ, vertex.position[2]);
    }
    const int chunksX = std::max(1, static_cast<int>(std::ceil((maxX - minX) / chunkSize)));
    const int chunksZ = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / chunkSize)));
    struct CpuChunk {
        std::array<std::vector<unsigned int>, 3> lodIndices;
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
        const bool foliageTriangle = green > red * 1.12F && green > blue * 1.10F;
        chunk.lodIndices[0].insert(chunk.lodIndices[0].end(), {ia, ib, ic});
        if (!foliageTriangle || triangleArea >= 0.035F)
            chunk.lodIndices[1].insert(chunk.lodIndices[1].end(), {ia, ib, ic});
        if (!foliageTriangle || triangleArea >= 0.14F)
            chunk.lodIndices[2].insert(chunk.lodIndices[2].end(), {ia, ib, ic});
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
            for (UINT lod = 0; lod < 3; ++lod) {
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
        const renderer::WorldVertex& source = vertices[i];
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
    if (vertexBytes > UINT_MAX || indexBytes > UINT_MAX ||
        !CreateUploadBuffer(m_device.Get(), vertexBytes, m_vertexBuffer) ||
        !CreateUploadBuffer(m_device.Get(), indexBytes, m_indexBuffer)) {
        LogError("Direct3D 12: scene geometry buffer allocation failed.");
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(m_vertexBuffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, gpuVertices.data(), static_cast<size_t>(vertexBytes));
    m_vertexBuffer->Unmap(0, nullptr);
    if (FAILED(m_indexBuffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, indices.data(), static_cast<size_t>(indexBytes));
    m_indexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
    m_vertexBufferView.StrideInBytes = sizeof(D3D12WorldVertex);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = static_cast<UINT>(indexBytes);
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexCount = static_cast<UINT>(indices.size());
    LogInfo("Direct3D 12: loaded procedural environment and imported base-color PBR model geometry.");
    return true;
}

bool D3D12Renderer::CreateModelAlbedoTexture(UINT width, UINT height, const std::uint8_t* rgba)
{
    if (width == 0 || height == 0 || rgba == nullptr) return false;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_modelAlbedoTexture.GetAddressOf())))) {
        LogError("Direct3D 12: imported model texture allocation failed.");
        return false;
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 uploadBytes = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowSize, &uploadBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (rows != height || rowSize < static_cast<UINT64>(width) * 4U ||
        !CreateUploadBuffer(m_device.Get(), uploadBytes, upload)) {
        LogError("Direct3D 12: imported model texture staging allocation failed.");
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped))) return false;
    const auto* sourceBytes = rgba;
    auto* destinationBytes = static_cast<std::uint8_t*>(mapped) + footprint.Offset;
    for (UINT row = 0; row < height; ++row)
        std::memcpy(destinationBytes + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
            sourceBytes + static_cast<std::size_t>(row) * width * 4U, static_cast<std::size_t>(width) * 4U);
    upload->Unmap(0, nullptr);

    if (FAILED(m_commandAllocators[0]->Reset()) || FAILED(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr))) {
        LogError("Direct3D 12: texture upload command recording reset failed.");
        return false;
    }
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = m_modelAlbedoTexture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    m_commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    D3D12_RESOURCE_BARRIER toShaderResource{};
    toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource.Transition.pResource = m_modelAlbedoTexture.Get();
    toShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toShaderResource);
    if (FAILED(m_commandList->Close())) return false;
    ID3D12CommandList* commandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, commandLists);
    if (!WaitForGpu()) {
        LogError("Direct3D 12: waiting for model texture upload failed.");
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(7) * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_modelAlbedoTexture.Get(), &srv, handle);
    return true;
}

bool D3D12Renderer::CreatePipeline()
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT result = D3DCompile(kSceneShader, sizeof(kSceneShader) - 1, nullptr, nullptr, nullptr,
        "VSMain", "vs_5_0", compileFlags, 0, vertexShader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: vertex shader compilation failed.");
        return false;
    }
    errors.Reset();
    result = D3DCompile(kSceneShader, sizeof(kSceneShader) - 1, nullptr, nullptr, nullptr,
        "PSMain", "ps_5_0", compileFlags, 0, pixelShader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: pixel shader compilation failed.");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 12;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rootParameters[3]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC staticSampler{};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.MipLODBias = 0.0F;
    staticSampler.MaxAnisotropy = 1;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSampler.MinLOD = 0.0F;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0;
    staticSampler.RegisterSpace = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 3;
    rootDescription.pParameters = rootParameters;
    rootDescription.NumStaticSamplers = 1;
    rootDescription.pStaticSamplers = &staticSampler;
    rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    errors.Reset();
    result = D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRoot.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        return false;
    }
    if (FAILED(m_device->CreateRootSignature(0, serializedRoot->GetBufferPointer(), serializedRoot->GetBufferSize(),
            IID_PPV_ARGS(m_rootSignature.GetAddressOf())))) {
        LogError("Direct3D 12: root signature creation failed.");
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    pso.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultBlend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = defaultBlend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    pso.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    pso.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {inputLayout, static_cast<UINT>(std::size(inputLayout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    D3D12_RANGE noRead{0, 0};
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (!CreateUploadBuffer(m_device.Get(), 1280, m_constantBuffers[i]) ||
            FAILED(m_constantBuffers[i]->Map(0, &noRead, &m_mappedConstants[i]))) {
            LogError("Direct3D 12: per-frame scene constant buffer allocation failed.");
            return false;
        }
    }
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_pipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: graphics pipeline state creation failed.");
        return false;
    }
    return true;
}

bool D3D12Renderer::CreateShadowPipeline()
{
    constexpr char shader[] = R"(
cbuffer ShadowConstants : register(b0) { float4x4 lightViewProjection; };
struct VSInput { float3 position : POSITION; };
float4 VSMain(VSInput input) : SV_POSITION
{
    return mul(lightViewProjection, float4(input.position, 1.0));
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_shadow", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: shadow vertex shader compilation failed.");
        return false;
    }
    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthBias = 1200;
    pso.RasterizerState.DepthBiasClamp = 0.002F;
    pso.RasterizerState.SlopeScaledDepthBias = 1.5F;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {input, static_cast<UINT>(std::size(input))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 0;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_shadowPipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: cascaded shadow pipeline creation failed.");
        return false;
    }
    return true;
}

bool D3D12Renderer::CreateTemporalPipeline()
{
    constexpr char shader[] = R"(
Texture2D<float4> hdrScene : register(t0);
Texture2D<float> sceneDepth : register(t1);
#if HISTORY_READ_INDEX == 0
Texture2D<float4> historyColor0 : register(t3);
Texture2D<float> historyDepth0 : register(t5);
#else
Texture2D<float4> historyColor1 : register(t4);
Texture2D<float> historyDepth1 : register(t6);
#endif
SamplerState linearClamp : register(s0);
cbuffer TemporalConstants : register(b1)
{
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4 temporalParams;
};
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
float4 ReadHistoryColor(float2 uv)
{
#if HISTORY_READ_INDEX == 0
    return historyColor0.SampleLevel(linearClamp, uv, 0);
#else
    return historyColor1.SampleLevel(linearClamp, uv, 0);
#endif
}
float ReadHistoryDepth(float2 uv)
{
#if HISTORY_READ_INDEX == 0
    return historyDepth0.SampleLevel(linearClamp, uv, 0);
#else
    return historyDepth1.SampleLevel(linearClamp, uv, 0);
#endif
}
struct TemporalOutput { float4 color : SV_TARGET0; float depth : SV_TARGET1; };
TemporalOutput PSMain(VSOut input)
{
    TemporalOutput output;
    float depth = sceneDepth.SampleLevel(linearClamp, input.uv, 0);
    float3 current = max(hdrScene.SampleLevel(linearClamp, input.uv, 0).rgb, 0.0);
    output.depth = depth;
    if (temporalParams.x < 0.5 || depth >= 0.99999)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 world = mul(inverseViewProjection, float4(ndc, depth, 1.0));
    if (abs(world.w) < 1.0e-6)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    world /= world.w;
    float4 previousClip = mul(previousViewProjection, world);
    if (previousClip.w <= 1.0e-5)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float2 previousUv = float2(previousClip.x / previousClip.w * 0.5 + 0.5,
        0.5 - previousClip.y / previousClip.w * 0.5);
    if (any(previousUv <= 0.001) || any(previousUv >= 0.999))
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float oldDepth = ReadHistoryDepth(previousUv);
    float expectedDepth = previousClip.z / previousClip.w;
    if (abs(oldDepth - expectedDepth) > max(0.0015, expectedDepth * 0.012))
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float3 neighborhoodMin = current;
    float3 neighborhoodMax = current;
    uint width, height;
    hdrScene.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(width, 1u), max(height, 1u));
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float3 sampleColor = hdrScene.SampleLevel(linearClamp,
                saturate(input.uv + float2(x, y) * texel), 0).rgb;
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }
    float3 history = ReadHistoryColor(previousUv).rgb;
    float3 expansion = (neighborhoodMax - neighborhoodMin) * 0.12 + 0.002;
    history = clamp(history, neighborhoodMin - expansion, neighborhoodMax + expansion);
    output.color = float4(lerp(current, history, 0.88), 1.0);
    return output;
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_temporal", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: temporal vertex shader compilation failed.");
        return false;
    }
    std::array<Microsoft::WRL::ComPtr<ID3DBlob>, 2> pixelShaders;
    const D3D_SHADER_MACRO readHistory0[] = {{"HISTORY_READ_INDEX", "0"}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO readHistory1[] = {{"HISTORY_READ_INDEX", "1"}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO* historyDefines[] = {readHistory0, readHistory1};
    for (UINT i = 0; i < 2; ++i) {
        errors.Reset();
        hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_temporal", historyDefines[i], nullptr,
            "PSMain", "ps_5_0", flags, 0, pixelShaders[i].GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
            LogError("Direct3D 12: temporal pixel shader compilation failed.");
            return false;
        }
    }
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (!CreateUploadBuffer(m_device.Get(), 256, m_temporalConstantBuffers[i]) ||
            FAILED(m_temporalConstantBuffers[i]->Map(0, nullptr, &m_mappedTemporalConstants[i]))) {
            LogError("Direct3D 12: per-frame temporal constant buffer allocation failed.");
            return false;
        }
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {pixelShaders[0]->GetBufferPointer(), pixelShaders[0]->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {nullptr, 0};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;
    for (UINT i = 0; i < 2; ++i) {
        pso.PS = {pixelShaders[i]->GetBufferPointer(), pixelShaders[i]->GetBufferSize()};
        if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_temporalPipelineStates[i].GetAddressOf())))) {
            LogError("Direct3D 12: temporal resolve pipeline creation failed.");
            return false;
        }
    }
    return true;
}

bool D3D12Renderer::CreatePostProcessPipeline()
{
    constexpr char shader[] = R"(
Texture2D hdrScene : register(t0);
Texture2D sceneDepth : register(t1);
Texture2D<float4> historyColor0 : register(t3);
Texture2D<float4> historyColor1 : register(t4);
SamplerState linearClamp : register(s0);
cbuffer TemporalConstants : register(b1)
{
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4 temporalParams;
};
float3 ResolvedColor(float2 uv)
{
    return temporalParams.z < 0.5
        ? historyColor0.SampleLevel(linearClamp, uv, 0).rgb
        : historyColor1.SampleLevel(linearClamp, uv, 0).rgb;
}
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
float3 ACES(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
float4 PSMain(VSOut input) : SV_TARGET
{
    float depth = sceneDepth.SampleLevel(linearClamp, input.uv, 0).r;
    if (depth >= 0.99999)
    {
        float horizon = saturate(1.0 - abs(input.uv.y - 0.52) * 1.7);
        float3 skyTop = float3(0.025, 0.055, 0.105);
        float3 horizonColor = float3(0.50, 0.59, 0.66);
        float3 sky = lerp(skyTop, horizonColor, pow(saturate(1.0 - input.uv.y), 0.72));
        float2 sunUv = input.uv - float2(0.73, 0.27);
        float sun = exp(-dot(sunUv, sunUv) * 1900.0);
        float halo = exp(-dot(sunUv, sunUv) * 85.0);
        sky += float3(7.0, 4.6, 2.4) * sun + float3(0.8, 0.38, 0.13) * halo;
        return float4(pow(ACES(sky), 1.0 / 2.2), 1.0);
    }
    uint width, height;
    hdrScene.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(width, 1u), max(height, 1u));
    float3 color = max(ResolvedColor(input.uv), 0.0);
    // Small cross-kernel contact AO approximation from neighboring depth discontinuities.
    float ao = 1.0;
    const float2 offsets[8] = {
        float2(1,0), float2(-1,0), float2(0,1), float2(0,-1),
        float2(2,1), float2(-2,1), float2(2,-1), float2(-2,-1)
    };
    float occlusion = 0.0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float sampleDepth = sceneDepth.SampleLevel(linearClamp, saturate(input.uv + offsets[i] * texel * 2.0), 0).r;
        occlusion += step(0.00015, depth - sampleDepth) * saturate(1.0 - abs(depth - sampleDepth) * 85.0);
    }
    ao = 1.0 - saturate(occlusion / 8.0) * 0.24;
    color *= ao;
    float3 bloom = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y)
    {
        [unroll] for (int x = -2; x <= 2; ++x)
        {
            float3 sampleColor = ResolvedColor(
                saturate(input.uv + float2(x, y) * texel * 3.0));
            float luminance = dot(sampleColor, float3(0.2126, 0.7152, 0.0722));
            bloom += sampleColor * smoothstep(1.0, 2.8, luminance);
        }
    }
    color += bloom * (0.65 / 25.0);
    color = ACES(color * 1.05);
    color = pow(max(color, 0.0), 1.0 / 2.2);
    return float4(color, 1.0);
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_post", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: post-process vertex shader compilation failed.");
        return false;
    }
    errors.Reset();
    hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_post", nullptr, nullptr,
        "PSMain", "ps_5_0", flags, 0, ps.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: post-process pixel shader compilation failed.");
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.InputLayout = {nullptr, 0};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_postProcessPipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: post-process pipeline creation failed.");
        return false;
    }
    return true;
}

bool D3D12Renderer::WaitForFrame(UINT frameIndex)
{
    if (m_fence->GetCompletedValue() >= m_fenceValues[frameIndex]) return true;
    if (FAILED(m_fence->SetEventOnCompletion(m_fenceValues[frameIndex], m_fenceEvent))) return false;
    return WaitForSingleObject(m_fenceEvent, INFINITE) == WAIT_OBJECT_0;
}

bool D3D12Renderer::WaitForGpu()
{
    if (!m_commandQueue || !m_fence || m_fenceEvent == nullptr) return true;
    const UINT64 value = m_nextFenceValue++;
    if (FAILED(m_commandQueue->Signal(m_fence.Get(), value))) return false;
    if (FAILED(m_fence->SetEventOnCompletion(value, m_fenceEvent))) return false;
    if (WaitForSingleObject(m_fenceEvent, INFINITE) != WAIT_OBJECT_0) return false;
    m_fenceValues.fill(value);
    return true;
}

bool D3D12Renderer::Resize(int width, int height)
{
    if (m_failed) return false;
    if (width <= 0 || height <= 0 ||
        (m_width == static_cast<UINT>(width) && m_height == static_cast<UINT>(height))) return true;
    if (!WaitForGpu()) {
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
        char message[160]{};
        std::snprintf(message, sizeof(message), "Direct3D 12: swap-chain resize failed (HRESULT 0x%08lX).",
            static_cast<unsigned long>(resizeResult));
        LogError(message);
        return false;
    }
    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);
    if (!CreateRenderTargets() || !CreateDepthTarget() || !CreateHdrTarget() || !CreateTemporalTargets()) return false;
    m_historyValid = false;
    m_historyWriteIndex = 0;
    m_previousViewProjection = Mat4::Identity();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

void D3D12Renderer::Render(float elapsedSeconds, float cameraX, float cameraY, float cameraZ,
                           float cameraYaw, float cameraPitch)
{
    if (m_failed) return;
    if (!WaitForFrame(m_frameIndex)) {
        LogError("Direct3D 12: waiting for a reusable frame failed.");
        m_failed = true;
        return;
    }

    const Vec3 eye{cameraX, cameraY, cameraZ};
    const Vec3 forward{std::sin(cameraYaw) * std::cos(cameraPitch), std::sin(cameraPitch),
                       std::cos(cameraYaw) * std::cos(cameraPitch)};
    const auto view = Mat4::LookAtLH(eye, eye + forward, {0.0F, 1.0F, 0.0F});
    if (!view) return;
    const Mat4 projection = Mat4::Perspective(math::DegreesToRadians(68.0F),
        static_cast<float>(m_width) / static_cast<float>(m_height), 0.08F, 260.0F);
    SceneConstants constants{};
    constants.viewProjection = projection * *view;
    const Vec3 lightDirection = *Vec3{-0.42F, 0.82F, -0.36F}.Normalized();
    constexpr float cascadeSplits[] = {28.0F, 80.0F, 220.0F};
    float previousSplit = 0.08F;
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        const float split = cascadeSplits[cascade];
        const float midpoint = (previousSplit + split) * 0.5F;
        const Vec3 center = eye + forward * midpoint;
        const float radius = split * (cascade == 0 ? 0.92F : 0.82F);
        const Vec3 lightEye = center + lightDirection * (radius * 2.4F + 30.0F);
        const auto lightView = Mat4::LookAtLH(lightEye, center, {0.0F, 1.0F, 0.0F});
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
    constants.cameraPosition[3] = 1.0F;
    constants.lighting[0] = -0.42F;
    constants.lighting[1] = 0.82F;
    constants.lighting[2] = -0.36F;
    constants.lighting[3] = elapsedSeconds;
    const float cameraDeltaX = cameraX - m_previousCameraX;
    const float cameraDeltaY = cameraY - m_previousCameraY;
    const float cameraDeltaZ = cameraZ - m_previousCameraZ;
    const bool cameraCut = (cameraDeltaX * cameraDeltaX + cameraDeltaY * cameraDeltaY + cameraDeltaZ * cameraDeltaZ > 64.0F) ||
        std::abs(cameraYaw - m_previousCameraYaw) > 0.7F || std::abs(cameraPitch - m_previousCameraPitch) > 0.7F;
    if (cameraCut) m_historyValid = false;
    TemporalConstants temporalConstants{};
    if (!InvertMatrix(constants.viewProjection, temporalConstants.inverseViewProjection)) return;
    temporalConstants.previousViewProjection = m_previousViewProjection;
    temporalConstants.parameters[0] = m_historyValid ? 1.0F : 0.0F;
    temporalConstants.parameters[1] = static_cast<float>(1U - m_historyWriteIndex);
    temporalConstants.parameters[2] = static_cast<float>(m_historyWriteIndex);
    temporalConstants.parameters[3] = 0.88F;
    std::memcpy(m_mappedTemporalConstants[m_frameIndex], &temporalConstants, sizeof(temporalConstants));
    std::memcpy(m_mappedConstants[m_frameIndex], &constants, sizeof(constants));
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        std::memcpy(static_cast<unsigned char*>(m_mappedConstants[m_frameIndex]) + kShadowConstantOffset + cascade * kShadowConstantStride,
            &constants.lightViewProjection[cascade], sizeof(Mat4));
    }

    if (FAILED(m_commandAllocators[m_frameIndex]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()))) {
        LogError("Direct3D 12: command recording reset failed.");
        m_failed = true;
        return;
    }

    D3D12_RESOURCE_BARRIER shadowToDepth{};
    shadowToDepth.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    shadowToDepth.Transition.pResource = m_shadowTexture.Get();
    shadowToDepth.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    shadowToDepth.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    shadowToDepth.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &shadowToDepth);
    D3D12_VIEWPORT shadowViewport{0.0F, 0.0F, static_cast<float>(kShadowResolution), static_cast<float>(kShadowResolution), 0.0F, 1.0F};
    D3D12_RECT shadowScissor{0, 0, static_cast<LONG>(kShadowResolution), static_cast<LONG>(kShadowResolution)};
    m_commandList->RSSetViewports(1, &shadowViewport);
    m_commandList->RSSetScissorRects(1, &shadowScissor);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_shadowPipelineState.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        m_commandList->OMSetRenderTargets(0, nullptr, &m_shadowDsvHandles[cascade]);
        m_commandList->ClearDepthStencilView(m_shadowDsvHandles[cascade], D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
        m_commandList->SetGraphicsRootConstantBufferView(0,
            m_constantBuffers[m_frameIndex]->GetGPUVirtualAddress() + kShadowConstantOffset + cascade * kShadowConstantStride);
        const float cascadeRange = cascadeSplits[cascade];
        const UINT shadowLod = cascade == 0 ? 0U : (cascade == 1 ? 1U : 2U);
        for (const DrawChunk& chunk : m_drawChunks) {
            const Vec3 toChunk{chunk.centerX - cameraX, chunk.centerY - cameraY, chunk.centerZ - cameraZ};
            if (toChunk.Length() > cascadeRange + chunk.radius * 1.5F) continue;
            if (chunk.indexCount[shadowLod] == 0) continue;
            m_commandList->DrawIndexedInstanced(chunk.indexCount[shadowLod], 1, chunk.startIndex[shadowLod], 0, 0);
        }
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
    const Vec3 cameraRight{std::cos(cameraYaw), 0.0F, -std::sin(cameraYaw)};
    const auto normalizedUp = forward.Cross(cameraRight).Normalized();
    const Vec3 cameraUp = normalizedUp.value_or(Vec3{0.0F, 1.0F, 0.0F});
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const float tanHalfVerticalFov = std::tan(math::DegreesToRadians(34.0F));
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
        const UINT lod = distance < 65.0F ? 0U : (distance < 140.0F ? 1U : 2U);
        if (chunk.indexCount[lod] != 0)
            m_commandList->DrawIndexedInstanced(chunk.indexCount[lod], 1, chunk.startIndex[lod], 0, 0);
    }

    D3D12_RESOURCE_BARRIER depthToSample{};
    depthToSample.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    depthToSample.Transition.pResource = m_depthTexture.Get();
    depthToSample.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthToSample.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    depthToSample.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &depthToSample);

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
    D3D12_RESOURCE_BARRIER depthToWrite = depthToSample;
    depthToWrite.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
    const HRESULT presentResult = m_swapChain->Present(1, 0);
    const UINT64 signalValue = m_nextFenceValue++;
    const HRESULT signalResult = m_commandQueue->Signal(m_fence.Get(), signalValue);
    if (FAILED(signalResult)) {
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
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
} // namespace vanta
