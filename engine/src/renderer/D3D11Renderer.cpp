#include "vanta/renderer/D3D11Renderer.hpp"

#include "vanta/core/Log.hpp"

#include <d3dcompiler.h>
#include <iterator>

namespace vanta
{
namespace
{
struct Vertex
{
    float position[3];
    float color[4];
};

constexpr Vertex kTriangleVertices[] = {
    {{0.0F, 0.62F, 0.0F}, {0.20F, 0.75F, 1.0F, 1.0F}},
    {{0.58F, -0.42F, 0.0F}, {0.35F, 0.95F, 0.65F, 1.0F}},
    {{-0.58F, -0.42F, 0.0F}, {0.95F, 0.55F, 0.25F, 1.0F}},
};

constexpr char kShaderSource[] = R"(
struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return float4(input.color.rgb, 1.0);
}
)";
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
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    const HRESULT deviceResult = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        static_cast<UINT>(std::size(featureLevels)),
        D3D11_SDK_VERSION,
        &swapChainDescription,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        &selectedFeatureLevel,
        m_context.GetAddressOf());

    if (FAILED(deviceResult))
    {
        LogError("Direct3D 11 device/swap-chain creation failed.");
        return false;
    }

    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);

    if (!CreateRenderTarget() || !CreatePipeline())
    {
        return false;
    }

    LogInfo("Direct3D 11 renderer initialized.");
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

    if (FAILED(m_device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, m_vertexShader.GetAddressOf())) ||
        FAILED(m_device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
            nullptr, m_pixelShader.GetAddressOf())))
    {
        LogError("Could not create the Direct3D shader objects.");
        return false;
    }

    constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    if (FAILED(m_device->CreateInputLayout(
            inputElements, static_cast<UINT>(std::size(inputElements)),
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            m_inputLayout.GetAddressOf())))
    {
        LogError("Could not create the vertex input layout.");
        return false;
    }

    D3D11_BUFFER_DESC bufferDescription{};
    bufferDescription.ByteWidth = sizeof(kTriangleVertices);
    bufferDescription.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = kTriangleVertices;

    if (FAILED(m_device->CreateBuffer(
            &bufferDescription, &initialData, m_vertexBuffer.GetAddressOf())))
    {
        LogError("Could not create the vertex buffer.");
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

    const HRESULT resizeResult = m_swapChain->ResizeBuffers(
        0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resizeResult))
    {
        LogError("Swap-chain resize failed.");
        return false;
    }

    m_width = static_cast<UINT>(width);
    m_height = static_cast<UINT>(height);
    return CreateRenderTarget();
}

void D3D11Renderer::Render()
{
    constexpr float clearColor[] = {0.035F, 0.045F, 0.075F, 1.0F};
    m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), nullptr);
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    m_context->RSSetViewports(1, &viewport);

    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    ID3D11Buffer* vertexBuffer = m_vertexBuffer.Get();
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->Draw(static_cast<UINT>(std::size(kTriangleVertices)), 0);

    (void)m_swapChain->Present(1, 0);
}
} // namespace vanta
