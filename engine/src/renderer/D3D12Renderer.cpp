#include "vanta/renderer/D3D12Renderer.hpp"

#include "vanta/core/Log.hpp"
#include "vanta/renderer/ProceduralWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <d3dcompiler.h>

namespace vanta
{
namespace
{
using math::Mat4;
using math::Vec3;

struct SceneConstants
{
    Mat4 viewProjection;
    float cameraPosition[4];
    float lighting[4];
};
static_assert(sizeof(SceneConstants) == 96);

constexpr char kSceneShader[] = R"(
cbuffer SceneConstants : register(b0)
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lighting;
};
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; float2 uv : TEXCOORD; };
struct PSInput { float4 position : SV_POSITION; float3 worldPosition : TEXCOORD0; float3 normal : TEXCOORD1; float4 color : COLOR; };
PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(viewProjection, float4(input.position, 1.0));
    o.worldPosition = input.position;
    o.normal = normalize(input.normal);
    o.color = input.color;
    return o;
}
float3 ACES(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float3 l = normalize(float3(-0.42, 0.82, -0.36));
    float ndl = saturate(dot(n, l));
    float wrapLight = saturate((dot(n, l) + 0.32) / 1.32);
    float3 albedo = input.color.rgb;
    float3 direct = albedo * (0.22 + wrapLight * 1.35);
    float3 skyFill = lerp(float3(0.035, 0.055, 0.075), float3(0.32, 0.43, 0.53), saturate(n.y * 0.5 + 0.5));
    float3 color = direct + skyFill * albedo * 0.52;
    float distanceToCamera = distance(cameraPosition.xyz, input.worldPosition);
    float fog = saturate(1.0 - exp(-max(distanceToCamera - 18.0, 0.0) * 0.018));
    float3 fogColor = float3(0.34, 0.43, 0.52);
    color = lerp(color, fogColor, fog);
    color = ACES(color * 1.12);
    color = pow(max(color, 0.0), 1.0 / 2.2);
    return float4(color, input.color.a);
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
    if (m_mappedConstants != nullptr && m_constantBuffer) {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedConstants = nullptr;
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
            wchar_t adapterName[128]{};
            WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, nullptr, 0, nullptr, nullptr);
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
    rtvHeapDescription.NumDescriptors = kFrameCount;
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(m_rtvHeap.GetAddressOf())))) {
        LogError("Direct3D 12: RTV descriptor heap creation failed.");
        return false;
    }
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
    dsvHeapDescription.NumDescriptors = 1;
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

    if (!CreateRenderTargets() || !CreateDepthTarget() || !CreateSceneGeometry() || !CreatePipeline()) return false;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    LogInfo("Direct3D 12 scene renderer initialized: procedural valley, depth testing, directional lighting, atmospheric fog.");
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
    desc.Format = DXGI_FORMAT_D32_FLOAT;
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

bool D3D12Renderer::CreateSceneGeometry()
{
    std::vector<renderer::WorldVertex> vertices;
    std::vector<unsigned int> indices;
    AppendTerrain(vertices, indices);
    renderer::BuildDetailedWorld(vertices, indices);
    if (vertices.empty() || indices.empty() || indices.size() > UINT_MAX) {
        LogError("Direct3D 12: procedural scene mesh is empty or too large.");
        return false;
    }
    const UINT64 vertexBytes = static_cast<UINT64>(vertices.size()) * sizeof(renderer::WorldVertex);
    const UINT64 indexBytes = static_cast<UINT64>(indices.size()) * sizeof(unsigned int);
    if (!CreateUploadBuffer(m_device.Get(), vertexBytes, m_vertexBuffer) ||
        !CreateUploadBuffer(m_device.Get(), indexBytes, m_indexBuffer)) {
        LogError("Direct3D 12: scene geometry buffer allocation failed.");
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(m_vertexBuffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexBytes));
    m_vertexBuffer->Unmap(0, nullptr);
    if (FAILED(m_indexBuffer->Map(0, nullptr, &mapped))) return false;
    std::memcpy(mapped, indices.data(), static_cast<size_t>(indexBytes));
    m_indexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
    m_vertexBufferView.StrideInBytes = sizeof(renderer::WorldVertex);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = static_cast<UINT>(indexBytes);
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexCount = static_cast<UINT>(indices.size());
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

    D3D12_ROOT_PARAMETER rootParameter{};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.ShaderRegister = 0;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 1;
    rootDescription.pParameters = &rootParameter;
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
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
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
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (!CreateUploadBuffer(m_device.Get(), 256, m_constantBuffer)) {
        LogError("Direct3D 12: constant buffer allocation failed.");
        return false;
    }
    D3D12_RANGE noRead{0, 0};
    if (FAILED(m_constantBuffer->Map(0, &noRead, &m_mappedConstants))) return false;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_pipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: graphics pipeline state creation failed.");
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
    if (!CreateRenderTargets() || !CreateDepthTarget()) return false;
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
    constants.cameraPosition[0] = cameraX;
    constants.cameraPosition[1] = cameraY;
    constants.cameraPosition[2] = cameraZ;
    constants.cameraPosition[3] = 1.0F;
    constants.lighting[0] = -0.42F;
    constants.lighting[1] = 0.82F;
    constants.lighting[2] = -0.36F;
    constants.lighting[3] = elapsedSeconds;
    std::memcpy(m_mappedConstants, &constants, sizeof(constants));

    if (FAILED(m_commandAllocators[m_frameIndex]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()))) {
        LogError("Direct3D 12: command recording reset failed.");
        m_failed = true;
        return;
    }

    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    constexpr float skyColor[] = {0.22F, 0.34F, 0.46F, 1.0F};
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_commandList->ClearRenderTargetView(rtv, skyColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
    D3D12_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(m_width), static_cast<float>(m_height), 0.0F, 1.0F};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);
    m_commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
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
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
} // namespace vanta
