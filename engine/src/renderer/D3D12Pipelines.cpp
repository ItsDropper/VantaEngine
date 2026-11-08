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
    float4 environmentSunColor;
    float4 environmentAmbient;
    float4 environmentFog;
    float4 environmentFogParameters;
    float4 environmentWind;
};
Texture2DArray<float> shadowMap : register(t2);
Texture2DArray modelAlbedoTexture : register(t7);
Texture2DArray modelNormalTexture : register(t8);
Texture2DArray modelRoughnessTexture : register(t9);
Texture2DArray modelMetallicTexture : register(t10);
Texture2DArray modelOcclusionTexture : register(t11);
Texture2DArray submittedAlbedoTexture : register(t12);
Texture2DArray submittedNormalTexture : register(t13);
Texture2DArray submittedRoughnessTexture : register(t14);
Texture2DArray submittedMetallicTexture : register(t15);
Texture2DArray submittedOcclusionTexture : register(t16);
cbuffer InstanceConstants : register(b1) { float4x4 instanceWorldConstants; };
SamplerState linearClamp : register(s0);
SamplerState linearWrap : register(s1);
float3 TransformInstanceNormal(float3 normal, float4x4 instanceWorld)
{
    const float3x3 linearWorld = (float3x3)instanceWorld;
    const float3 cofactor0 = cross(linearWorld[1], linearWorld[2]);
    const float3 cofactor1 = cross(linearWorld[2], linearWorld[0]);
    const float3 cofactor2 = cross(linearWorld[0], linearWorld[1]);
    const float determinant = dot(linearWorld[0], cofactor0);
    return normalize(float3(dot(cofactor0, normal), dot(cofactor1, normal), dot(cofactor2, normal)) / determinant);
}
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float4 material : TEXCOORD1;
    float4 worldRow0 : INSTANCEWORLD0;
    float4 worldRow1 : INSTANCEWORLD1;
    float4 worldRow2 : INSTANCEWORLD2;
    float4 worldRow3 : INSTANCEWORLD3;
};
struct PSInput { float4 position : SV_POSITION; float3 worldPosition : TEXCOORD0; float3 normal : TEXCOORD1; float4 color : COLOR; float2 uv : TEXCOORD2; float4 material : TEXCOORD3; };
PSInput VSMain(VSInput input)
{
    PSInput o;
    const float4x4 instanceWorld = float4x4(input.worldRow0, input.worldRow1,
        input.worldRow2, input.worldRow3);
    const float4x4 instanceWorld = float4x4(input.worldRow0, input.worldRow1,
        input.worldRow2, input.worldRow3);
    float3 worldPosition = mul(instanceWorld, float4(input.position, 1.0F)).xyz;
    // Wind only affects green-dominant foliage. Trunks, buildings, rocks, and terrain
    // remain rigid. The height weight keeps grass motion subtle and tree crowns flexible.
    float foliageMask = step(input.color.r * 1.28, input.color.g) *
                        step(input.color.b * 1.20, input.color.g) *
                        step(0.105, input.color.g) * step(0.9, input.color.a);
    float heightWeight = saturate((worldPosition.y + 0.35) * 0.13);
    // Triangle-wave wind avoids per-vertex sin/cos, which is costly on dense foliage.
    float phaseA = cameraPosition.w * 1.25 + worldPosition.x * 0.31 + worldPosition.z * 0.47;
    float phaseB = cameraPosition.w * 0.83 + worldPosition.z * 0.29 - worldPosition.x * 0.23;
    float waveA = 1.0 - 4.0 * abs(frac(phaseA * 0.15915494) - 0.5);
    float waveB = 1.0 - 4.0 * abs(frac(phaseB * 0.15915494) - 0.5);
    float2 windDirection = environmentWind.w > 0.5
        ? normalize(environmentWind.xy) : normalize(float2(1.0, 0.529));
    float windStrength = environmentWind.w > 0.5 ? environmentWind.z : 0.085;
    worldPosition.x += waveA * windStrength * windDirection.x * foliageMask * heightWeight;
    worldPosition.z += waveB * windStrength * windDirection.y * foliageMask * heightWeight;
    o.position = mul(viewProjection, float4(worldPosition, 1.0));
    o.worldPosition = worldPosition;
    o.normal = TransformInstanceNormal(input.normal, instanceWorld);
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

    // Mid/far cascades occupy the upper-left 1024² region of their 2048² array slice.
    const float shadowScale = cascade == 0 ? 1.0 : 0.5;
    uv *= shadowScale;
    const float2 texel = cascade == 0 ? 1.0 / 2048.0 : 1.0 / 1024.0;
    const float bias = max(0.00018 * (1.0 - saturate(dot(normal, lightDirection))), 0.000045);

    // A single 3x3 PCF kernel costs nine depth reads. The previous blocker-search
    // plus filtering path performed eighteen reads for every shaded pixel, which
    // was disproportionately expensive on dense imported vegetation.
    float visibility = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            visibility += projected.z - bias <= shadowMap.SampleLevel(
                linearClamp, float3(uv + texel * float2(x, y), cascade), 0) ? 1.0 : 0.0;
    return lerp(0.38, 1.0, visibility / 9.0);
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    uint materialFlags = (uint)(input.material.w + 0.5);
    float3 dp1 = ddx(input.worldPosition);
    float3 dp2 = ddy(input.worldPosition);
    float2 duv1 = ddx(input.uv);
    float2 duv2 = ddy(input.uv);
    const bool submittedMaterial = (materialFlags & 128U) != 0U;
    if ((materialFlags & 2U) != 0U)
    {
        float3 tangentNormal = submittedMaterial
            ? submittedNormalTexture.Sample(linearWrap, float3(input.uv, input.color.a)).xyz * 2.0 - 1.0
            : modelNormalTexture.Sample(linearWrap, float3(input.uv, input.color.a)).xyz * 2.0 - 1.0;
        float3 tangent = normalize(dp1 * duv2.y - dp2 * duv1.y);
        float3 bitangent = normalize(-dp1 * duv2.x + dp2 * duv1.x);
        n = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + n * tangentNormal.z);
    }
    float3 v = normalize(cameraPosition.xyz - input.worldPosition);
    float3 l = normalize(lighting.xyz);
    float3 h = normalize(v + l);
    float3 albedo = max(input.color.rgb, 0.0);
    if ((materialFlags & 1U) != 0U)
        albedo *= submittedMaterial
            ? submittedAlbedoTexture.Sample(linearWrap, float3(input.uv, input.color.a)).rgb
            : modelAlbedoTexture.Sample(linearWrap, float3(input.uv, input.color.a)).rgb;
    // Determine whether this pixel belongs to the procedural terrain before evaluating
    // its expensive multi-octave noise. The merged scene also contains foliage, cabins,
    // rocks and props; those pixels should not pay the terrain-material cost.
    float2 worldXZ = input.worldPosition.xz;
    float importedMaterial = saturate(input.material.z);
    float isTerrain = (materialFlags & 64U) != 0U ? 1.0 : 0.0;
    float snowTerrainMask = (materialFlags & 256U) != 0U ? 1.0 : 0.0;
    // Generic procedural ground remains limited to low-altitude grass/soil.
    // Sakkola's authored snow surface is a separate semantic material and must
    // not be disabled by the old 1.65 m grass-terrain height cutoff.
    float terrainColorMask = isTerrain * (1.0 - snowTerrainMask) *
        (1.0 - smoothstep(0.15, 1.65, input.worldPosition.y));
    float terrainSurfaceMask = max(terrainColorMask, snowTerrainMask);
    float macroNoise = 0.5;
    float breakup = 0.5;
    float clumpNoise = 0.5;
    float fineNoise = 0.5;
    float grainNoise = 0.5;
    float flecks = 0.5;
    float microHeight = 0.0;
    float roughness = saturate(lerp(0.76, input.material.x, importedMaterial));

    if (snowTerrainMask > 0.001)
    {
        const float2 windDirection = environmentWind.w > 0.5
            ? normalize(environmentWind.xy) : normalize(float2(0.914, 0.406));
        const float broadDrift = TerrainNoise(worldXZ * 0.012 + float2(17.0, -9.0));
        const float crossWindDrift = TerrainNoise(worldXZ * 0.031 +
            float2(-windDirection.y, windDirection.x) * 23.0);
        const float crustNoise = TerrainNoise(worldXZ * 0.17 + float2(4.3, 19.1));
        const float crystalNoise = ValueNoise(worldXZ * 2.4 + float2(31.0, -7.0));
        const float fineGrain = ValueNoise(worldXZ * 9.0 + float2(-13.0, 27.0));
        const float slope = 1.0 - saturate(n.y);
        const float windExposure = saturate(0.5 +
            dot(normalize(worldXZ + float2(0.001, 0.001)), windDirection) * 0.18);
        const float packedMask = saturate(0.30 + (broadDrift - 0.5) * 0.55 +
            (crossWindDrift - 0.5) * 0.38 + slope * 0.42 + windExposure * 0.12);
        const float crustMask = saturate((crustNoise - 0.43) * 1.7 +
            (fineGrain - 0.5) * 0.28);
        const float exposedStone = saturate(smoothstep(0.34, 0.72,
            slope + (broadDrift - 0.5) * 0.22) * 0.34);
        const float3 freshSnow = albedo *
            (0.965 + (broadDrift - 0.5) * 0.095 + (crystalNoise - 0.5) * 0.035);
        const float3 windPackedSnow = albedo * float3(0.84, 0.895, 0.96) *
            (0.965 + (crustNoise - 0.5) * 0.065);
        float3 snowAlbedo = lerp(freshSnow, windPackedSnow, packedMask * 0.62);
        snowAlbedo *= 0.97 + fineGrain * 0.055;
        const float3 exposedRockAlbedo = float3(0.34, 0.365, 0.39) *
            (0.91 + crystalNoise * 0.18);
        snowAlbedo = lerp(snowAlbedo, exposedRockAlbedo, exposedStone);
        albedo = lerp(albedo, snowAlbedo, snowTerrainMask);
        roughness = lerp(0.965, 0.76, packedMask * 0.58 + crustMask * 0.12);
        microHeight = (broadDrift - 0.5) * 0.48 +
            (crossWindDrift - 0.5) * 0.27 +
            (crustNoise - 0.5) * 0.13 +
            (crystalNoise - 0.5) * 0.075 +
            (fineGrain - 0.5) * 0.035;
    }

    if (terrainColorMask > 0.001)
    {
        macroNoise = TerrainNoise(worldXZ * 0.055);
        breakup = TerrainNoise(worldXZ * 0.19 + float2(13.7, -8.2));
        clumpNoise = TerrainNoise(worldXZ * 0.73 + float2(-21.4, 5.8));
        fineNoise = TerrainNoise(worldXZ * 2.7);
        grainNoise = TerrainNoise(worldXZ * 8.4 + float2(3.1, 19.7));
        flecks = ValueNoise(worldXZ * 31.0 + float2(47.2, -11.3));

        float slope = 1.0 - saturate(n.y);
        float altitudeRock = smoothstep(0.9, 2.6, input.worldPosition.y);
        float rockMask = saturate(
            smoothstep(0.24, 0.70, slope + (macroNoise - 0.5) * 0.28) * 0.88 +
            altitudeRock * 0.20);
        float exposedSoil = smoothstep(0.52, 0.76, breakup + (clumpNoise - 0.5) * 0.24);
        float soilMask = (1.0 - rockMask) * exposedSoil;
        float grassVariation = 0.78 + macroNoise * 0.30 + (breakup - 0.5) * 0.22;
        float fibreA = 0.5 + 0.5 * sin(worldXZ.x * 38.0 + sin(worldXZ.y * 17.0) * 1.8);
        float fibreB = 0.5 + 0.5 * sin((worldXZ.x + worldXZ.y * 0.63) * 27.0 + clumpNoise * 5.0);
        float grassFibres = lerp(0.78, 1.12, fibreA * 0.62 + fibreB * 0.38);
        float3 grassAlbedo = albedo * grassVariation * lerp(0.87, 1.10, fineNoise) * grassFibres;
        float soilCrumb = lerp(0.66, 1.22, grainNoise) * lerp(0.86, 1.12, flecks);
        float3 soilTint = float3(0.54, 0.40, 0.27);
        float3 soilAlbedo = albedo * soilTint * soilCrumb * lerp(0.78, 1.12, clumpNoise);
        float strata = 0.5 + 0.5 * sin((worldXZ.x * 0.83 + worldXZ.y * 0.47) +
            (breakup - 0.5) * 4.0);
        float mineral = lerp(0.72, 1.16, grainNoise) * lerp(0.82, 1.12, flecks);
        float crack = smoothstep(0.68, 0.84, fineNoise) * smoothstep(0.42, 0.68, strata);
        float3 rockAlbedo = float3(0.31, 0.315, 0.30) * mineral * lerp(1.0, 0.48, crack);
        rockAlbedo *= lerp(0.88, 1.10, strata);
        float3 terrainAlbedo = lerp(grassAlbedo, soilAlbedo, soilMask * 0.86);
        terrainAlbedo = lerp(terrainAlbedo, rockAlbedo, rockMask * 0.94);
        terrainAlbedo *= 0.94 + 0.11 * (fineNoise - 0.5) + 0.09 * (macroNoise - 0.5);
        albedo = lerp(albedo, terrainAlbedo, terrainColorMask);
        roughness = saturate(0.76 + 0.16 * (grainNoise - 0.5) + terrainColorMask * 0.10);
        microHeight = TerrainNoise(worldXZ * 5.5) * 0.62 +
            TerrainNoise(worldXZ * 17.0 + float2(8.3, 2.1)) * 0.28 +
            TerrainNoise(worldXZ * 43.0 - float2(4.7, 12.6)) * 0.10;
    }
    const float authoredRoughness = snowTerrainMask > 0.5
        ? roughness : input.material.x + 0.05 * (grainNoise - 0.5);
    roughness = saturate(lerp(roughness, authoredRoughness, importedMaterial));

    // Keep derivatives outside the terrain branch: pixel-shader derivatives must remain
    // well-defined across 2x2 lanes. The zero micro-height on non-terrain pixels is masked
    // by terrainColorMask when blending the resulting normal.
    float3 terrainDpdx = ddx(input.worldPosition);
    float3 tangent = normalize(terrainDpdx - n * dot(terrainDpdx, n));
    float3 bitangent = normalize(cross(n, tangent));
    float3 microNormal = normalize(n - tangent * ddx(microHeight) * 2.4 -
        bitangent * ddy(microHeight) * 2.4);
    n = normalize(lerp(n, microNormal, terrainSurfaceMask * 0.92));

    // Subtle material-specific micro-relief for imported surfaces that do not yet carry
    // authored normal maps. The height fields follow the UV direction for bark/needles
    // and world-space triplanar scale for stone/snow, so they do not become screen-space
    // speckle or a single repeated checker pattern.
    float luminance = dot(albedo, float3(0.2126, 0.7152, 0.0722));
    float foliageWeight = saturate((albedo.g - max(albedo.r, albedo.b)) * 5.0);
    float woodWeight = saturate((albedo.r - albedo.b) * 2.8) * (1.0 - foliageWeight);
    float snowWeight = saturate((luminance - 0.48) * 2.2);
    float stoneWeight = saturate(1.0 - foliageWeight - woodWeight - snowWeight);
    float barkGrain = 0.0;
    float needleRelief = 0.0;
    float stoneRelief = 0.0;
    float snowRelief = 0.0;
    // Evaluate only the material-specific height field that contributes to this
    // pixel. The previous shader evaluated four independent multi-octave noise
    // fields for every fragment, including foliage and snow that used only one.
    if (woodWeight > 0.001)
    {
        barkGrain = ValueNoise(float2(input.uv.x * 38.0, input.uv.y * 190.0)) * 0.68 +
            ValueNoise(float2(input.uv.x * 112.0, input.uv.y * 510.0) + 7.3) * 0.22 +
            (0.5 + 0.5 * sin(input.uv.x * 510.0 + ValueNoise(input.uv * 73.0) * 3.0)) * 0.10;
    }
    if (foliageWeight > 0.001)
    {
        needleRelief = ValueNoise(input.uv * 185.0 + float2(13.0, 7.0)) * 0.58 +
            ValueNoise(input.uv * 420.0 - float2(5.0, 19.0)) * 0.42;
    }
    if (stoneWeight > 0.001)
    {
        stoneRelief = TerrainNoise(worldXZ * 10.5) * 0.58 +
            TerrainNoise(worldXZ * 31.0 + float2(4.7, -13.2)) * 0.29 +
            ValueNoise(worldXZ * 83.0) * 0.13;
    }
    if (snowWeight > 0.001)
    {
        snowRelief = ValueNoise(worldXZ * 34.0) * 0.62 +
            ValueNoise(worldXZ * 91.0 + float2(9.0, 21.0)) * 0.38;
    }
    float materialHeight = woodWeight * barkGrain + foliageWeight * needleRelief +
        stoneWeight * stoneRelief + snowWeight * snowRelief;
    float3 materialTangentRaw = dp1 * duv2.y - dp2 * duv1.y;
    float tangentLengthSquared = dot(materialTangentRaw, materialTangentRaw);
    float3 fallbackAxis = abs(n.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 fallbackTangent = normalize(cross(fallbackAxis, n));
    float3 materialTangent = tangentLengthSquared > 1.0e-8
        ? materialTangentRaw * rsqrt(tangentLengthSquared) : fallbackTangent;
    float uvHandedness = (duv1.x * duv2.y - duv1.y * duv2.x) < 0.0 ? -1.0 : 1.0;
    float3 materialBitangent = normalize(cross(n, materialTangent)) * uvHandedness;
    float3 materialMicroNormal = normalize(n - materialTangent * ddx(materialHeight) * 1.25 -
        materialBitangent * ddy(materialHeight) * 1.25);
    float materialReliefMask = importedMaterial * (1.0 - terrainSurfaceMask) *
        (((materialFlags & 2U) != 0U) ? 0.0 : 1.0);
    n = normalize(lerp(n, materialMicroNormal, materialReliefMask * 0.72));

    float metallic = saturate(lerp(0.025, input.material.y, importedMaterial));
    if ((materialFlags & 4U) != 0U)
    {
        float4 roughnessChannels = submittedMaterial
            ? submittedRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a))
            : modelRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a));
        float roughnessSample = roughnessChannels.r;
        if ((materialFlags & 32U) != 0U)
            roughnessSample = roughnessChannels.g;
        roughness = saturate(roughness * roughnessSample);
    }
    if ((materialFlags & 8U) != 0U)
        metallic = saturate(metallic * (submittedMaterial
            ? submittedMetallicTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r
            : modelMetallicTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r));
    if ((materialFlags & 32U) != 0U)
    {
        const float packedMetallic = submittedMaterial
            ? submittedRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a)).b
            : modelRoughnessTexture.Sample(linearWrap, float3(input.uv, input.color.a)).b;
        metallic = saturate(metallic * packedMetallic);
    }
    float ambientOcclusion = (materialFlags & 16U) != 0U
        ? (submittedMaterial
            ? submittedOcclusionTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r
            : modelOcclusionTexture.Sample(linearWrap, float3(input.uv, input.color.a)).r)
        : 1.0;
    float ndl = saturate(dot(n, l));
    float ndv = max(saturate(dot(n, v)), 0.001);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 f = FresnelSchlick(saturate(dot(h, v)), f0);
    float d = DistributionGGX(n, h, roughness);
    float g = GeometrySchlick(ndv, roughness) * GeometrySchlick(max(ndl, 0.001), roughness);
    float3 specular = d * g * f / max(4.0 * ndv * max(ndl, 0.001), 0.001);
    float3 kD = (1.0 - f) * (1.0 - metallic);
    float shadow = CalculateShadow(input.worldPosition, n, l);
    // Approximate clear-sky solar transmittance. The longer atmospheric path at
    // low elevation preferentially removes blue wavelengths, warming sunrise/sunset
    // light without applying a global color grade to materials.
    float solarElevation = max(saturate(lighting.y), 0.015);
    float solarZenithDegrees = 90.0 - degrees(asin(solarElevation));
    float airMass = min(1.0 / max(solarElevation + 0.50572 *
        pow(max(96.07995 - solarZenithDegrees, 0.1), -1.6364), 0.02), 12.0);
    float3 solarTransmittance = exp(-float3(0.105, 0.041, 0.018) * airMass);
    float3 atmosphericSolar = solarTransmittance * float3(5.25, 5.05, 4.72);
    float3 daylightColor = environmentSunColor.w > 0.5
        ? environmentSunColor.rgb
        : lerp(float3(1.55, 0.46, 0.18), atmosphericSolar,
            smoothstep(0.015, 0.30, solarElevation));
    float3 direct = (kD * albedo / 3.14159265 + specular) * daylightColor * lighting.w * ndl * shadow;
    // Snow is a translucent dielectric: add restrained forward-scattering and
    // back-lit transmission only to bright, rough, non-metallic surfaces.
    float snowSubsurfaceMask = saturate((min(albedo.r, min(albedo.g, albedo.b)) - 0.70) * 5.0) *
        saturate((roughness - 0.58) * 2.4) * (1.0 - metallic);
    float wrappedSnowDiffuse = saturate((dot(n, l) + 0.32) / 1.32);
    float snowBackScatter = pow(saturate(dot(-l, v)), 3.0);
    float3 snowTransmission = albedo * daylightColor * lighting.w *
        (wrappedSnowDiffuse * 0.075 + snowBackScatter * 0.12) *
        snowSubsurfaceMask * (1.0 - shadow * 0.35);
    direct += snowTransmission;
    // Hemispherical sky irradiance: the horizon carries atmospheric haze while
    // the upper hemisphere retains the colder blue skylight of a clear winter day.
    float skyElevation = saturate(n.y * 0.5 + 0.5);
    float horizonWeight = pow(1.0 - skyElevation, 2.2);
    float3 physicallyBasedSky = lerp(float3(0.29, 0.35, 0.40),
        float3(0.075, 0.145, 0.245), skyElevation);
    physicallyBasedSky = lerp(physicallyBasedSky,
        float3(0.48, 0.52, 0.54), horizonWeight * 0.24);
    float3 sky = environmentSunColor.w > 0.5
        ? lerp(environmentAmbient.rgb * 0.18, environmentAmbient.rgb, skyElevation)
        : physicallyBasedSky;
    float ambientIntensity = environmentSunColor.w > 0.5 ? environmentAmbient.w : 1.05;
    float3 ambient = sky * albedo * (1.0 - metallic) * ambientIntensity * ambientOcclusion;
    float3 reflectedSky = environmentSunColor.w > 0.5
        ? lerp(environmentAmbient.rgb * 0.35, environmentAmbient.rgb * 1.2,
            saturate(reflect(-v, n).y * 0.5 + 0.5))
        : lerp(float3(0.10, 0.095, 0.08), float3(0.30, 0.29, 0.25),
            saturate(reflect(-v, n).y * 0.5 + 0.5));
    ambient += reflectedSky * f * (1.0 - roughness * 0.7) * 0.45;
    float3 cameraToSurface = input.worldPosition - cameraPosition.xyz;
    float distanceToCamera = length(cameraToSurface);
    float3 viewRay = cameraToSurface / max(distanceToCamera, 1.0e-4);
    float fogStart = environmentFogParameters.y > 0.5 ? environmentFogParameters.x : 18.0;
    float fogDensity = environmentFogParameters.y > 0.5 ? environmentFog.w : 0.018;
    float fogBaseHeight = environmentFogParameters.y > 0.5 ? environmentFogParameters.z : -1.5;
    float heightFalloff = environmentFogParameters.y > 0.5 ? environmentFogParameters.w : 0.075;
    float fogLength = max(distanceToCamera - fogStart, 0.0);
    // Four stratified integration samples approximate the extinction integral along
    // the camera ray. Density increases in cold air pooled between snow-covered trees.
    float opticalDepth = 0.0;
    [unroll] for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
    {
        float sampleDistance = fogStart + fogLength * ((sampleIndex + 0.5) * 0.25);
        float sampleHeight = cameraPosition.y + viewRay.y * sampleDistance;
        float localDensity = fogDensity * exp(-max(sampleHeight - fogBaseHeight, 0.0) * heightFalloff);
        opticalDepth += localDensity * fogLength * 0.25;
    }
    float fog = saturate(1.0 - exp(-opticalDepth));
    float3 fogColor = environmentFogParameters.y > 0.5 ? environmentFog.rgb
        : lerp(float3(0.025, 0.032, 0.055), float3(0.56, 0.66, 0.76), saturate(lighting.w * 0.85));
    // Henyey-Greenstein forward scattering gives the low winter sun a restrained
    // luminous shaft contribution without bleaching the scene's diffuse materials.
    float phaseCosine = dot(-viewRay, l);
    const float anisotropy = 0.32;
    float phaseDenominator = pow(max(1.0 + anisotropy * anisotropy -
        2.0 * anisotropy * phaseCosine, 0.08), 1.5);
    float phase = (1.0 - anisotropy * anisotropy) /
        (12.5663706 * phaseDenominator);
    // Integrate a restrained single-scattering term with the solar phase function.
    // Height-dependent extinction above already models cold-air pooling; this term
    // adds the warm/cool directional response that makes distant forest layers recede.
    float3 inScattering = daylightColor * phase * lighting.w * fog * 0.30;
    float horizonHaze = saturate(1.0 - abs(viewRay.y) * 1.7);
    float aerialPerspective = fog * horizonHaze;
    fogColor = max(fogColor + inScattering +
        lerp(float3(0.34, 0.41, 0.47), daylightColor * 0.055,
            saturate(phase * 8.0)) * aerialPerspective, 0.0);
    float3 litColor = max(lerp(direct + ambient, fogColor, fog), 0.0);
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

