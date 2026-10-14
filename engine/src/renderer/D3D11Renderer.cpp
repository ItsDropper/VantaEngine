#include "vanta/renderer/D3D11Renderer.hpp"

#include "vanta/core/Log.hpp"
#include "vanta/math/Math.hpp"
#include "vanta/renderer/ObjLoader.hpp"
#include "vanta/renderer/WicTextureLoader.hpp"
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <d3dcompiler.h>
#include <iterator>

namespace vanta
{
namespace
{
using math::Mat4;
using math::Quaternion;
using math::Vec3;

struct Vertex
{
    float position[3];
    float normal[3];
    float color[4];
    float uv[2];
};

struct SceneConstants
{
    Mat4 worldViewProjection;
    Mat4 world;
    UINT useTexture;
    float padding[3];
    float cameraPosition[3];
    float fogDensity;
    float lightDirection[3];
    float fogStart;
    float materialProperties[4];
    float baseColorFactor[4];
};

constexpr Vertex kCubeVertices[] = {
    // Front (+Z)
    {{-1,-1, 1},{0,0,1},{0.08F,0.78F,1.00F,1}},
    {{-1, 1, 1},{0,0,1},{0.08F,0.78F,1.00F,1}},
    {{ 1, 1, 1},{0,0,1},{0.08F,0.78F,1.00F,1}},
    {{ 1,-1, 1},{0,0,1},{0.08F,0.78F,1.00F,1}},
    // Back (-Z)
    {{ 1,-1,-1},{0,0,-1},{0.18F,0.35F,1.00F,1}},
    {{ 1, 1,-1},{0,0,-1},{0.18F,0.35F,1.00F,1}},
    {{-1, 1,-1},{0,0,-1},{0.18F,0.35F,1.00F,1}},
    {{-1,-1,-1},{0,0,-1},{0.18F,0.35F,1.00F,1}},
    // Top (+Y)
    {{-1, 1, 1},{0,1,0},{0.30F,0.95F,0.62F,1}},
    {{-1, 1,-1},{0,1,0},{0.30F,0.95F,0.62F,1}},
    {{ 1, 1,-1},{0,1,0},{0.30F,0.95F,0.62F,1}},
    {{ 1, 1, 1},{0,1,0},{0.30F,0.95F,0.62F,1}},
    // Bottom (-Y)
    {{-1,-1,-1},{0,-1,0},{0.08F,0.48F,0.38F,1}},
    {{-1,-1, 1},{0,-1,0},{0.08F,0.48F,0.38F,1}},
    {{ 1,-1, 1},{0,-1,0},{0.08F,0.48F,0.38F,1}},
    {{ 1,-1,-1},{0,-1,0},{0.08F,0.48F,0.38F,1}},
    // Right (+X)
    {{1,-1, 1},{1,0,0},{1.00F,0.42F,0.24F,1}},
    {{1, 1, 1},{1,0,0},{1.00F,0.42F,0.24F,1}},
    {{1, 1,-1},{1,0,0},{1.00F,0.42F,0.24F,1}},
    {{1,-1,-1},{1,0,0},{1.00F,0.42F,0.24F,1}},
    // Left (-X)
    {{-1,-1,-1},{-1,0,0},{0.78F,0.28F,0.92F,1}},
    {{-1, 1,-1},{-1,0,0},{0.78F,0.28F,0.92F,1}},
    {{-1, 1, 1},{-1,0,0},{0.78F,0.28F,0.92F,1}},
    {{-1,-1, 1},{-1,0,0},{0.78F,0.28F,0.92F,1}},
};

constexpr unsigned short kCubeIndices[] = {
     0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7,
     8, 9,10, 8,10,11, 12,13,14,12,14,15,
    16,17,18,16,18,19, 20,21,22,20,22,23
};

constexpr char kShaderSource[] = R"(
cbuffer SceneConstants : register(b0)
{
    float4x4 worldViewProjection;
    float4x4 world;
    uint useTexture;
    float3 padding;
    float3 cameraPosition;
    float fogDensity;
    float3 sunDirection;
    float fogStart;
    float4 materialProperties;
    float4 baseColorFactor;
};
Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float3 worldPosition : TEXCOORD1;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(worldViewProjection, float4(input.position, 1.0));
    output.normal = normalize(mul((float3x3)world, input.normal));
    output.worldPosition = mul(world, float4(input.position, 1.0)).xyz;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float DistributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = saturate(dot(n, h));
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denominator * denominator, 0.0001);
}
float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}
float GeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    return GeometrySchlickGGX(saturate(dot(n, v)), roughness) *
           GeometrySchlickGGX(saturate(dot(n, l)), roughness);
}
float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosTheta), 5.0);
}
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
float4 PSMain(PixelInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float3 v = normalize(cameraPosition - input.worldPosition);
    float3 l = normalize(-sunDirection);
    float3 h = normalize(v + l);
    float roughness = clamp(materialProperties.x, 0.04, 1.0);
    float metallic = saturate(materialProperties.y);
    float4 sampled = useTexture != 0 ? diffuseTexture.Sample(diffuseSampler, input.uv) : input.color;
    float3 albedo = max(sampled.rgb * baseColorFactor.rgb, 0.0);
    float nDotL = saturate(dot(n, l));
    float nDotV = max(saturate(dot(n, v)), 0.001);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 f = FresnelSchlick(saturate(dot(h, v)), f0);
    float ndf = DistributionGGX(n, h, roughness);
    float geometry = GeometrySmith(n, v, l, roughness);
    float3 specular = (ndf * geometry * f) / max(4.0 * nDotV * max(nDotL, 0.001), 0.001);
    float3 kD = (1.0 - f) * (1.0 - metallic);
    float3 radiance = float3(4.2, 3.45, 2.55);
    float3 direct = (kD * albedo / 3.14159265 + specular) * radiance * nDotL;
    float3 sky = lerp(float3(0.025, 0.035, 0.055), float3(0.22, 0.30, 0.38), saturate(n.y));
    float3 ambient = sky * albedo * (1.0 - metallic) * (1.0 - roughness * 0.25);
    float3 color = ACESFilm(direct + ambient);
    color = pow(max(color, 0.0), 1.0 / 2.2);
    float distanceToCamera = distance(cameraPosition, input.worldPosition);
    float fog = saturate(1.0 - exp(-max(distanceToCamera - fogStart, 0.0) * fogDensity));
    float3 fogColor = float3(0.42, 0.48, 0.55);
    return float4(lerp(color, fogColor, fog), sampled.a * baseColorFactor.a);
}
)";

