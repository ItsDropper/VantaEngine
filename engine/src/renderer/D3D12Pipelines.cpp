#include "vanta/renderer/D3D12Renderer.hpp"

#include "vanta/core/Log.hpp"

#include <d3dcompiler.h>
#include <iterator>
#include <limits>

namespace vanta
{
namespace
{
constexpr char kSceneShader[] = R"(
cbuffer SceneConstants : register(b0)
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lighting;
    float4x4 lightViewProjection[3];
    float4 cascadeSplits;
};
Texture2DArray<float> shadowMap : register(t2);
Texture2DArray modelAlbedoTexture : register(t7);
Texture2DArray modelNormalTexture : register(t8);
Texture2DArray modelRoughnessTexture : register(t9);
Texture2DArray modelMetallicTexture : register(t10);
Texture2DArray modelOcclusionTexture : register(t11);
SamplerState linearClamp : register(s0);
SamplerState linearWrap : register(s1);
struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; float2 uv : TEXCOORD0; float4 material : TEXCOORD1; };
struct PSInput { float4 position : SV_POSITION; float3 worldPosition : TEXCOORD0; float3 normal : TEXCOORD1; float4 color : COLOR; float2 uv : TEXCOORD2; float4 material : TEXCOORD3; };
PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(viewProjection, float4(input.position, 1.0));
    o.worldPosition = input.position;
    o.normal = normalize(input.normal);
    o.color = input.color;
    o.uv = input.uv;
    o.material = input.material;
    return o;
}
float DistributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nh = saturate(dot(n, h));
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 0.0001);
}
float GeometrySchlick(float nv, float roughness)
{
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    return nv / max(nv * (1.0 - k) + k, 0.0001);
}
float3 FresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}
float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float TerrainNoise(float2 p)
{
    float n = ValueNoise(p) * 0.57;
    n += ValueNoise(p * 2.03 + 17.0) * 0.28;
    n += ValueNoise(p * 5.11 - 9.0) * 0.15;
    return n;
}
float CalculateShadow(float3 worldPosition, float3 normal, float3 lightDirection)
{
    float cameraDistance = distance(cameraPosition.xyz, worldPosition);
    int cascade = cameraDistance < cascadeSplits.x ? 0 :
                  (cameraDistance < cascadeSplits.y ? 1 : 2);
    float4 shadowClip = mul(lightViewProjection[cascade], float4(worldPosition, 1.0));
    if (shadowClip.w <= 0.0) return 1.0;
    float3 projected = shadowClip.xyz / shadowClip.w;
    float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(uv <= 0.001) || any(uv >= 0.999) || projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;
    float bias = max(0.00018 * (1.0 - saturate(dot(normal, lightDirection))), 0.000045);
    float visibility = 0.0;
    const float2 texel = 1.0 / 2048.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float storedDepth = shadowMap.SampleLevel(linearClamp,
                float3(uv + float2(x, y) * texel, cascade), 0);
            visibility += projected.z - bias <= storedDepth ? 1.0 : 0.0;
        }
    }
    return lerp(0.58, 1.0, visibility / 9.0);
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    uint materialFlags = (uint)(input.material.w + 0.5);
    float3 dp1 = ddx(input.worldPosition);
    float3 dp2 = ddy(input.worldPosition);
    float2 duv1 = ddx(input.uv);
    float2 duv2 = ddy(input.uv);
    if ((materialFlags & 2U) != 0U)
    {
        float3 tangentNormal = modelNormalTexture.Sample(linearWrap, float3(input.uv, input.color.a)).xyz * 2.0 - 1.0;
        float3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
        float3 bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
        n = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + n * tangentNormal.z);
    }
    float3 v = normalize(cameraPosition.xyz - input.worldPosition);
    float3 l = normalize(lighting.xyz);
    float3 h = normalize(v + l);
    float3 albedo = max(input.color.rgb, 0.0);
    if ((materialFlags & 1U) != 0U)
        albedo *= modelAlbedoTexture.Sample(linearWrap, float3(input.uv, input.color.a)).rgb;
    // World-space multi-frequency noise breaks up large flat color regions without
    // relying on tiling textures. A slope/height mask gives the terrain distinct soil,
    // grass and exposed-rock response while leaving trees and buildings mostly intact.
    float2 worldXZ = input.worldPosition.xz;
    float macroNoise = TerrainNoise(worldXZ * 0.115);
    float detailNoise = TerrainNoise(worldXZ * 1.65);
    float fine = detailNoise * 2.0 - 1.0;
    float importedMaterial = saturate(input.material.z);
    float terrainColorMask = (1.0 - importedMaterial) *
        smoothstep(0.055, 0.20, albedo.g - max(albedo.r, albedo.b) * 0.42) *
        (1.0 - smoothstep(0.15, 1.65, input.worldPosition.y));
    float slope = 1.0 - saturate(n.y);
    float rockMask = terrainColorMask * smoothstep(0.28, 0.78, slope + (macroNoise - 0.5) * 0.22);
    float soilMask = terrainColorMask * (1.0 - rockMask) * smoothstep(0.30, 0.67, macroNoise);
    float3 grassAlbedo = albedo * (0.80 + macroNoise * 0.34) * (0.90 + detailNoise * 0.20);
    float3 soilAlbedo = albedo * float3(0.72, 0.59, 0.43) * (0.72 + detailNoise * 0.48);
    float3 rockAlbedo = lerp(albedo * 0.52, float3(0.31, 0.32, 0.30), detailNoise * 0.55);
    float3 terrainAlbedo = lerp(grassAlbedo, soilAlbedo, soilMask * 0.72);
    terrainAlbedo = lerp(terrainAlbedo, rockAlbedo, rockMask * 0.88);
    // Terrain breakup is isolated from authored model textures and their material factors.
    terrainAlbedo *= 0.94 + 0.10 * fine + 0.07 * (macroNoise - 0.5);
    albedo = lerp(albedo, terrainAlbedo, terrainColorMask);
    float roughness = saturate(lerp(0.72 + 0.16 * fine + terrainColorMask * 0.12,
        input.material.x + 0.05 * fine, importedMaterial));
    float metallic = saturate(lerp(0.025, input.material.y, importedMaterial));
    if ((materialFlags & 4U) != 0U)
    {
        float roughnessSample = modelRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r;
        if ((materialFlags & 32U) != 0U)
            roughnessSample = modelRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a)).g;
        roughness = saturate(roughness * roughnessSample);
    }
    if ((materialFlags & 8U) != 0U)
        metallic = saturate(metallic * modelMetallicTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r);
    if ((materialFlags & 32U) != 0U)
        metallic = saturate(metallic * modelRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a)).b);
    float ambientOcclusion = (materialFlags & 16U) != 0U
        ? modelOcclusionTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r : 1.0;
    float ndl = saturate(dot(n, l));
    float ndv = max(saturate(dot(n, v)), 0.001);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 f = FresnelSchlick(saturate(dot(h, v)), f0);
    float d = DistributionGGX(n, h, roughness);
    float g = GeometrySchlick(ndv, roughness) * GeometrySchlick(max(ndl, 0.001), roughness);
    float3 specular = d * g * f / max(4.0 * ndv * max(ndl, 0.001), 0.001);
    float3 kD = (1.0 - f) * (1.0 - metallic);
    float shadow = CalculateShadow(input.worldPosition, n, l);
    float3 daylightColor = lerp(float3(1.9, 0.62, 0.28), float3(4.2, 3.85, 3.45), smoothstep(0.02, 0.48, lighting.y));
    float3 direct = (kD * albedo / 3.14159265 + specular) * daylightColor * lighting.w * ndl * shadow;
    float3 sky = lerp(float3(0.025, 0.032, 0.055), float3(0.20, 0.30, 0.43), saturate(n.y * 0.5 + 0.5));
    float3 ambient = sky * albedo * (1.0 - metallic) * 1.05 * ambientOcclusion;
    float3 reflectedSky = lerp(float3(0.10, 0.095, 0.08), float3(0.30, 0.29, 0.25), saturate(reflect(-v, n).y * 0.5 + 0.5));
    ambient += reflectedSky * f * (1.0 - roughness * 0.7) * 0.45;
    float distanceToCamera = distance(cameraPosition.xyz, input.worldPosition);
    float fog = saturate(1.0 - exp(-max(distanceToCamera - 18.0, 0.0) * 0.018));
    float3 fogColor = lerp(float3(0.025, 0.032, 0.055), float3(0.56, 0.66, 0.76), saturate(lighting.w * 0.85));
    // Preserve material colors. Channel-biased grading tints every surface.
    float3 litColor = max(lerp(direct + ambient, fogColor * 1.05, fog), 0.0);
    return float4(litColor, 1.0);
}
)";

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

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 12;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER rootParameters[3]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2]{};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[0].MipLODBias = 0.0F;
    staticSamplers[0].MaxAnisotropy = 1;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSamplers[0].MinLOD = 0.0F;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].RegisterSpace = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    staticSamplers[1] = staticSamplers[0];
    staticSamplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
    staticSamplers[1].MaxAnisotropy = 16;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[1].ShaderRegister = 1;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 3;
    rootDescription.pParameters = rootParameters;
    rootDescription.NumStaticSamplers = 2;
    rootDescription.pStaticSamplers = staticSamplers;
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
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
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
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    D3D12_RANGE noRead{0, 0};
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (!CreateUploadBuffer(m_device.Get(), 1280, m_constantBuffers[i]) ||
            FAILED(m_constantBuffers[i]->Map(0, &noRead, &m_mappedConstants[i]))) {
            LogError("Direct3D 12: per-frame scene constant buffer allocation failed.");
            return false;
        }
    }
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_pipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: graphics pipeline state creation failed.");
        return false;
    }
    return true;
}