bool ValidateD3D12SceneShader(std::string& error)
{
    const auto compile = [&error](const char* entryPoint, const char* profile)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(kSceneShader, sizeof(kSceneShader) - 1U,
            nullptr, nullptr, nullptr, entryPoint, profile, D3DCOMPILE_ENABLE_STRICTNESS,
            0U, bytecode.GetAddressOf(), errors.GetAddressOf());
        if (SUCCEEDED(result))
            return true;
        error = errors
            ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize())
            : std::string("D3D12 scene shader compilation failed without diagnostics.");
        return false;
    };
    if (!compile("VSMain", "vs_5_0") || !compile("PSMain", "ps_5_0"))
        return false;
    error.clear();
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

    D3D12_DESCRIPTOR_RANGE srvRanges[2]{};
    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 12;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[1].NumDescriptors = D3D12Renderer::kSubmittedMaterialSrvCount;
    srvRanges[1].BaseShaderRegister = 12;
    srvRanges[1].RegisterSpace = 0;
    srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12Renderer::kSubmittedMaterialBaseSrvIndex;
    D3D12_ROOT_PARAMETER rootParameters[4]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 2;
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvRanges;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 1;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[3].Constants.ShaderRegister = 1;
    rootParameters[3].Constants.RegisterSpace = 0;
    rootParameters[3].Constants.Num32BitValues = 16;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
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
    rootDescription.NumParameters = 4;
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
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"INSTANCEWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}
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
cbuffer ShadowConstants : register(b0) { float4x4 lightViewProjection; float windTime; };
struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float4 worldRow0 : INSTANCEWORLD0;
    float4 worldRow1 : INSTANCEWORLD1;
    float4 worldRow2 : INSTANCEWORLD2;
    float4 worldRow3 : INSTANCEWORLD3;
};
float4 VSMain(VSInput input) : SV_POSITION
{
    float3 worldPosition = mul(instanceWorld, float4(input.position, 1.0F)).xyz;
    float foliageMask = step(input.color.r * 1.28, input.color.g) *
                        step(input.color.b * 1.20, input.color.g) *
                        step(0.105, input.color.g);
    float heightWeight = saturate((worldPosition.y + 0.35) * 0.13);
    float phaseA = windTime * 1.25 + worldPosition.x * 0.31 + worldPosition.z * 0.47;
    float phaseB = windTime * 0.83 + worldPosition.z * 0.29 - worldPosition.x * 0.23;
    float waveA = 1.0 - 4.0 * abs(frac(phaseA * 0.15915494) - 0.5);
    float waveB = 1.0 - 4.0 * abs(frac(phaseB * 0.15915494) - 0.5);
    worldPosition.x += waveA * 0.085 * foliageMask * heightWeight;
    worldPosition.z += waveB * 0.045 * foliageMask * heightWeight;
    return mul(lightViewProjection, float4(worldPosition, 1.0));
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
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"INSTANCEWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}
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
    float4 skyHorizonColor;
    float4 skyZenithColor;
    float4 environmentSunColor;
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
    // Respect the per-frame history weight and reduce accumulation around unstable,
    // high-contrast foliage edges to limit ghost trails during camera motion.
    float neighborhoodRange = max(neighborhoodMax.r - neighborhoodMin.r,
        max(neighborhoodMax.g - neighborhoodMin.g, neighborhoodMax.b - neighborhoodMin.b));
    float historyWeight = saturate(temporalParams.w) *
        (1.0 - saturate(neighborhoodRange * 1.8) * 0.42);
    output.color = float4(lerp(current, history, historyWeight), 1.0);
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
    float4 skyHorizonColor;
    float4 skyZenithColor;
    float4 environmentSunColor;
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
        float customEnvironment = step(0.5, environmentSunColor.w);
        float3 zenith = lerp(lerp(nightZenith, dayZenith, daylight),
            skyZenithColor.rgb, customEnvironment);
        float3 nightHorizon = float3(0.025, 0.035, 0.075);
        float3 sunsetHorizon = float3(0.95, 0.24, 0.075);
        float3 dayHorizon = float3(0.56, 0.72, 0.86);
        float3 defaultHorizon = lerp(lerp(nightHorizon, sunsetHorizon,
            saturate(sunParameters.w * (1.0 - daylight))), dayHorizon, daylight);
        float3 horizonColor = lerp(defaultHorizon, skyHorizonColor.rgb, customEnvironment);
        float3 sky = lerp(zenith, horizonColor, smoothstep(0.12, 0.58, input.uv.y));
        float horizonScattering = exp(-abs(input.uv.y - 0.52) * 8.0);
        sky += horizonColor * horizonScattering * (0.035 + 0.045 * daylight);
        float2 sunUv = input.uv - sunParameters.xy;
        float sun = exp(-dot(sunUv, sunUv) * 1900.0);
        float halo = exp(-dot(sunUv, sunUv) * 85.0);
        float3 defaultSolarDisc = lerp(float3(1.0, 0.32, 0.10), float3(1.0, 0.91, 0.73), daylight);
        float3 solarDisc = lerp(defaultSolarDisc, environmentSunColor.rgb, customEnvironment);
        sky += solarDisc * (sun * 7.0 * daylight + halo * 0.8 * saturate(daylight + 0.18));
        sky += float3(0.12, 0.16, 0.24) * horizon * daylight;
        return float4(pow(ACES(max(sky, 0.0)), 1.0 / 2.2), 1.0);
    }
    uint width, height;
    hdrScene.GetDimensions(width, height);
    float2 texel = 1.0 / float2(max(width, 1u), max(height, 1u));
    float3 color = max(ResolvedColor(input.uv), 0.0);
    // Horizon-based screen-space ambient occlusion. Reconstruct view/world
    // positions from the depth buffer, estimate the local surface normal, then
    // integrate the maximum horizon elevation over eight azimuths and four steps.
    // Unlike raw depth-difference darkening this is distance/radius aware and
    // suppresses false halos at large depth discontinuities.
    float4 centerH = mul(inverseViewProjection,
        float4(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0, depth, 1.0));
    float3 centerPosition = centerH.xyz / max(abs(centerH.w), 1.0e-5);
    float2 pixel = texel;
    float depthRight = sceneDepth.SampleLevel(linearClamp, saturate(input.uv + float2(pixel.x, 0.0)), 0).r;
    float depthDown = sceneDepth.SampleLevel(linearClamp, saturate(input.uv + float2(0.0, pixel.y)), 0).r;
    float4 rightH = mul(inverseViewProjection,
        float4((input.uv.x + pixel.x) * 2.0 - 1.0, 1.0 - input.uv.y * 2.0, depthRight, 1.0));
    float4 downH = mul(inverseViewProjection,
        float4(input.uv.x * 2.0 - 1.0, 1.0 - (input.uv.y + pixel.y) * 2.0, depthDown, 1.0));
    float3 rightPosition = rightH.xyz / max(abs(rightH.w), 1.0e-5);
    float3 downPosition = downH.xyz / max(abs(downH.w), 1.0e-5);
    float3 surfaceNormal = normalize(cross(rightPosition - centerPosition,
        downPosition - centerPosition));
    if (dot(surfaceNormal, centerPosition) > 0.0) surfaceNormal = -surfaceNormal;
    const float2 directions[8] = {
        float2(1.0, 0.0), float2(0.7071, 0.7071),
        float2(0.0, 1.0), float2(-0.7071, 0.7071),
        float2(-1.0, 0.0), float2(-0.7071, -0.7071),
        float2(0.0, -1.0), float2(0.7071, -0.7071)
    };
    float horizonOcclusion = 0.0;
    float horizonWeight = 0.0;
    const float aoRadiusPixels = 14.0;
    [unroll] for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
    {
        float horizon = -1.0;
        [unroll] for (int stepIndex = 1; stepIndex <= 4; ++stepIndex)
        {
            float stepScale = (float)stepIndex * 0.25;
            float2 sampleUv = saturate(input.uv +
                directions[directionIndex] * pixel * aoRadiusPixels * stepScale);
            float sampleDepth = sceneDepth.SampleLevel(linearClamp, sampleUv, 0).r;
            float4 sampleH = mul(inverseViewProjection,
                float4(sampleUv.x * 2.0 - 1.0, 1.0 - sampleUv.y * 2.0, sampleDepth, 1.0));
            float3 samplePosition = sampleH.xyz / max(abs(sampleH.w), 1.0e-5);
            float3 delta = samplePosition - centerPosition;
            float distanceSquared = dot(delta, delta);
            float distanceToSample = sqrt(max(distanceSquared, 1.0e-5));
            float elevation = dot(surfaceNormal, delta / distanceToSample);
            float rangeFade = saturate(1.0 - distanceToSample / 2.8);
            horizon = max(horizon, elevation * rangeFade);
        }
        float directionalOcclusion = saturate(horizon - 0.035);
        horizonOcclusion += directionalOcclusion * directionalOcclusion;
        horizonWeight += 1.0;
    }
    float ao = 1.0 - saturate(horizonOcclusion / max(horizonWeight, 1.0)) * 0.62;
    color *= ao;
    float3 bloom = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            float3 sampleColor = ResolvedColor(
                saturate(input.uv + float2(x, y) * texel * 4.0));
            float luminance = dot(sampleColor, float3(0.2126, 0.7152, 0.0722));
            bloom += sampleColor * smoothstep(1.0, 2.8, luminance);
        }
    }
    color += bloom * (0.45 / 9.0);
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