constexpr char kSkyShaderSource[] = R"(
struct SkyVertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
SkyVertexOutput VSMain(uint vertexId : SV_VertexID)
{
    SkyVertexOutput output;
    float2 p = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 1.0, 1.0);
    output.uv = p;
    return output;
}
float4 PSMain(SkyVertexOutput input) : SV_TARGET
{
    float t = saturate(input.uv.y);
    float3 zenith = float3(0.018, 0.035, 0.085);
    float3 upperSky = float3(0.055, 0.12, 0.22);
    float3 horizon = float3(0.92, 0.43, 0.22);
    float3 lowerHorizon = float3(0.36, 0.40, 0.46);
    float3 sky = lerp(zenith, upperSky, smoothstep(0.0, 0.55, t));
    sky = lerp(sky, horizon, smoothstep(0.48, 0.78, t) * 0.72);
    sky = lerp(sky, lowerHorizon, smoothstep(0.74, 1.0, t) * 0.65);
    float2 sunDelta = (input.uv - float2(0.72, 0.66)) * float2(1.0, 1.7);
    float sun = exp(-dot(sunDelta, sunDelta) * 120.0);
    float glow = exp(-dot(sunDelta, sunDelta) * 10.0);
    sky += float3(1.0, 0.45, 0.18) * (sun * 2.0 + glow * 0.22);
    return float4(sky, 1.0);
}
)";

bool CreateDepthResources(
    ID3D11Device* device,
    const UINT width,
    const UINT height,
    Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>& view)
{
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilView(texture.Get(), nullptr, view.GetAddressOf())))
    {
        LogError("Could not create the depth buffer.");
        return false;
    }
    return true;
}
}

