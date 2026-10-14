#pragma once

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
    void Render();
private:
    static constexpr UINT kFrameCount = 2;
    [[nodiscard]] bool CreateRenderTargets();
    [[nodiscard]] bool WaitForGpu();
    [[nodiscard]] bool WaitForFrame(UINT frameIndex);

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> m_commandAllocators;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> m_renderTargets;
    std::array<UINT64, kFrameCount> m_fenceValues{};
    HANDLE m_fenceEvent = nullptr;
    HWND m_window = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_frameIndex = 0;
    UINT64 m_nextFenceValue = 1;
    bool m_failed = false;
};
} // namespace vanta
