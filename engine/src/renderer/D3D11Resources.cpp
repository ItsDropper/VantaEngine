#include "vanta/renderer/D3D11Renderer.hpp"

#include "vanta/core/Log.hpp"

#include <cstddef>

namespace vanta
{
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
        LogError("Could not create the swap-chain render-target view.");
        return false;
    }

    D3D11_TEXTURE2D_DESC hdrDescription{};
    hdrDescription.Width = m_width;
    hdrDescription.Height = m_height;
    hdrDescription.MipLevels = 1;
    hdrDescription.ArraySize = 1;
    hdrDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrDescription.SampleDesc.Count = 1;
    hdrDescription.Usage = D3D11_USAGE_DEFAULT;
    hdrDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&hdrDescription, nullptr, m_hdrTexture.GetAddressOf())) ||
        FAILED(m_device->CreateRenderTargetView(m_hdrTexture.Get(), nullptr, m_hdrRenderTarget.GetAddressOf())) ||
        FAILED(m_device->CreateShaderResourceView(m_hdrTexture.Get(), nullptr, m_hdrShaderResource.GetAddressOf())))
    {
        LogError("Could not create the HDR scene target.");
        return false;
    }
    return true;
}

bool D3D11Renderer::CreateTemporalTargets()
{
    for (std::size_t i = 0; i < m_historyTextures.size(); ++i) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = m_width;
        description.Height = m_height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_device->CreateTexture2D(&description, nullptr, m_historyTextures[i].GetAddressOf())) ||
            FAILED(m_device->CreateRenderTargetView(m_historyTextures[i].Get(), nullptr, m_historyRenderTargets[i].GetAddressOf())) ||
            FAILED(m_device->CreateShaderResourceView(m_historyTextures[i].Get(), nullptr, m_historyShaderResources[i].GetAddressOf()))) {
            LogError("Could not create temporal history targets.");
            return false;
        }
    }
    m_historyValid = false;
    m_historyWriteIndex = 0;
    m_frameIndex = 0;
    return true;
}

bool D3D11Renderer::CreateShadowResources()
{
    constexpr UINT shadowSize = 2048;
    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = shadowSize;
    textureDescription.Height = shadowSize;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 3;
    textureDescription.Format = DXGI_FORMAT_R32_TYPELESS;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&textureDescription, nullptr, m_shadowTexture.GetAddressOf()))) {
        LogError("Could not create the shadow-map texture.");
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDescription{};
    depthViewDescription.Format = DXGI_FORMAT_D32_FLOAT;
    depthViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    depthViewDescription.Texture2DArray.ArraySize = 1;
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        depthViewDescription.Texture2DArray.FirstArraySlice = cascade;
        if (FAILED(m_device->CreateDepthStencilView(m_shadowTexture.Get(), &depthViewDescription,
                m_shadowDepthStencil[cascade].GetAddressOf()))) {
            LogError("Could not create a cascaded shadow-map depth view.");
            return false;
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC resourceDescription{};
    resourceDescription.Format = DXGI_FORMAT_R32_FLOAT;
    resourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    resourceDescription.Texture2DArray.MipLevels = 1;
    resourceDescription.Texture2DArray.ArraySize = 3;
    if (FAILED(m_device->CreateShaderResourceView(m_shadowTexture.Get(), &resourceDescription, m_shadowShaderResource.GetAddressOf()))) {
        LogError("Could not create the shadow-map shader view.");
        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    samplerDescription.BorderColor[0] = 1.0F;
    samplerDescription.BorderColor[1] = 1.0F;
    samplerDescription.BorderColor[2] = 1.0F;
    samplerDescription.BorderColor[3] = 1.0F;
    samplerDescription.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    samplerDescription.MinLOD = 0.0F;
    samplerDescription.MaxLOD = 0.0F;
    if (FAILED(m_device->CreateSamplerState(&samplerDescription, m_shadowSampler.GetAddressOf()))) {
        LogError("Could not create the shadow comparison sampler.");
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDescription{};
    rasterDescription.FillMode = D3D11_FILL_SOLID;
    rasterDescription.CullMode = D3D11_CULL_NONE;
    rasterDescription.DepthClipEnable = TRUE;
    rasterDescription.DepthBias = 120;
    rasterDescription.SlopeScaledDepthBias = 1.5F;
    rasterDescription.DepthBiasClamp = 0.01F;
    if (FAILED(m_device->CreateRasterizerState(&rasterDescription, m_shadowRasterizerState.GetAddressOf()))) {
        LogError("Could not create the shadow rasterizer state.");
        return false;
    }
    return true;
}

} // namespace vanta