bool D3D11Renderer::Initialize(const HWND window, const int width, const int height)
{
    DXGI_SWAP_CHAIN_DESC swapChainDescription{};
    swapChainDescription.BufferCount = 2;
    swapChainDescription.BufferDesc.Width = static_cast<UINT>(width);
    swapChainDescription.BufferDesc.Height = static_cast<UINT>(height);
    swapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.OutputWindow = window;
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.Windowed = TRUE;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    const HRESULT deviceResult = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels,
        static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
        &swapChainDescription, m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
        &selectedFeatureLevel, m_context.GetAddressOf());

    if (FAILED(deviceResult))
    {
        LogError("Direct3D 11 device/swap-chain creation failed.");
        return false;
    }

    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);

    if (!CreateRenderTarget() || !CreateDepthTarget() || !CreatePipeline())
    {
        return false;
    }

    LogInfo("Direct3D 11 renderer initialized (textured model pipeline, depth buffer, dynamic scene).");
    return true;
}

bool D3D11Renderer::CreateRenderTarget()
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
    {
        LogError("Could not retrieve the swap-chain back buffer.");
        return false;
    }
    if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTarget.GetAddressOf())))
    {
        LogError("Could not create the render-target view.");
        return false;
    }
    return true;
}

bool D3D11Renderer::CreateDepthTarget()
{
    m_depthTexture.Reset();
    m_depthStencil.Reset();
    return CreateDepthResources(m_device.Get(), m_width, m_height, m_depthTexture, m_depthStencil);
}

