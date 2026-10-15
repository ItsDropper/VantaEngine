#include "vanta/renderer/D3D12Renderer.hpp"

#include "vanta/core/Log.hpp"
#include "vanta/renderer/WicTextureLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vanta
{
namespace
{
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

std::vector<std::uint8_t> ResampleTexture(const renderer::CpuTexture& source, UINT width, UINT height, bool srgb)
{
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4U);
    const auto toLinear = [](float c) {
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    const auto toSrgb = [](float c) {
        c = std::clamp(c, 0.0F, 1.0F);
        return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
    };
    for (UINT y = 0; y < height; ++y) {
        const float sy = std::clamp((static_cast<float>(y) + 0.5F) * source.height / height - 0.5F,
            0.0F, static_cast<float>(source.height - 1U));
        const UINT y0 = static_cast<UINT>(sy);
        const UINT y1 = std::min(source.height - 1U, y0 + 1U);
        const float fy = sy - static_cast<float>(y0);
        for (UINT x = 0; x < width; ++x) {
            const float sx = std::clamp((static_cast<float>(x) + 0.5F) * source.width / width - 0.5F,
                0.0F, static_cast<float>(source.width - 1U));
            const UINT x0 = static_cast<UINT>(sx);
            const UINT x1 = std::min(source.width - 1U, x0 + 1U);
            const float fx = sx - static_cast<float>(x0);
            for (UINT channel = 0; channel < 4; ++channel) {
                auto sample = [&](UINT px, UINT py) {
                    float value = static_cast<float>(source.rgba[(static_cast<std::size_t>(py) * source.width + px) * 4U + channel]) / 255.0F;
                    return srgb && channel < 3U ? toLinear(value) : value;
                };
                const float top = sample(x0, y0) * (1.0F - fx) + sample(x1, y0) * fx;
                const float bottom = sample(x0, y1) * (1.0F - fx) + sample(x1, y1) * fx;
                float value = top * (1.0F - fy) + bottom * fy;
                if (srgb && channel < 3U) value = toSrgb(value);
                result[(static_cast<std::size_t>(y) * width + x) * 4U + channel] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0F), 0L, 255L));
            }
        }
    }
    return result;
}

} // namespace

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
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
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

bool D3D12Renderer::CreateShadowResources()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kShadowResolution;
    desc.Height = kShadowResolution;
    desc.DepthOrArraySize = 3;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0F;
    clear.DepthStencil.Stencil = 0;
    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(m_shadowTexture.GetAddressOf())))) {
        LogError("Direct3D 12: cascaded shadow texture creation failed.");
        return false;
    }
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT cascade = 0; cascade < 3; ++cascade) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.MipSlice = 0;
        dsv.Texture2DArray.FirstArraySlice = cascade;
        dsv.Texture2DArray.ArraySize = 1;
        m_shadowDsvHandles[cascade] = heapStart;
        m_shadowDsvHandles[cascade].ptr += static_cast<SIZE_T>(cascade + 1) * m_dsvDescriptorSize;
        m_device->CreateDepthStencilView(m_shadowTexture.Get(), &dsv, m_shadowDsvHandles[cascade]);
    }
    return true;
}

bool D3D12Renderer::CreateHdrTarget()
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
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear.Color[0] = 0.18F; clear.Color[1] = 0.28F; clear.Color[2] = 0.40F; clear.Color[3] = 1.0F;
    if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(m_hdrTexture.GetAddressOf())))) {
        LogError("Direct3D 12: HDR scene target creation failed.");
        return false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kFrameCount) * m_rtvDescriptorSize;
    m_device->CreateRenderTargetView(m_hdrTexture.Get(), nullptr, rtv);

    D3D12_DESCRIPTOR_HEAP_DESC srvDescription{};
    srvDescription.NumDescriptors = 12;
    srvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&srvDescription, IID_PPV_ARGS(m_srvHeap.GetAddressOf())))) {
        LogError("Direct3D 12: post-process SRV heap creation failed.");
        return false;
    }
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrv{};
    hdrSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    hdrSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    hdrSrv.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_hdrTexture.Get(), &hdrSrv, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
    depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE depthHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    depthHandle.ptr += m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_depthTexture.Get(), &depthSrv, depthHandle);
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv{};
    shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrv.Texture2DArray.MipLevels = 1;
    shadowSrv.Texture2DArray.ArraySize = 3;
    D3D12_CPU_DESCRIPTOR_HANDLE shadowHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    shadowHandle.ptr += static_cast<SIZE_T>(2) * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_shadowTexture.Get(), &shadowSrv, shadowHandle);
    return true;
}

