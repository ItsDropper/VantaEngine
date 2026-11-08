#include "vanta/renderer/SceneGeometry.hpp"

#include <array>

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

} // namespace

bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error)
{
    output = {};

    struct GeometryKey
    {
        const MeshResource* mesh = nullptr;
        const MaterialResource* material = nullptr;
        bool operator==(const GeometryKey&) const noexcept = default;
    };
    struct GeometryKeyHash
    {
        std::size_t operator()(const GeometryKey& key) const noexcept
        {
            const std::size_t meshHash = std::hash<const void*>{}(key.mesh);
            const std::size_t materialHash = std::hash<const void*>{}(key.material);
            return meshHash ^ (materialHash + 0x9e3779b9U + (meshHash << 6U) + (meshHash >> 2U));
        }
    };

    std::unordered_map<GeometryKey, std::pair<std::uint32_t, std::uint32_t>, GeometryKeyHash> geometryRanges;
    std::unordered_map<std::string, std::uint32_t> materialLayerByTextureSet;
    geometryRanges.reserve(frame.draws.size());
    materialLayerByTextureSet.reserve(frame.draws.size());
    output.draws.reserve(frame.draws.size());

    for (const DrawItem& draw : frame.draws)
    {
        if (!draw.mesh || !draw.material)
        {
            error = "Scene frame contains a draw with a missing mesh or material.";
            output = {};
            return false;
        }
        if (draw.mesh->vertices.empty() || draw.mesh->indices.empty() ||
            draw.mesh->indices.size() % 3U != 0U)
        {
            error = "Scene frame contains invalid indexed triangle geometry.";
            output = {};
            return false;
        }

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (!std::isfinite(draw.world.At(row, column)))
                {
                    error = "Submitted instance has a non-finite world transform.";
                    output = {};
                    return false;
                }
            }
        }

        const math::Mat4& world = draw.world;
        const float determinant =
            world.At(0, 0) * (world.At(1, 1) * world.At(2, 2) - world.At(1, 2) * world.At(2, 1)) -
            world.At(0, 1) * (world.At(1, 0) * world.At(2, 2) - world.At(1, 2) * world.At(2, 0)) +
            world.At(0, 2) * (world.At(1, 0) * world.At(2, 1) - world.At(1, 1) * world.At(2, 0));
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8F ||
            std::abs(world.At(3, 0)) > 1.0e-6F || std::abs(world.At(3, 1)) > 1.0e-6F ||
            std::abs(world.At(3, 2)) > 1.0e-6F || std::abs(world.At(3, 3) - 1.0F) > 1.0e-6F)
        {
            error = "Submitted instance has a singular or non-affine world transform.";
            output = {};
            return false;
        }

        SubmittedDrawRange range;
        range.world = world;
        range.material = draw.material->description;

        const std::string textureSetKey = MaterialTextureKey(range.material);
        const auto [layerIt, inserted] = materialLayerByTextureSet.emplace(
            textureSetKey, static_cast<std::uint32_t>(output.materialLayers.size()));
        if (inserted)
            output.materialLayers.push_back(range.material);
        range.materialLayer = layerIt->second;

        const GeometryKey geometryKey{draw.mesh.get(), draw.material.get()};
        const auto cachedGeometry = geometryRanges.find(geometryKey);
        if (cachedGeometry != geometryRanges.end())
        {
            range.firstVertex = cachedGeometry->second.first;
            range.vertexCount = static_cast<std::uint32_t>(draw.mesh->vertices.size());
            range.firstIndex = cachedGeometry->second.second;
            range.indexCount = static_cast<std::uint32_t>(draw.mesh->indices.size());
            output.draws.push_back(std::move(range));
            continue;
        }

        if (draw.mesh->vertices.size() > std::numeric_limits<std::uint32_t>::max() - output.vertices.size() ||
            draw.mesh->indices.size() > std::numeric_limits<std::uint32_t>::max() - output.indices.size())
        {
            error = "Submitted scene exceeds the 32-bit draw range limits.";
            output = {};
            return false;
        }

        range.firstVertex = static_cast<std::uint32_t>(output.vertices.size());
        range.vertexCount = static_cast<std::uint32_t>(draw.mesh->vertices.size());
        range.firstIndex = static_cast<std::uint32_t>(output.indices.size());
        range.indexCount = static_cast<std::uint32_t>(draw.mesh->indices.size());

        std::uint32_t materialFlags = 0U;
        if (!range.material.baseColorTexture.empty()) materialFlags |= 1U;
        if (!range.material.normalTexture.empty()) materialFlags |= 2U;
        if (!range.material.roughnessTexture.empty()) materialFlags |= 4U;
        if (!range.material.metallicTexture.empty()) materialFlags |= 8U;
        if (!range.material.ambientOcclusionTexture.empty()) materialFlags |= 16U;
        if (range.material.packedMetallicRoughness) materialFlags |= 32U;
        if (range.material.terrain) materialFlags |= 64U;
        if (materialFlags != 0U) materialFlags |= 128U;
        if (range.material.snowSurface) materialFlags |= 256U;

        for (const assets::ModelVertex& source : draw.mesh->vertices)
        {
            const float attributes[] = {
                source.position.x, source.position.y, source.position.z,
                source.normal.x, source.normal.y, source.normal.z, source.u, source.v
            };
            for (const float attribute : attributes)
            {
                if (!std::isfinite(attribute))
                {
                    error = "Submitted mesh contains a non-finite vertex attribute.";
                    output = {};
                    return false;
                }
            }

            SubmittedSceneVertex vertex;
            vertex.position[0] = source.position.x;
            vertex.position[1] = source.position.y;
            vertex.position[2] = source.position.z;
            vertex.normal[0] = source.normal.x;
            vertex.normal[1] = source.normal.y;
            vertex.normal[2] = source.normal.z;
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

        geometryRanges.emplace(geometryKey, std::pair{range.firstVertex, range.firstIndex});
        output.draws.push_back(std::move(range));
    }

    error.clear();
    return true;
}

bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometryCache& cache, std::string& error)
{
    std::vector<std::pair<const MeshResource*, const MaterialResource*>> key;
    key.reserve(frame.draws.size());
    for (const DrawItem& draw : frame.draws) {
        if (!draw.mesh || !draw.material) {
            error = "Scene frame contains a draw with a missing mesh or material.";
            return false;
        }
        key.emplace_back(draw.mesh.get(), draw.material.get());
    }

    if (key == cache.drawKey && cache.geometry.draws.size() == frame.draws.size()) {
        for (std::size_t index = 0; index < frame.draws.size(); ++index) {
            const math::Mat4& world = frame.draws[index].world;
            for (const float value : world.elements) {
                if (!std::isfinite(value)) {
                    error = "Submitted instance has a non-finite world transform.";
                    return false;
                }
            }
            const float a = world.At(0, 0), b = world.At(0, 1), c = world.At(0, 2);
            const float d = world.At(1, 0), e = world.At(1, 1), f = world.At(1, 2);
            const float g = world.At(2, 0), h = world.At(2, 1), i = world.At(2, 2);
            const float determinant = a * (e * i - f * h) - b * (d * i - f * g) +
                c * (d * h - e * g);
            if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8F ||
                std::abs(world.At(3, 0)) > 1.0e-6F ||
                std::abs(world.At(3, 1)) > 1.0e-6F ||
                std::abs(world.At(3, 2)) > 1.0e-6F ||
                std::abs(world.At(3, 3) - 1.0F) > 1.0e-6F) {
                error = "Submitted instance has a singular or non-affine world transform.";
                return false;
            }
            cache.geometry.draws[index].world = world;
        }
        error.clear();
        return true;
    }

    if (!BuildSubmittedSceneGeometry(frame, cache.geometry, error))
        return false;
    cache.drawKey = std::move(key);
    ++cache.revision;
    if (cache.revision == 0U)
        ++cache.revision;
    return true;
}
} // namespace vanta::renderer