bool D3D12Renderer::CreateShadowPipeline()
{
    constexpr char shader[] = R"(
cbuffer ShadowConstants : register(b0) { float4x4 lightViewProjection; };
struct VSInput { float3 position : POSITION; };
float4 VSMain(VSInput input) : SV_POSITION
{
    return mul(lightViewProjection, float4(input.position, 1.0));
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_shadow", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: shadow vertex shader compilation failed.");
        return false;
    }
    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthBias = 1200;
    pso.RasterizerState.DepthBiasClamp = 0.002F;
    pso.RasterizerState.SlopeScaledDepthBias = 1.5F;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {input, static_cast<UINT>(std::size(input))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 0;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_shadowPipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: cascaded shadow pipeline creation failed.");
        return false;
    }
    return true;
}

bool D3D12Renderer::CreateTemporalPipeline()
{
    constexpr char shader[] = R"(
Texture2D<float4> hdrScene : register(t0);
Texture2D<float> sceneDepth : register(t1);
#if HISTORY_READ_INDEX == 0
Texture2D<float4> historyColor0 : register(t3);
Texture2D<float> historyDepth0 : register(t5);
#else
Texture2D<float4> historyColor1 : register(t4);
Texture2D<float> historyDepth1 : register(t6);
#endif
SamplerState linearClamp : register(s0);
cbuffer TemporalConstants : register(b1)
{
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4 temporalParams;
    float4 sunParameters;
};
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
float4 ReadHistoryColor(float2 uv)
{
#if HISTORY_READ_INDEX == 0
    return historyColor0.SampleLevel(linearClamp, uv, 0);
#else
    return historyColor1.SampleLevel(linearClamp, uv, 0);
#endif
}
float ReadHistoryDepth(float2 uv)
{
#if HISTORY_READ_INDEX == 0
    return historyDepth0.SampleLevel(linearClamp, uv, 0);
#else
    return historyDepth1.SampleLevel(linearClamp, uv, 0);
#endif
}
struct TemporalOutput { float4 color : SV_TARGET0; float depth : SV_TARGET1; };
TemporalOutput PSMain(VSOut input)
{
    TemporalOutput output;
    float depth = sceneDepth.SampleLevel(linearClamp, input.uv, 0);
    float3 current = max(hdrScene.SampleLevel(linearClamp, input.uv, 0).rgb, 0.0);
    output.depth = depth;
    if (temporalParams.x < 0.5 || depth >= 0.99999)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 world = mul(inverseViewProjection, float4(ndc, depth, 1.0));
    if (abs(world.w) < 1.0e-6)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    world /= world.w;
    float4 previousClip = mul(previousViewProjection, world);
    if (previousClip.w <= 1.0e-5)
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float2 previousUv = float2(previousClip.x / previousClip.w * 0.5 + 0.5,
        0.5 - previousClip.y / previousClip.w * 0.5);
    if (any(previousUv <= 0.001) || any(previousUv >= 0.999))
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float oldDepth = ReadHistoryDepth(previousUv);
    float expectedDepth = previousClip.z / previousClip.w;
    if (abs(oldDepth - expectedDepth) > max(0.0015, expectedDepth * 0.012))
    {
        output.color = float4(current, 1.0);
        return output;
    }
    float3 neighborhoodMin = current;
    float3 neighborhoodMax = current;
    uint width, height;
    hdrScene.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(width, 1u), max(height, 1u));
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float3 sampleColor = hdrScene.SampleLevel(linearClamp,
                saturate(input.uv + float2(x, y) * texel), 0).rgb;
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
        }
    }
    float3 history = ReadHistoryColor(previousUv).rgb;
    float3 expansion = (neighborhoodMax - neighborhoodMin) * 0.12 + 0.002;
    history = clamp(history, neighborhoodMin - expansion, neighborhoodMax + expansion);
    output.color = float4(lerp(current, history, 0.88), 1.0);
    return output;
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_temporal", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: temporal vertex shader compilation failed.");
        return false;
    }
    std::array<Microsoft::WRL::ComPtr<ID3DBlob>, 2> pixelShaders;
    const D3D_SHADER_MACRO readHistory0[] = {{"HISTORY_READ_INDEX", "0"}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO readHistory1[] = {{"HISTORY_READ_INDEX", "1"}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO* historyDefines[] = {readHistory0, readHistory1};
    for (UINT i = 0; i < 2; ++i) {
        errors.Reset();
        hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_temporal", historyDefines[i], nullptr,
            "PSMain", "ps_5_0", flags, 0, pixelShaders[i].GetAddressOf(), errors.GetAddressOf());
        if (FAILED(hr)) {
            if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
            LogError("Direct3D 12: temporal pixel shader compilation failed.");
            return false;
        }
    }
    for (UINT i = 0; i < kFrameCount; ++i) {
        if (!CreateUploadBuffer(m_device.Get(), 256, m_temporalConstantBuffers[i]) ||
            FAILED(m_temporalConstantBuffers[i]->Map(0, nullptr, &m_mappedTemporalConstants[i]))) {
            LogError("Direct3D 12: per-frame temporal constant buffer allocation failed.");
            return false;
        }
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {pixelShaders[0]->GetBufferPointer(), pixelShaders[0]->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {nullptr, 0};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;
    for (UINT i = 0; i < 2; ++i) {
        pso.PS = {pixelShaders[i]->GetBufferPointer(), pixelShaders[i]->GetBufferSize()};
        if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_temporalPipelineStates[i].GetAddressOf())))) {
            LogError("Direct3D 12: temporal resolve pipeline creation failed.");
            return false;
        }
    }
    return true;
}