bool D3D12Renderer::CreateTemporalTargets()
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC colorDesc{};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = m_width;
    colorDesc.Height = m_height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorClear.Color[0] = colorClear.Color[1] = colorClear.Color[2] = 0.0F;
    colorClear.Color[3] = 1.0F;
    D3D12_RESOURCE_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_R32_FLOAT;
    depthClear.Color[0] = 1.0F;
    for (UINT i = 0; i < 2; ++i) {
        if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &colorDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear,
                IID_PPV_ARGS(m_historyColorTextures[i].GetAddressOf()))) ||
            FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &depthClear,
                IID_PPV_ARGS(m_historyDepthTextures[i].GetAddressOf())))) {
            LogError("Direct3D 12: temporal history texture creation failed.");
            return false;
        }
        m_historyColorRtvs[i] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_historyColorRtvs[i].ptr += static_cast<SIZE_T>(kFrameCount + 1 + i) * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(m_historyColorTextures[i].Get(), nullptr, m_historyColorRtvs[i]);
        m_historyDepthRtvs[i] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_historyDepthRtvs[i].ptr += static_cast<SIZE_T>(kFrameCount + 3 + i) * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(m_historyDepthTextures[i].Get(), nullptr, m_historyDepthRtvs[i]);

        D3D12_SHADER_RESOURCE_VIEW_DESC colorSrv{};
        colorSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        colorSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        colorSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        colorSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE colorHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        colorHandle.ptr += static_cast<SIZE_T>(3 + i) * m_srvDescriptorSize;
        m_device->CreateShaderResourceView(m_historyColorTextures[i].Get(), &colorSrv, colorHandle);
        D3D12_SHADER_RESOURCE_VIEW_DESC historyDepthSrv{};
        historyDepthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        historyDepthSrv.Format = DXGI_FORMAT_R32_FLOAT;
        historyDepthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        historyDepthSrv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE historyDepthHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        historyDepthHandle.ptr += static_cast<SIZE_T>(5 + i) * m_srvDescriptorSize;
        m_device->CreateShaderResourceView(m_historyDepthTextures[i].Get(), &historyDepthSrv, historyDepthHandle);
    }
    for (UINT slot = 0; slot < m_modelMaterialTextures.size(); ++slot) {
        if (!m_modelMaterialTextures[slot] || m_modelMaterialMipLevels[slot] == 0U) continue;
        D3D12_SHADER_RESOURCE_VIEW_DESC materialSrv{};
        materialSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        materialSrv.Format = slot == 0U ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        materialSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        materialSrv.Texture2DArray.MipLevels = m_modelMaterialMipLevels[slot];
        materialSrv.Texture2DArray.ArraySize = m_modelMaterialLayerCount;
        materialSrv.Texture2DArray.FirstArraySlice = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE materialHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        materialHandle.ptr += static_cast<SIZE_T>(7U + slot) * m_srvDescriptorSize;
        m_device->CreateShaderResourceView(m_modelMaterialTextures[slot].Get(), &materialSrv, materialHandle);
    }
    m_historyWriteIndex = 0;
    m_historyValid = false;
    m_previousViewProjection = Mat4::Identity();
    return true;
}

