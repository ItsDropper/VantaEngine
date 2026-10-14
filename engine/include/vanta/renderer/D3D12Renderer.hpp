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
    [[nodiscard]] bool CreateRenderTargets();
    [[nodiscard]] bool CreateDepthTarget();
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
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> m_commandAllocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> m_renderTargets;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    std::array<UINT64, kFrameCount> m_fenceValues{};
    HANDLE m_fenceEvent = nullptr;
    void* m_mappedConstants = nullptr;
    HWND m_window = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_frameIndex = 0;
    UINT64 m_nextFenceValue = 1;
    UINT m_indexCount = 0;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView{};
    bool m_failed = false;
};
} // namespace vanta