bool D3D12Renderer::CreateHiZPipeline()
{
    constexpr char shader[] = R"(
cbuffer HiZBuildConstants : register(b0)
{
    uint sourceWidth;
    uint sourceHeight;
    uint destinationWidth;
    uint destinationHeight;
    uint sourceMip;
    uint3 padding;
};
Texture2D<float> sourceDepth : register(t0);
RWTexture2D<float> destinationDepth : register(u0);

[numthreads(8, 8, 1)]
void CopyDepthCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 destination = dispatchThreadId.xy;
    if (destination.x >= destinationWidth || destination.y >= destinationHeight)
        return;
    destinationDepth[destination] = sourceDepth.Load(int3(destination, 0));
}

[numthreads(8, 8, 1)]
void DownsampleHiZCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 destination = dispatchThreadId.xy;
    if (destination.x >= destinationWidth || destination.y >= destinationHeight)
        return;

    // Integer partitioning covers odd source dimensions too; every source texel
    // contributes to exactly one destination texel, with no dropped border row/column.
    const uint2 begin = destination * uint2(sourceWidth, sourceHeight) /
                        uint2(destinationWidth, destinationHeight);
    const uint2 endNumerator = (destination + 1U) * uint2(sourceWidth, sourceHeight);
    const uint2 destinationScale = uint2(destinationWidth, destinationHeight);
    // Round the upper bounds up so source texels straddling a normalized tile edge
    // contribute to both adjacent tiles. This avoids holes from odd mip dimensions.
    const uint2 end = (endNumerator + destinationScale - 1U) / destinationScale;
    float maximumDepth = 0.0F;
    [loop] for (uint y = begin.y; y < end.y; ++y)
    {
        [loop] for (uint x = begin.x; x < end.x; ++x)
        {
            maximumDepth = max(maximumDepth, sourceDepth.Load(int3(x, y, sourceMip)));
        }
    }
    destinationDepth[destination] = maximumDepth;
}
)";

    Microsoft::WRL::ComPtr<ID3DBlob> copyShader, downsampleShader, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT result = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_hiz", nullptr, nullptr,
        "CopyDepthCS", "cs_5_0", flags, 0, copyShader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: Hi-Z depth-copy shader compilation failed.");
        return false;
    }
    errors.Reset();
    result = D3DCompile(shader, sizeof(shader) - 1, "vanta_d3d12_hiz", nullptr, nullptr,
        "DownsampleHiZCS", "cs_5_0", flags, 0, downsampleShader.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: Hi-Z downsample shader compilation failed.");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &uavRange;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = static_cast<UINT>(std::size(parameters));
    rootDescription.pParameters = parameters;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    errors.Reset();
    result = D3D12SerializeRootSignature(&rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRoot.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors) LogError(static_cast<const char*>(errors->GetBufferPointer()));
        LogError("Direct3D 12: Hi-Z root-signature serialization failed.");
        return false;
    }
    if (FAILED(m_device->CreateRootSignature(0, serializedRoot->GetBufferPointer(),
            serializedRoot->GetBufferSize(), IID_PPV_ARGS(m_hiZRootSignature.GetAddressOf())))) {
        LogError("Direct3D 12: Hi-Z root-signature creation failed.");
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = m_hiZRootSignature.Get();
    pipeline.CS.pShaderBytecode = copyShader->GetBufferPointer();
    pipeline.CS.BytecodeLength = copyShader->GetBufferSize();
    if (FAILED(m_device->CreateComputePipelineState(&pipeline,
            IID_PPV_ARGS(m_hiZCopyPipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: Hi-Z copy pipeline creation failed.");
        return false;
    }
    pipeline.CS.pShaderBytecode = downsampleShader->GetBufferPointer();
    pipeline.CS.BytecodeLength = downsampleShader->GetBufferSize();
    if (FAILED(m_device->CreateComputePipelineState(&pipeline,
            IID_PPV_ARGS(m_hiZDownsamplePipelineState.GetAddressOf())))) {
        LogError("Direct3D 12: Hi-Z downsample pipeline creation failed.");
        return false;
    }

    LogInfo("Direct3D 12: max-reduced hierarchical depth pipeline initialized.");
    return true;
}


} // namespace vanta