bool D3D12Renderer::CreateModelTextureArray(UINT width, UINT height, UINT layers,
                                                    const std::vector<std::uint8_t>& rgbaLayers,
                                                    UINT descriptorIndex, bool srgb)
{
    if (width == 0 || height == 0 || layers == 0 ||
        layers > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION ||
        descriptorIndex < 7U || descriptorIndex >= 12U) return false;
    const std::size_t layerBytes = static_cast<std::size_t>(width) * height * 4U;
    if (layerBytes == 0U || rgbaLayers.size() < layerBytes * layers) return false;

    UINT mipLevels = 1;
    for (UINT w = width, h = height; w > 1U || h > 1U; ++mipLevels) {
        w = std::max(1U, w / 2U);
        h = std::max(1U, h / 2U);
    }
    const std::size_t subresourceCount = static_cast<std::size_t>(layers) * mipLevels;
    std::vector<std::vector<std::uint8_t>> subresources(subresourceCount);
    const auto toLinear = [](float c) {
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    const auto toSrgb = [](float c) {
        c = std::clamp(c, 0.0F, 1.0F);
        return c <= 0.0031308F ? c * 12.92F : 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
    };
    for (UINT layer = 0; layer < layers; ++layer) {
        const std::size_t baseOffset = static_cast<std::size_t>(layer) * layerBytes;
        subresources[static_cast<std::size_t>(layer) * mipLevels].assign(
            rgbaLayers.begin() + static_cast<std::ptrdiff_t>(baseOffset),
            rgbaLayers.begin() + static_cast<std::ptrdiff_t>(baseOffset + layerBytes));
        UINT previousWidth = width;
        UINT previousHeight = height;
        for (UINT mip = 1; mip < mipLevels; ++mip) {
            const UINT nextWidth = std::max(1U, previousWidth / 2U);
            const UINT nextHeight = std::max(1U, previousHeight / 2U);
            const auto& previous = subresources[static_cast<std::size_t>(layer) * mipLevels + mip - 1U];
            std::vector<std::uint8_t> next(static_cast<std::size_t>(nextWidth) * nextHeight * 4U);
            for (UINT y = 0; y < nextHeight; ++y) {
                for (UINT x = 0; x < nextWidth; ++x) {
                    for (UINT channel = 0; channel < 4; ++channel) {
                        float sum = 0.0F;
                        for (UINT oy = 0; oy < 2; ++oy) {
                            for (UINT ox = 0; ox < 2; ++ox) {
                                const UINT sx = std::min(previousWidth - 1U, x * 2U + ox);
                                const UINT sy = std::min(previousHeight - 1U, y * 2U + oy);
                                float value = static_cast<float>(previous[
                                    (static_cast<std::size_t>(sy) * previousWidth + sx) * 4U + channel]) / 255.0F;
                                if (srgb && channel < 3U) value = toLinear(value);
                                sum += value;
                            }
                        }
                        float value = sum * 0.25F;
                        if (srgb && channel < 3U) value = toSrgb(value);
                        next[(static_cast<std::size_t>(y) * nextWidth + x) * 4U + channel] =
                            static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0F), 0L, 255L));
                    }
                }
            }
            subresources[static_cast<std::size_t>(layer) * mipLevels + mip] = std::move(next);
            previousWidth = nextWidth;
            previousHeight = nextHeight;
        }
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = static_cast<UINT16>(layers);
    textureDesc.MipLevels = static_cast<UINT16>(mipLevels);
    textureDesc.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(m_modelMaterialTextures[descriptorIndex - 7U].GetAddressOf())))) {
        LogError("Direct3D 12: imported material texture-array allocation failed.");
        return false;
    }

    const UINT totalSubresources = static_cast<UINT>(subresourceCount);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(totalSubresources);
    std::vector<UINT> rowCounts(totalSubresources);
    std::vector<UINT64> rowSizes(totalSubresources);
    UINT64 uploadBytes = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, totalSubresources, 0,
        footprints.data(), rowCounts.data(), rowSizes.data(), &uploadBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (!CreateUploadBuffer(m_device.Get(), uploadBytes, upload)) {
        LogError("Direct3D 12: material texture-array staging allocation failed.");
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped))) return false;
    for (UINT layer = 0; layer < layers; ++layer) {
        UINT mipWidth = width;
        UINT mipHeight = height;
        for (UINT mip = 0; mip < mipLevels; ++mip) {
            const UINT subresource = layer * mipLevels + mip;
            if (rowCounts[subresource] != mipHeight ||
                rowSizes[subresource] < static_cast<UINT64>(mipWidth) * 4U) {
                upload->Unmap(0, nullptr);
                LogError("Direct3D 12: material texture-array mip footprint is invalid.");
                return false;
            }
            auto* destination = static_cast<std::uint8_t*>(mapped) + footprints[subresource].Offset;
            const auto& pixels = subresources[subresource];
            for (UINT row = 0; row < mipHeight; ++row)
                std::memcpy(destination + static_cast<std::size_t>(row) * footprints[subresource].Footprint.RowPitch,
                    pixels.data() + static_cast<std::size_t>(row) * mipWidth * 4U,
                    static_cast<std::size_t>(mipWidth) * 4U);
            mipWidth = std::max(1U, mipWidth / 2U);
            mipHeight = std::max(1U, mipHeight / 2U);
        }
    }
    upload->Unmap(0, nullptr);

    if (FAILED(m_commandAllocators[0]->Reset()) ||
        FAILED(m_commandList->Reset(m_commandAllocators[0].Get(), nullptr))) {
        LogError("Direct3D 12: material texture-array upload command reset failed.");
        return false;
    }
    for (UINT layer = 0; layer < layers; ++layer) {
        for (UINT mip = 0; mip < mipLevels; ++mip) {
            const UINT subresource = layer * mipLevels + mip;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = m_modelMaterialTextures[descriptorIndex - 7U].Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprints[subresource];
            m_commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
    }
    D3D12_RESOURCE_BARRIER toShaderResource{};
    toShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource.Transition.pResource = m_modelMaterialTextures[descriptorIndex - 7U].Get();
    toShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &toShaderResource);
    if (FAILED(m_commandList->Close())) return false;
    ID3D12CommandList* commandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, commandLists);
    if (!WaitForGpu()) {
        LogError("Direct3D 12: waiting for material texture-array upload failed.");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = textureDesc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = mipLevels;
    srv.Texture2DArray.ArraySize = layers;
    srv.Texture2DArray.FirstArraySlice = 0;
    m_modelMaterialMipLevels[descriptorIndex - 7U] = mipLevels;
    m_modelMaterialLayerCount = layers;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorIndex) * m_srvDescriptorSize;
    m_device->CreateShaderResourceView(m_modelMaterialTextures[descriptorIndex - 7U].Get(), &srv, handle);
    return true;
}

} // namespace vanta
