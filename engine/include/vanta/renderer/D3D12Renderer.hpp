#pragma once

#include "vanta/math/Math.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace vanta
{
class D3D12Renderer final
{
public:
    D3D12Renderer() = default;
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    [[nodiscard]] bool Initialize(HWND window, int width, int height);
    [[nodiscard]] bool Resize(int width, int height);
    void Render(float elapsedSeconds, float cameraX, float cameraY, float cameraZ,
                float cameraYaw, float cameraPitch);
private:
    static constexpr UINT kFrameCount = 2;
    struct DrawChunk
    {
        std::array<UINT, 3> startIndex{};
        std::array<UINT, 3> indexCount{};
        float centerX = 0.0F;
        float centerY = 0.0F;
        float centerZ = 0.0F;
        float radius = 20.0F;
    };
    [[nodiscard]] bool CreateRenderTargets();
    [[nodiscard]] bool CreateDepthTarget();
    [[nodiscard]] bool CreateHdrTarget();
    [[nodiscard]] bool CreateShadowResources();
    [[nodiscard]] bool CreateShadowPipeline();
    [[nodiscard]] bool CreateTemporalTargets();
    [[nodiscard]] bool CreateTemporalPipeline();
    [[nodiscard]] bool CreateModelAlbedoTexture(UINT width, UINT height, const std::uint8_t* rgba);
    [[nodiscard]] bool CreatePostProcessPipeline();
    [[nodiscard]] bool CreateSceneGeometry();
    [[nodiscard]] bool CreatePipeline();
    [[nodiscard]] bool WaitForGpu();
    [[nodiscard]] bool WaitForFrame(UINT frameIndex);

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> m_commandAllocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> m_renderTargets;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_historyColorTextures;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_historyDepthTextures;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> m_historyColorRtvs{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> m_historyDepthRtvs{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_hdrTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_modelAlbedoTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> m_constantBuffers;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> m_temporalConstantBuffers;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_postProcessPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_shadowPipelineState;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 2> m_temporalPipelineStates;
    std::array<UINT64, kFrameCount> m_fenceValues{};
    HANDLE m_fenceEvent = nullptr;
    std::array<void*, kFrameCount> m_mappedConstants{};
    std::array<void*, kFrameCount> m_mappedTemporalConstants{};
    HWND m_window = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_dsvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;
    UINT m_frameIndex = 0;
    UINT64 m_nextFenceValue = 1;
    UINT m_indexCount = 0;
    std::vector<DrawChunk> m_drawChunks;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    bool m_failed = false;
};
} // namespace vanta