bool D3D11Renderer::CreatePipeline()
{
    Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;

    const HRESULT vertexCompile = D3DCompile(
        kShaderSource, sizeof(kShaderSource) - 1, "vanta_vertex", nullptr, nullptr,
        "VSMain", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        vertexBytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(vertexCompile))
    {
        LogError("Vertex shader compilation failed.");
        if (errors) { OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer())); }
        return false;
    }

    errors.Reset();
    const HRESULT pixelCompile = D3DCompile(
        kShaderSource, sizeof(kShaderSource) - 1, "vanta_pixel", nullptr, nullptr,
        "PSMain", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
        pixelBytecode.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(pixelCompile))
    {
        LogError("Pixel shader compilation failed.");
        if (errors) { OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer())); }
        return false;
    }

    if (FAILED(m_device->CreateVertexShader(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf())) ||
        FAILED(m_device->CreatePixelShader(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf())))
    {
        LogError("Could not create the Direct3D shader objects.");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> skyVertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> skyPixelBytecode;
    errors.Reset();
    if (FAILED(D3DCompile(kSkyShaderSource, sizeof(kSkyShaderSource) - 1, "vanta_sky_vertex", nullptr, nullptr,
            "VSMain", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, skyVertexBytecode.GetAddressOf(), errors.GetAddressOf()))) {
        LogError("Sky vertex shader compilation failed.");
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return false;
    }
    errors.Reset();
    if (FAILED(D3DCompile(kSkyShaderSource, sizeof(kSkyShaderSource) - 1, "vanta_sky_pixel", nullptr, nullptr,
            "PSMain", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, skyPixelBytecode.GetAddressOf(), errors.GetAddressOf()))) {
        LogError("Sky pixel shader compilation failed.");
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return false;
    }
    if (FAILED(m_device->CreateVertexShader(skyVertexBytecode->GetBufferPointer(), skyVertexBytecode->GetBufferSize(), nullptr, m_skyVertexShader.GetAddressOf())) ||
        FAILED(m_device->CreatePixelShader(skyPixelBytecode->GetBufferPointer(), skyPixelBytecode->GetBufferSize(), nullptr, m_skyPixelShader.GetAddressOf()))) {
        LogError("Could not create the sky shader objects.");
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC skyDepthDescription{};
    skyDepthDescription.DepthEnable = FALSE;
    skyDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    skyDepthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(m_device->CreateDepthStencilState(&skyDepthDescription, m_skyDepthState.GetAddressOf()))) {
        LogError("Could not create the sky depth state.");
        return false;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(m_device->CreateInputLayout(inputElements, static_cast<UINT>(std::size(inputElements)),
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), m_inputLayout.GetAddressOf())))
    {
        LogError("Could not create the vertex input layout.");
        return false;
    }

    D3D11_BUFFER_DESC vertexDescription{};
    vertexDescription.ByteWidth = sizeof(kCubeVertices);
    vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData{};
    vertexData.pSysMem = kCubeVertices;
    if (FAILED(m_device->CreateBuffer(&vertexDescription, &vertexData, m_vertexBuffer.GetAddressOf())))
    {
        LogError("Could not create the cube vertex buffer.");
        return false;
    }

    D3D11_BUFFER_DESC indexDescription{};
    indexDescription.ByteWidth = sizeof(kCubeIndices);
    indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexData{};
    indexData.pSysMem = kCubeIndices;
    if (FAILED(m_device->CreateBuffer(&indexDescription, &indexData, m_indexBuffer.GetAddressOf())))
    {
        LogError("Could not create the cube index buffer.");
        return false;
    }

    const std::filesystem::path modelPath = std::filesystem::path{VANTA_ASSET_DIR} / "models" / "stone_house.obj";
    renderer::CpuModel model;
    std::string importError;
    if (!renderer::LoadWavefrontObj(modelPath, model, importError)) {
        LogError(importError.c_str());
        return false;
    }
    std::vector<Vertex> modelVertices;
    modelVertices.reserve(model.vertices.size());
    for (const renderer::ModelVertex& source : model.vertices) {
        modelVertices.push_back({{source.position.x, source.position.y, source.position.z},
            {source.normal.x, source.normal.y, source.normal.z}, {1.0F, 1.0F, 1.0F, 1.0F},
            {source.u, source.v}});
    }
    if (modelVertices.empty() || model.indices.empty() ||
        modelVertices.size() * sizeof(Vertex) > std::numeric_limits<UINT>::max() ||
        model.indices.size() * sizeof(unsigned int) > std::numeric_limits<UINT>::max()) {
        LogError("Imported model has invalid or excessive geometry.");
        return false;
    }

    D3D11_BUFFER_DESC modelVertexDescription{};
    modelVertexDescription.ByteWidth = static_cast<UINT>(modelVertices.size() * sizeof(Vertex));
    modelVertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    modelVertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA modelVertexData{};
    modelVertexData.pSysMem = modelVertices.data();
    if (FAILED(m_device->CreateBuffer(&modelVertexDescription, &modelVertexData, m_modelVertexBuffer.GetAddressOf()))) {
        LogError("Could not create the imported model vertex buffer.");
        return false;
    }

    D3D11_BUFFER_DESC modelIndexDescription{};
    modelIndexDescription.ByteWidth = static_cast<UINT>(model.indices.size() * sizeof(unsigned int));
    modelIndexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    modelIndexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA modelIndexData{};
    modelIndexData.pSysMem = model.indices.data();
    if (FAILED(m_device->CreateBuffer(&modelIndexDescription, &modelIndexData, m_modelIndexBuffer.GetAddressOf()))) {
        LogError("Could not create the imported model index buffer.");
        return false;
    }
    m_modelIndexCount = static_cast<UINT>(model.indices.size());

    // Procedural rolling terrain: deterministic layered waves, vertex normals, and altitude/slope-based biome colors.
    constexpr int terrainCells = 128;
    constexpr float terrainExtent = 72.0F;
    const auto terrainHeight = [](const float x, const float z) {
        return -1.0F + 0.68F * std::sin(x * 0.075F) * std::cos(z * 0.061F)
            + 0.28F * std::sin((x + z) * 0.17F) + 0.15F * std::cos(z * 0.29F) * std::sin(x * 0.23F);
    };
    std::vector<Vertex> terrainVertices;
    std::vector<unsigned int> terrainIndices;
    terrainVertices.reserve(static_cast<std::size_t>(terrainCells + 1) * (terrainCells + 1));
    terrainIndices.reserve(static_cast<std::size_t>(terrainCells) * terrainCells * 6);
    for (int z = 0; z <= terrainCells; ++z) {
        for (int x = 0; x <= terrainCells; ++x) {
            const float worldX = (static_cast<float>(x) / terrainCells * 2.0F - 1.0F) * terrainExtent;
            const float worldZ = (static_cast<float>(z) / terrainCells * 2.0F - 1.0F) * terrainExtent;
            const float y = terrainHeight(worldX, worldZ);
            const float left = terrainHeight(worldX - 0.12F, worldZ);
            const float right = terrainHeight(worldX + 0.12F, worldZ);
            const float back = terrainHeight(worldX, worldZ - 0.12F);
            const float front = terrainHeight(worldX, worldZ + 0.12F);
            const float nx = (left - right) / 0.24F;
            const float nz = (back - front) / 0.24F;
            const float normalLength = std::sqrt(nx * nx + 1.0F + nz * nz);
            const float slope = 1.0F / normalLength;
            float red = 0.16F, green = 0.29F, blue = 0.13F;
            if (y > -0.15F) { red = 0.31F; green = 0.34F; blue = 0.22F; }
            if (y > 0.10F || slope < 0.88F) { red = 0.34F; green = 0.32F; blue = 0.28F; }
            if (y < -1.55F) { red = 0.12F; green = 0.24F; blue = 0.18F; }
            terrainVertices.push_back({{worldX, y, worldZ}, {nx / normalLength, 1.0F / normalLength, nz / normalLength},
                {red, green, blue, 1.0F}, {worldX * 0.12F, worldZ * 0.12F}});
        }
    }
    for (int z = 0; z < terrainCells; ++z) {
        for (int x = 0; x < terrainCells; ++x) {
            const unsigned int a = static_cast<unsigned int>(z * (terrainCells + 1) + x);
            const unsigned int b = a + static_cast<unsigned int>(terrainCells + 1);
            terrainIndices.insert(terrainIndices.end(), {a, b, a + 1U, a + 1U, b, b + 1U});
        }
    }
    D3D11_BUFFER_DESC terrainVertexDesc{};
    terrainVertexDesc.ByteWidth = static_cast<UINT>(terrainVertices.size() * sizeof(Vertex));
    terrainVertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    terrainVertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA terrainVertexData{};
    terrainVertexData.pSysMem = terrainVertices.data();
    D3D11_BUFFER_DESC terrainIndexDesc{};
    terrainIndexDesc.ByteWidth = static_cast<UINT>(terrainIndices.size() * sizeof(unsigned int));
    terrainIndexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    terrainIndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA terrainIndexData{};
    terrainIndexData.pSysMem = terrainIndices.data();
    if (FAILED(m_device->CreateBuffer(&terrainVertexDesc, &terrainVertexData, m_terrainVertexBuffer.GetAddressOf())) ||
        FAILED(m_device->CreateBuffer(&terrainIndexDesc, &terrainIndexData, m_terrainIndexBuffer.GetAddressOf()))) {
        LogError("Could not create procedural terrain buffers.");
        return false;
    }
    m_terrainIndexCount = static_cast<UINT>(terrainIndices.size());

    // Build a deterministic low-poly pine forest into one mesh to keep draw-call overhead low.
    std::vector<Vertex> forestVertices;
    std::vector<unsigned int> forestIndices;
    const auto addTriangle = [&](const Vec3& a, const Vec3& b, const Vec3& c, const float r, const float g, const float blue) {
        const Vec3 edgeA = b - a;
        const Vec3 edgeB = c - a;
        const Vec3 cross = edgeA.Cross(edgeB);
        const float length = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        if (length < 0.0001F) return;
        const Vec3 n{cross.x / length, cross.y / length, cross.z / length};
        const unsigned int base = static_cast<unsigned int>(forestVertices.size());
        const Vertex vA{{a.x,a.y,a.z},{n.x,n.y,n.z},{r,g,blue,1.0F},{0,0}};
        const Vertex vB{{b.x,b.y,b.z},{n.x,n.y,n.z},{r,g,blue,1.0F},{0,1}};
        const Vertex vC{{c.x,c.y,c.z},{n.x,n.y,n.z},{r,g,blue,1.0F},{1,0}};
        forestVertices.insert(forestVertices.end(), {vA,vB,vC});
        forestIndices.insert(forestIndices.end(), {base,base+1U,base+2U});
    };
    const auto hash01 = [](const int a, const int b) {
        const float value = std::sin(static_cast<float>(a * 127 + b * 311 + 47) * 12.9898F) * 43758.5453F;
        return value - std::floor(value);
    };
    for (int tz = -8; tz <= 8; ++tz) {
        for (int tx = -8; tx <= 8; ++tx) {
            const float x = static_cast<float>(tx) * 7.2F + (hash01(tx, tz) - 0.5F) * 4.0F;
            const float z = static_cast<float>(tz) * 7.2F + (hash01(tz, tx) - 0.5F) * 4.0F;
            if (x * x + z * z < 85.0F || std::abs(x) > 57.0F || std::abs(z) > 57.0F) continue;
            const float ground = terrainHeight(x, z);
            const float treeHeight = 3.6F + hash01(tx + 9, tz - 5) * 2.4F;
            const float radius = 0.75F + hash01(tx - 4, tz + 2) * 0.55F;
            const float trunkRadius = 0.12F + hash01(tx + 2, tz + 8) * 0.07F;
            const Vec3 trunkA{x-trunkRadius,ground,z-trunkRadius}, trunkB{x+trunkRadius,ground,z-trunkRadius};
            const Vec3 trunkC{x+trunkRadius,ground+treeHeight*0.42F,z-trunkRadius}, trunkD{x-trunkRadius,ground+treeHeight*0.42F,z-trunkRadius};
            const Vec3 trunkE{x-trunkRadius,ground,z+trunkRadius}, trunkF{x+trunkRadius,ground,z+trunkRadius};
            const Vec3 trunkG{x+trunkRadius,ground+treeHeight*0.42F,z+trunkRadius}, trunkH{x-trunkRadius,ground+treeHeight*0.42F,z+trunkRadius};
            const float bark = 0.22F + hash01(tx, tz + 12) * 0.08F;
            addTriangle(trunkA,trunkB,trunkC,bark,0.12F,0.065F); addTriangle(trunkA,trunkC,trunkD,bark,0.12F,0.065F);
            addTriangle(trunkF,trunkE,trunkH,bark,0.12F,0.065F); addTriangle(trunkF,trunkH,trunkG,bark,0.12F,0.065F);
            addTriangle(trunkE,trunkA,trunkD,bark,0.12F,0.065F); addTriangle(trunkE,trunkD,trunkH,bark,0.12F,0.065F);
            addTriangle(trunkB,trunkF,trunkG,bark,0.12F,0.065F); addTriangle(trunkB,trunkG,trunkC,bark,0.12F,0.065F);
            constexpr int sides = 7;
            for (int tier = 0; tier < 3; ++tier) {
                const float baseY = ground + treeHeight * (0.22F + static_cast<float>(tier) * 0.20F);
                const float tipY = baseY + treeHeight * 0.48F;
                const float tierRadius = radius * (1.0F - static_cast<float>(tier) * 0.23F);
                const Vec3 tip{x,tipY,z};
                for (int side = 0; side < sides; ++side) {
                    const float a0 = static_cast<float>(side) * 6.2831853F / sides;
                    const float a1 = static_cast<float>(side+1) * 6.2831853F / sides;
                    const Vec3 p0{x+std::cos(a0)*tierRadius,baseY,z+std::sin(a0)*tierRadius};
                    const Vec3 p1{x+std::cos(a1)*tierRadius,baseY,z+std::sin(a1)*tierRadius};
                    const float shade = 0.72F + hash01(tx + side, tz + tier) * 0.24F;
                    addTriangle(p0,p1,tip,0.035F*shade,0.20F*shade,0.105F*shade);
                }
            }
        }
    }
    D3D11_BUFFER_DESC forestVertexDesc{};
    forestVertexDesc.ByteWidth = static_cast<UINT>(forestVertices.size() * sizeof(Vertex));
    forestVertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    forestVertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA forestVertexData{};
    forestVertexData.pSysMem = forestVertices.data();
    D3D11_BUFFER_DESC forestIndexDesc{};
    forestIndexDesc.ByteWidth = static_cast<UINT>(forestIndices.size() * sizeof(unsigned int));
    forestIndexDesc.Usage = D3D11_USAGE_IMMUTABLE;
    forestIndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA forestIndexData{};
    forestIndexData.pSysMem = forestIndices.data();
    if (forestVertices.empty() || FAILED(m_device->CreateBuffer(&forestVertexDesc, &forestVertexData, m_forestVertexBuffer.GetAddressOf())) ||
        FAILED(m_device->CreateBuffer(&forestIndexDesc, &forestIndexData, m_forestIndexBuffer.GetAddressOf()))) {
        LogError("Could not create procedural forest buffers.");
        return false;
    }
    m_forestIndexCount = static_cast<UINT>(forestIndices.size());

    renderer::CpuTexture image;
    if (model.diffuseTexture.empty() || !renderer::LoadWicTexture(model.diffuseTexture, image)) {
        LogError("Imported model has no readable diffuse texture (check its MTL map_Kd entry).");
        return false;
    }
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = image.width;
    textureDescription.Height = image.height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA textureData{};
    textureData.pSysMem = image.rgba.data();
    textureData.SysMemPitch = image.width * 4U;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(m_device->CreateTexture2D(&textureDescription, &textureData, texture.GetAddressOf())) ||
        FAILED(m_device->CreateShaderResourceView(texture.Get(), nullptr, m_diffuseTexture.GetAddressOf()))) {
        LogError("Could not upload the imported model's diffuse texture.");
        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&samplerDescription, m_textureSampler.GetAddressOf()))) {
        LogError("Could not create the material texture sampler.");
        return false;
    }

    D3D11_BUFFER_DESC constantsDescription{};
    constantsDescription.ByteWidth = sizeof(SceneConstants);
    constantsDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantsDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantsDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&constantsDescription, nullptr, m_sceneConstants.GetAddressOf())))
    {
        LogError("Could not create the scene constant buffer.");
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDescription{};
    depthDescription.DepthEnable = TRUE;
    depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDescription.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&depthDescription, m_depthState.GetAddressOf())))
    {
        LogError("Could not create the depth state.");
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDescription{};
    rasterDescription.FillMode = D3D11_FILL_SOLID;
    rasterDescription.CullMode = D3D11_CULL_NONE;
    rasterDescription.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rasterDescription, m_rasterizerState.GetAddressOf())))
    {
        LogError("Could not create the rasterizer state.");
        return false;
    }

    return true;
}

