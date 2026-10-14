#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

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
    void Render(float elapsedSeconds, float cameraX, float cameraY, float cameraZ, float cameraYaw, float cameraPitch);

private:
    [[nodiscard]] bool CreateRenderTarget();
    [[nodiscard]] bool CreateDepthTarget();
    [[nodiscard]] bool CreateShadowResources();
    [[nodiscard]] bool CreatePipeline();

    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_hdrTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_hdrRenderTarget;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_hdrShaderResource;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_postProcessPixelShader;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_shadowTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_shadowDepthStencil;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shadowShaderResource;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_shadowSampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_shadowRasterizerState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_shadowVertexShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_shadowInputLayout;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_skyVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_skyPixelShader;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_skyDepthState;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_modelVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_modelIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_terrainVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_terrainIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_forestVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_forestIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_diffuseTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_normalTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_roughnessTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_metallicTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ambientOcclusionTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_grassAlbedoTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_rockAlbedoTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_woodAlbedoTexture;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_textureSampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_sceneConstants;
    UINT m_modelIndexCount = 0;
    UINT m_terrainIndexCount = 0;
    UINT m_forestIndexCount = 0;
    float m_modelRoughness = 0.72F;
    float m_modelMetallic = 0.0F;
    float m_modelBaseColor[3] = {1.0F, 1.0F, 1.0F};

    UINT m_width = 0;
    UINT m_height = 0;
};
} // namespace vanta
