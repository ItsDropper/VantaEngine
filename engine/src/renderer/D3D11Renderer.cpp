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
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(worldViewProjection, float4(input.position, 1.0));
    output.normal = normalize(mul((float3x3)world, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(float3(-0.45, 0.8, -0.55));
    float diffuse = saturate(dot(normal, lightDirection));
    float lighting = 0.22 + diffuse * 0.78;
    float4 albedo = useTexture != 0 ? diffuseTexture.Sample(diffuseSampler, input.uv) : input.color;
    float3 color = albedo.rgb * lighting;
    return float4(color, albedo.a);
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
    CpuModel model;
    std::string importError;
    if (!LoadWavefrontObj(modelPath, model, importError)) {
        LogError(importError.c_str());
        return false;
    }
    std::vector<Vertex> modelVertices;
    modelVertices.reserve(model.vertices.size());
    for (const ModelVertex& source : model.vertices) {
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

    CpuTexture image;
    if (model.diffuseTexture.empty() || !LoadWicTexture(model.diffuseTexture, image)) {
        LogError("Imported model has no readable diffuse texture (check its MTL map_Kd entry).");
        return false;
    }
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = image.width;
    textureDescription.Height = image.height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    const auto drawCube = [&](const Mat4& worldTransform)
    {
        const SceneConstants constants{projection * view * worldTransform, worldTransform, 0U, {0.0F, 0.0F, 0.0F}};
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

    // A broad ground slab keeps the existing depth/lighting test environment.
    drawCube(Mat4::Translation({0.0F, -1.15F, 0.0F}) * Mat4::Scale({12.0F, 0.15F, 12.0F}));

    // Render the imported house with its MTL diffuse texture.
    const Mat4 houseWorld = Mat4::Translation({0.0F, -1.0F, 0.0F}) * Mat4::Scale({0.8F, 0.8F, 0.8F});
    const SceneConstants houseConstants{projection * view * houseWorld, houseWorld, 1U, {0.0F, 0.0F, 0.0F}};
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

    const auto propA = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, 0.55F);
    const auto propB = Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F}, -0.8F);
    const auto propC = Quaternion::FromAxisAngle({0.3F, 1.0F, 0.0F}, 0.35F);
    drawCube(Mat4::Translation({-3.0F, -0.4F, 1.5F}) * Mat4::Rotation(propA.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({2.4F, -0.4F, 2.6F}) * Mat4::Rotation(propB.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({3.4F, -0.4F, -2.4F}) * Mat4::Rotation(propC.value_or(Quaternion::Identity())) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    drawCube(Mat4::Translation({-2.8F, -0.4F, -3.0F}) * Mat4::Scale({0.6F, 0.6F, 0.6F}));
    (void)m_swapChain->Present(1, 0);
}
} // namespace vanta
