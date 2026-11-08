#include "vanta/renderer/SceneGeometry.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace vanta::renderer
{
namespace
{
std::string MaterialTextureKey(const MaterialDescription& material)
{
    const std::array<const std::filesystem::path*, 5> paths = {
        &material.baseColorTexture, &material.normalTexture, &material.roughnessTexture,
        &material.metallicTexture, &material.ambientOcclusionTexture
    };
    std::string key;
    for (const std::filesystem::path* path : paths)
    {
        const std::string value = path->generic_string();
        key += std::to_string(value.size());
        key.push_back(':');
        key += value;
    }
    return key;
}

bool TransformNormal(const math::Mat4& world, const math::Vec3& normal, math::Vec3& output) noexcept
{
    const float a = world.At(0, 0), b = world.At(0, 1), c = world.At(0, 2);
    const float d = world.At(1, 0), e = world.At(1, 1), f = world.At(1, 2);
    const float g = world.At(2, 0), h = world.At(2, 1), i = world.At(2, 2);
    const float c00 = e * i - f * h;
    const float c01 = f * g - d * i;
    const float c02 = d * h - e * g;
    const float c10 = c * h - b * i;
    const float c11 = a * i - c * g;
    const float c12 = b * g - a * h;
    const float c20 = b * f - c * e;
    const float c21 = c * d - a * f;
    const float c22 = a * e - b * d;
    const float determinant = a * c00 + b * c01 + c * c02;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8F)
        return false;

    const float inverseDeterminant = 1.0F / determinant;
    const math::Vec3 transformed{
        (c00 * normal.x + c01 * normal.y + c02 * normal.z) * inverseDeterminant,
        (c10 * normal.x + c11 * normal.y + c12 * normal.z) * inverseDeterminant,
        (c20 * normal.x + c21 * normal.y + c22 * normal.z) * inverseDeterminant
    };
    const auto normalized = transformed.Normalized();
    if (!normalized)
        return false;
    output = *normalized;
    return true;
}
} // namespace

bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error)
{
    output = {};
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    for (const DrawItem& draw : frame.draws)
    {
        if (!draw.mesh || !draw.material)
        {
            error = "Scene frame contains a draw with a missing mesh or material.";
            return false;
        }
        if (draw.mesh->vertices.empty() || draw.mesh->indices.empty() ||
            draw.mesh->indices.size() % 3U != 0U)
        {
            error = "Scene frame contains invalid indexed triangle geometry.";
            return false;
        }
        if (draw.mesh->vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - totalVertices ||
            draw.mesh->indices.size() > std::numeric_limits<std::uint32_t>::max() - totalIndices)
        {
            error = "Submitted scene exceeds the 32-bit draw range limits.";
            return false;
        }
        totalVertices += draw.mesh->vertices.size();
        totalIndices += draw.mesh->indices.size();
    }

    output.vertices.reserve(totalVertices);
    output.indices.reserve(totalIndices);
    output.draws.reserve(frame.draws.size());
    output.materialLayers.reserve(frame.draws.size());
    std::unordered_map<std::string, std::uint32_t> materialLayerByTextureSet;
    materialLayerByTextureSet.reserve(frame.draws.size());

    for (const DrawItem& draw : frame.draws)
    {
        SubmittedDrawRange range;
        range.firstVertex = static_cast<std::uint32_t>(output.vertices.size());
        range.vertexCount = static_cast<std::uint32_t>(draw.mesh->vertices.size());
        range.firstIndex = static_cast<std::uint32_t>(output.indices.size());
        range.indexCount = static_cast<std::uint32_t>(draw.mesh->indices.size());
        range.material = draw.material->description;
        const std::string textureSetKey = MaterialTextureKey(range.material);
        const auto [layerIt, inserted] = materialLayerByTextureSet.emplace(
            textureSetKey, static_cast<std::uint32_t>(output.materialLayers.size()));
        if (inserted)
            output.materialLayers.push_back(range.material);
        range.materialLayer = layerIt->second;
        std::uint32_t materialFlags = 0U;
        if (!range.material.baseColorTexture.empty()) materialFlags |= 1U;
        if (!range.material.normalTexture.empty()) materialFlags |= 2U;
        if (!range.material.roughnessTexture.empty()) materialFlags |= 4U;
        if (!range.material.metallicTexture.empty()) materialFlags |= 8U;
        if (!range.material.ambientOcclusionTexture.empty()) materialFlags |= 16U;
        if (range.material.packedMetallicRoughness) materialFlags |= 32U;
        if (materialFlags != 0U) materialFlags |= 128U;

        for (const assets::ModelVertex& source : draw.mesh->vertices)
        {
            const math::Vec4 transformed = draw.world.Transform(
                {source.position.x, source.position.y, source.position.z, 1.0F});
            if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) ||
                !std::isfinite(transformed.z) || !std::isfinite(transformed.w) ||
                std::abs(transformed.w) <= 1.0e-8F)
            {
                error = "Submitted vertex transform produced an invalid homogeneous position.";
                output = {};
                return false;
            }

            math::Vec3 normal;
            if (!TransformNormal(draw.world, source.normal, normal))
            {
                error = "Submitted instance has a singular world transform or invalid normal.";
                output = {};
                return false;
            }

            SubmittedSceneVertex vertex;
            const float inverseW = 1.0F / transformed.w;
            vertex.position[0] = transformed.x * inverseW;
            vertex.position[1] = transformed.y * inverseW;
            vertex.position[2] = transformed.z * inverseW;
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
            vertex.color[0] = range.material.baseColor.x;
            vertex.color[1] = range.material.baseColor.y;
            vertex.color[2] = range.material.baseColor.z;
            vertex.color[3] = static_cast<float>(range.materialLayer);
            vertex.uv[0] = source.u;
            vertex.uv[1] = source.v;
            vertex.material[0] = range.material.roughness;
            vertex.material[1] = range.material.metallic;
            vertex.material[2] = 1.0F;
            vertex.material[3] = static_cast<float>(materialFlags);
            output.vertices.push_back(vertex);
        }

        for (const std::uint32_t index : draw.mesh->indices)
        {
            if (index >= range.vertexCount)
            {
                error = "Submitted mesh contains an out-of-range index.";
                output = {};
                return false;
            }
            output.indices.push_back(index);
        }
        output.draws.push_back(std::move(range));
    }

    error.clear();
    return true;
}
} // namespace vanta::renderer