bool D3D12Renderer::CreatePostProcessPipeline()
{
    constexpr char shader[] = R"(
Texture2D hdrScene : register(t0);
Texture2D sceneDepth : register(t1);
Texture2D<float4> historyColor0 : register(t3);
Texture2D<float4> historyColor1 : register(t4);
SamplerState linearClamp : register(s0);
cbuffer TemporalConstants : register(b1)
{
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4 temporalParams;
    float4 sunParameters;
};
float3 ResolvedColor(float2 uv)
{
    return temporalParams.z < 0.5
        ? historyColor0.SampleLevel(linearClamp, uv, 0).rgb
        : historyColor1.SampleLevel(linearClamp, uv, 0).rgb;
}
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
float3 ACES(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
float4 PSMain(VSOut input) : SV_TARGET
{
    float depth = sceneDepth.SampleLevel(linearClamp, input.uv, 0).r;
    if (depth >= 0.99999)
    {
        float horizon = saturate(1.0 - abs(input.uv.y - 0.52) * 1.7);
        float daylight = saturate(sunParameters.z);
        float3 nightZenith = float3(0.006, 0.012, 0.032);
        float3 dayZenith = float3(0.075, 0.20, 0.42);
        float3 zenith = lerp(nightZenith, dayZenith, daylight);
        float3 nightHorizon = float3(0.025, 0.035, 0.075);
        float3 sunsetHorizon = float3(0.95, 0.24, 0.075);
        float3 dayHorizon = float3(0.56, 0.72, 0.86);
        float3 horizonColor = lerp(lerp(nightHorizon, sunsetHorizon, saturate(sunParameters.w * (1.0 - daylight))), dayHorizon, daylight);
        float3 sky = lerp(zenith, horizonColor, pow(saturate(1.0 - input.uv.y), 0.72));
        float2 sunUv = input.uv - sunParameters.xy;
        float sun = exp(-dot(sunUv, sunUv) * 1900.0);
        float halo = exp(-dot(sunUv, sunUv) * 85.0);
        float3 solarDisc = lerp(float3(1.0, 0.32, 0.10), float3(1.0, 0.91, 0.73), daylight);
        sky += solarDisc * (sun * 7.0 * daylight + halo * 0.8 * saturate(daylight + 0.18));
        sky += float3(0.12, 0.16, 0.24) * horizon * daylight;
        return float4(pow(ACES(max(sky, 0.0)), 1.0 / 2.2), 1.0);
    }
    uint width, height;
    hdrScene.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(width, 1u), max(height, 1u));
    float3 color = max(ResolvedColor(input.uv), 0.0);
    // Small cross-kernel contact AO approximation from neighboring depth discontinuities.
    float ao = 1.0;
    const float2 offsets[8] = {
        float2(1,0), float2(-1,0), float2(0,1), float2(0,-1),
        float2(2,1), float2(-2,1), float2(2,-1), float2(-2,-1)
    };
    float occlusion = 0.0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float sampleDepth = sceneDepth.SampleLevel(linearClamp, saturate(input.uv + offsets[i] * texel * 2.0), 0).r;
        occlusion += step(0.00015, depth - sampleDepth) * saturate(1.0 - abs(depth - sampleDepth) * 85.0);
    }
    ao = 1.0 - saturate(occlusion / 8.0) * 0.24;
    color *= ao;
    float3 bloom = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y)
    {
        [unroll] for (int x = -2; x <= 2; ++x)
        {
            float3 sampleColor = ResolvedColor(
                saturate(input.uv + float2(x, y) * texel * 3.0));
            float luminance = dot(sampleColor, float3(0.2126, 0.7152, 0.0722));
            bloom += sampleColor * smoothstep(1.0, 2.8, luminance);
        }
    }
    color += bloom * (0.45 / 25.0);
    // Keep display transform channel-neutral; tint correction belongs in lighting,
    // not in a global per-channel multiplier applied to every material.
    color = ACES(color * 1.05);
    color = pow(max(color, 0.0), 1.0 / 2.2);
    return float4(color, 1.0);
}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_post", nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, vs.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: post-process vertex shader compilation failed.");
        return false;
    }
    errors.Reset();
    hr = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_post", nullptr, nullptr,
        "PSMain", "ps_5_0", flags, 0, ps.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: post-process pixel shader compilation failed.");
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC blend = {
        FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    for (auto& target : pso.BlendState.RenderTarget) target = blend;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.InputLayout = {nullptr, 0};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_postProcessPipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: post-process pipeline creation failed.");
        return false;
    }
    return true;
}


} // namespace vanta