bool D3D11Renderer::Resize(const int width, const int height)
{
    if (width <= 0 || height <= 0 ||
        (m_width == static_cast<UINT>(width) && m_height == static_cast<UINT>(height)))
    {
        return true;
    }

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    m_depthStencil.Reset();
    m_depthTexture.Reset();

    const HRESULT resizeResult = m_swapChain->ResizeBuffers(0, static_cast<UINT>(width),
        static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resizeResult))
    {
        LogError("Swap-chain resize failed.");
        return false;
    }

    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);
    return CreateRenderTarget() && CreateDepthTarget();
}

void D3D11Renderer::Render(const float elapsedSeconds, const float cameraX, const float cameraY, const float cameraZ, const float cameraYaw, const float cameraPitch)
{
    constexpr float clearColor[] = {0.018F, 0.025F, 0.055F, 1.0F};
    ID3D11RenderTargetView* target = m_renderTarget.Get();
    m_context->OMSetRenderTargets(1, &target, m_depthStencil.Get());
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);
    m_context->ClearDepthStencilView(m_depthStencil.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0F, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    m_context->RSSetViewports(1, &viewport);
    m_context->RSSetState(m_rasterizerState.Get());

    // Full-screen atmospheric sky pass: a gradient and solar glow, rendered without depth writes.
    m_context->OMSetDepthStencilState(m_skyDepthState.Get(), 0);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_skyVertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_skyPixelShader.Get(), nullptr, 0);
    m_context->Draw(3, 0);
    m_context->OMSetDepthStencilState(m_depthState.Get(), 0);

    const Vec3 cameraPosition{cameraX, cameraY, cameraZ};
    const float cosPitch = std::cos(cameraPitch);
    const Vec3 cameraForward{std::sin(cameraYaw) * cosPitch, std::sin(cameraPitch), std::cos(cameraYaw) * cosPitch};
    const Mat4 view = Mat4::LookAtLH(cameraPosition, cameraPosition + cameraForward, {0.0F, 1.0F, 0.0F})
        .value_or(Mat4::Identity());
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const Mat4 projection = Mat4::Perspective(math::DegreesToRadians(55.0F), aspect, 0.1F, 150.0F);

    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    ID3D11Buffer* constantsBuffer = m_sceneConstants.Get();
    m_context->VSSetConstantBuffers(0, 1, &constantsBuffer);
    m_context->PSSetConstantBuffers(0, 1, &constantsBuffer);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    const auto drawCube = [&](const Mat4& worldTransform)
    {
        const SceneConstants constants{projection * view * worldTransform, worldTransform, 0U, {0.0F, 0.0F, 0.0F},
            {cameraX, cameraY, cameraZ}, 0.018F, {-0.45F, -0.8F, 0.55F}, 5.0F,
            {0.32F, 0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}};
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(m_sceneConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            LogError("Could not update the per-object scene constants.");
            return;
        }
        *static_cast<SceneConstants*>(mapped.pData) = constants;
        m_context->Unmap(m_sceneConstants.Get(), 0);
        m_context->DrawIndexed(static_cast<UINT>(std::size(kCubeIndices)), 0, 0);
    };

    // Render terrain and forest as merged static meshes; the ground is now a real walkable-scale landscape.
    const Mat4 identity = Mat4::Identity();
    const SceneConstants worldConstants{projection * view, identity, 0U, {0.0F, 0.0F, 0.0F},
        {cameraX, cameraY, cameraZ}, 0.018F, {-0.45F, -0.8F, 0.55F}, 5.0F,
        {0.92F, 0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F, 1.0F}};
    D3D11_MAPPED_SUBRESOURCE worldMapped{};
    if (SUCCEEDED(m_context->Map(m_sceneConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &worldMapped))) {
        *static_cast<SceneConstants*>(worldMapped.pData) = worldConstants;
        m_context->Unmap(m_sceneConstants.Get(), 0);
        constexpr UINT worldStride = sizeof(Vertex);
        constexpr UINT worldOffset = 0;
        ID3D11Buffer* terrainVertex = m_terrainVertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &terrainVertex, &worldStride, &worldOffset);
        m_context->IASetIndexBuffer(m_terrainIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        m_context->DrawIndexed(m_terrainIndexCount, 0, 0);
        ID3D11Buffer* forestVertex = m_forestVertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &forestVertex, &worldStride, &worldOffset);
        m_context->IASetIndexBuffer(m_forestIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        m_context->DrawIndexed(m_forestIndexCount, 0, 0);
        ID3D11Buffer* cubeVertex = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &cubeVertex, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    } else {
        LogError("Could not update constants for terrain and forest rendering.");
    }

    // Render the imported house with its MTL diffuse texture.
    const Mat4 houseWorld = Mat4::Translation({0.0F, -1.0F, 0.0F}) * Mat4::Scale({0.8F, 0.8F, 0.8F});
    const SceneConstants houseConstants{projection * view * houseWorld, houseWorld, 1U, {0.0F, 0.0F, 0.0F},
        {cameraX, cameraY, cameraZ}, 0.018F, {-0.45F, -0.8F, 0.55F}, 5.0F,
        {model.roughness, model.metallic, 0.0F, 0.0F},
        {model.baseColor.x, model.baseColor.y, model.baseColor.z, 1.0F}};
    D3D11_MAPPED_SUBRESOURCE houseMapped{};
    if (SUCCEEDED(m_context->Map(m_sceneConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &houseMapped))) {
        *static_cast<SceneConstants*>(houseMapped.pData) = houseConstants;
        m_context->Unmap(m_sceneConstants.Get(), 0);
        constexpr UINT modelStride = sizeof(Vertex);
        constexpr UINT modelOffset = 0;
        ID3D11Buffer* modelVertexBuffer = m_modelVertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &modelVertexBuffer, &modelStride, &modelOffset);
        m_context->IASetIndexBuffer(m_modelIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        ID3D11ShaderResourceView* textureView = m_diffuseTexture.Get();
        ID3D11SamplerState* sampler = m_textureSampler.Get();
        m_context->PSSetShaderResources(0, 1, &textureView);
        m_context->PSSetSamplers(0, 1, &sampler);
        m_context->DrawIndexed(m_modelIndexCount, 0, 0);
        ID3D11ShaderResourceView* noTexture = nullptr;
        m_context->PSSetShaderResources(0, 1, &noTexture);
        ID3D11Buffer* cubeVertexBuffer = m_vertexBuffer.Get();
        m_context->IASetVertexBuffers(0, 1, &cubeVertexBuffer, &stride, &offset);
        m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    } else {
        LogError("Could not update constants for the imported model.");
    }

    const auto propA = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, 0.55F + elapsedSeconds * 0.35F);
    const auto propB = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, -0.8F);
    const auto propC = Quaternion::FromAxisAngle({0.3F, 1.0F, 0.0F}, 0.35F);
    drawCube(Mat4::Translation({-3.0F, -0.4F, 1.5F}) * Mat4::Rotation(propA.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({2.4F, -0.4F, 2.6F}) * Mat4::Rotation(propB.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({3.4F, -0.4F, -2.4F}) * Mat4::Rotation(propC.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({-2.8F, -0.4F, -3.0F}) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    (void)m_swapChain->Present(1, 0);
}
} // namespace vanta
