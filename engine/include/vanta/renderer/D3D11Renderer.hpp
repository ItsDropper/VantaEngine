#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace vanta
{
class D3D11Renderer final
{
public:
    D3D11Renderer() = default;
    ~D3D11Renderer() = default;

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    [[nodiscard]] bool Initialize(HWND window, int width, int height);
    [[nodiscard]] bool Resize(int width, int height);
    void Render();

private:
    [[nodiscard]] bool CreateRenderTarget();
    [[nodiscard]] bool CreatePipeline();

    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;

    UINT m_width = 0;
    UINT m_height = 0;
};
} // namespace vanta
