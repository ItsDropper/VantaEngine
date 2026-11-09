#include "vanta/renderer/SceneGeometry.hpp"

#include <algorithm>
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
bool IsValidSubmittedWorldTransform(const math::Mat4& world) noexcept
{
    for (const float value : world.elements)
    {
        if (!std::isfinite(value))
            return false;
    }
    const float a = world.At(0, 0), b = world.At(0, 1), c = world.At(0, 2);
    const float d = world.At(1, 0), e = world.At(1, 1), f = world.At(1, 2);
    const float g = world.At(2, 0), h = world.At(2, 1), i = world.At(2, 2);
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) +
        c * (d * h - e * g);
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-8F &&
        std::abs(world.At(3, 0)) <= 1.0e-6F &&
        std::abs(world.At(3, 1)) <= 1.0e-6F &&
        std::abs(world.At(3, 2)) <= 1.0e-6F &&
        std::abs(world.At(3, 3) - 1.0F) <= 1.0e-6F;
}

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

bool IsSubmittedDrawVisible(
    const SubmittedDrawRange& draw, const math::Mat4& viewProjection) noexcept
{
    // Transform the local AABB into clip space as a center plus three half-axis
    // vectors. Testing its support radius against each clip plane avoids the
    // previous eight corner transforms and six per-corner rejection reductions.
    const math::Mat4 clip = viewProjection * draw.world;
    const float centerX = (draw.localBoundsMin.x + draw.localBoundsMax.x) * 0.5F;
    const float centerY = (draw.localBoundsMin.y + draw.localBoundsMax.y) * 0.5F;
    const float centerZ = (draw.localBoundsMin.z + draw.localBoundsMax.z) * 0.5F;
    const float extentX = (draw.localBoundsMax.x - draw.localBoundsMin.x) * 0.5F;
    const float extentY = (draw.localBoundsMax.y - draw.localBoundsMin.y) * 0.5F;
    const float extentZ = (draw.localBoundsMax.z - draw.localBoundsMin.z) * 0.5F;

    const math::Vec4 center = clip.Transform({centerX, centerY, centerZ, 1.0F});
    if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(center.z) || !std::isfinite(center.w) ||
        !std::isfinite(extentX) || !std::isfinite(extentY) || !std::isfinite(extentZ))
        return true;

    const auto rejectPlane = [&](const float a, const float b, const float c, const float d) noexcept
    {
        const float value = a * center.x + b * center.y + c * center.z + d * center.w;
        const float radius =
            std::abs(a * clip.At(0, 0) + b * clip.At(1, 0) + c * clip.At(2, 0) + d * clip.At(3, 0)) * extentX +
            std::abs(a * clip.At(0, 1) + b * clip.At(1, 1) + c * clip.At(2, 1) + d * clip.At(3, 1)) * extentY +
            std::abs(a * clip.At(0, 2) + b * clip.At(1, 2) + c * clip.At(2, 2) + d * clip.At(3, 2)) * extentZ;
        return std::isfinite(value) && std::isfinite(radius) && value + radius < 0.0F;
    };

    // D3D clip volume: -w<=x<=w, -w<=y<=w, 0<=z<=w.
    return !(rejectPlane(1.0F, 0.0F, 0.0F, 1.0F) ||
        rejectPlane(-1.0F, 0.0F, 0.0F, 1.0F) ||
        rejectPlane(0.0F, 1.0F, 0.0F, 1.0F) ||
        rejectPlane(0.0F, -1.0F, 0.0F, 1.0F) ||
        rejectPlane(0.0F, 0.0F, 1.0F, 0.0F) ||
        rejectPlane(0.0F, 0.0F, -1.0F, 1.0F));
}

bool BuildSubmittedInstanceStreams(
    const SubmittedSceneGeometry& geometry,
    const math::Mat4& shadowViewProjection,
    const math::Mat4& sceneViewProjection,
    SubmittedInstanceStreams& output,
    std::string& error,
    const bool includeSceneInstances)
{
    output = {};
    output.shadowTransforms.reserve(geometry.draws.size() + 1U);
    output.shadowTransforms.push_back(math::Mat4::Identity());
    if (includeSceneInstances) {
        output.sceneTransforms.reserve(geometry.draws.size() + 1U);
        output.sceneTransforms.push_back(math::Mat4::Identity());
    }
    output.shadowBatches.reserve(geometry.batches.size());
    if (includeSceneInstances)
        output.sceneBatches.reserve(geometry.batches.size());

    // The GPU buffer stores the complete shadow stream before the scene stream.
    // Finish the shadow stream before calculating any scene batch's absolute offset.
    for (std::size_t batchIndex = 0; batchIndex < geometry.batches.size(); ++batchIndex) {
        const SubmittedSceneBatch& batch = geometry.batches[batchIndex];
        const std::size_t firstInstance = output.shadowTransforms.size();
        for (const std::uint32_t drawIndex : batch.drawIndices) {
            if (drawIndex >= geometry.draws.size()) {
                error = "Submitted scene batch references a missing draw while building shadow instances.";
                output = {};
                return false;
            }
            const SubmittedDrawRange& draw = geometry.draws[drawIndex];
            if (IsSubmittedDrawVisible(draw, shadowViewProjection))
                output.shadowTransforms.push_back(draw.world);
        }
        const std::size_t instanceCount = output.shadowTransforms.size() - firstInstance;
        if (instanceCount != 0U) {
            if (firstInstance > std::numeric_limits<std::uint32_t>::max() ||
                instanceCount > std::numeric_limits<std::uint32_t>::max()) {
                error = "Submitted shadow instance stream exceeds 32-bit batch limits.";
                output = {};
                return false;
            }
            output.shadowBatches.push_back({
                static_cast<std::uint32_t>(batchIndex),
                static_cast<std::uint32_t>(firstInstance),
                static_cast<std::uint32_t>(instanceCount)
            });
        }
    }

    if (includeSceneInstances) {
        const std::size_t sceneStreamBase = output.shadowTransforms.size();
        for (std::size_t batchIndex = 0; batchIndex < geometry.batches.size(); ++batchIndex) {
            const SubmittedSceneBatch& batch = geometry.batches[batchIndex];
            const std::size_t firstSceneInstance = output.sceneTransforms.size();
            for (const std::uint32_t drawIndex : batch.drawIndices) {
                if (drawIndex >= geometry.draws.size()) {
                    error = "Submitted scene batch references a missing draw while building color instances.";
                    output = {};
                    return false;
                }
                const SubmittedDrawRange& draw = geometry.draws[drawIndex];
                if (IsSubmittedDrawVisible(draw, sceneViewProjection))
                    output.sceneTransforms.push_back(draw.world);
            }
            const std::size_t instanceCount = output.sceneTransforms.size() - firstSceneInstance;
            if (instanceCount != 0U) {
                const std::size_t absoluteFirstInstance = sceneStreamBase + firstSceneInstance;
                if (absoluteFirstInstance > std::numeric_limits<std::uint32_t>::max() ||
                    instanceCount > std::numeric_limits<std::uint32_t>::max()) {
                    error = "Submitted scene instance stream exceeds 32-bit batch limits.";
                    output = {};
                    return false;
                }
                output.sceneBatches.push_back({
                    static_cast<std::uint32_t>(batchIndex),
                    static_cast<std::uint32_t>(absoluteFirstInstance),
                    static_cast<std::uint32_t>(instanceCount)
                });
            }
        }
    }

    error.clear();
    return true;
}

bool RebaseSubmittedSceneBatchOffsets(
    std::vector<SubmittedInstanceBatchDraw>& sceneBatches,
    const std::size_t sourceSceneStreamBase,
    const std::size_t destinationSceneStreamBase,
    const std::size_t sceneStreamSize,
    std::string& error)
{
    for (SubmittedInstanceBatchDraw& batch : sceneBatches) {
        if (batch.firstInstance < sourceSceneStreamBase) {
            error = "Submitted scene batch starts before its source scene-instance stream.";
            return false;
        }
        const std::size_t relativeFirst = static_cast<std::size_t>(batch.firstInstance) -
            sourceSceneStreamBase;
        if (relativeFirst > sceneStreamSize || batch.instanceCount > sceneStreamSize - relativeFirst) {
            error = "Submitted scene batch exceeds its source scene-instance stream.";
            return false;
        }
        if (relativeFirst > std::numeric_limits<std::uint32_t>::max() ||
            destinationSceneStreamBase > std::numeric_limits<std::uint32_t>::max() - relativeFirst) {
            error = "Rebased submitted scene batch offset exceeds 32-bit limits.";
            return false;
        }
        batch.firstInstance = static_cast<std::uint32_t>(destinationSceneStreamBase + relativeFirst);
    }
    error.clear();
    return true;
}

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

    struct GeometryRange
    {
        std::uint32_t firstVertex = 0;
        std::uint32_t firstIndex = 0;
        std::uint32_t batchIndex = 0;
    };
    std::unordered_map<GeometryKey, GeometryRange, GeometryKeyHash> geometryRanges;
    std::unordered_map<const MeshResource*, std::pair<math::Vec3, math::Vec3>> meshBounds;
    std::unordered_map<std::string, std::uint32_t> materialLayerByTextureSet;
    geometryRanges.reserve(frame.draws.size());
    meshBounds.reserve(frame.draws.size());
    materialLayerByTextureSet.reserve(frame.draws.size());
    output.draws.reserve(frame.draws.size());
    output.batches.reserve(frame.draws.size());

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

        auto bounds = meshBounds.find(draw.mesh.get());
        if (bounds == meshBounds.end())
        {
            math::Vec3 minimum{
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
            math::Vec3 maximum{
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
            for (const assets::ModelVertex& vertex : draw.mesh->vertices)
            {
                minimum.x = std::min(minimum.x, vertex.position.x);
                minimum.y = std::min(minimum.y, vertex.position.y);
                minimum.z = std::min(minimum.z, vertex.position.z);
                maximum.x = std::max(maximum.x, vertex.position.x);
                maximum.y = std::max(maximum.y, vertex.position.y);
                maximum.z = std::max(maximum.z, vertex.position.z);
            }
            bounds = meshBounds.emplace(draw.mesh.get(), std::pair{minimum, maximum}).first;
        }
        range.localBoundsMin = bounds->second.first;
        range.localBoundsMax = bounds->second.second;

        const GeometryKey geometryKey{draw.mesh.get(), draw.material.get()};
        const auto cachedGeometry = geometryRanges.find(geometryKey);
        if (cachedGeometry != geometryRanges.end())
        {
            range.firstVertex = cachedGeometry->second.firstVertex;
            range.vertexCount = static_cast<std::uint32_t>(draw.mesh->vertices.size());
            range.firstIndex = cachedGeometry->second.firstIndex;
            range.indexCount = static_cast<std::uint32_t>(draw.mesh->indices.size());
            const std::uint32_t drawIndex = static_cast<std::uint32_t>(output.draws.size());
            output.draws.push_back(std::move(range));
            output.batches[cachedGeometry->second.batchIndex].drawIndices.push_back(drawIndex);
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

        const std::uint32_t drawIndex = static_cast<std::uint32_t>(output.draws.size());
        SubmittedSceneBatch batch;
        batch.firstVertex = range.firstVertex;
        batch.vertexCount = range.vertexCount;
        batch.firstIndex = range.firstIndex;
        batch.indexCount = range.indexCount;
        batch.drawIndices.push_back(drawIndex);
        const std::uint32_t batchIndex = static_cast<std::uint32_t>(output.batches.size());
        output.batches.push_back(std::move(batch));
        geometryRanges.emplace(geometryKey,
            GeometryRange{range.firstVertex, range.firstIndex, batchIndex});
        output.draws.push_back(std::move(range));
    }

    error.clear();
    return true;
}

bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometryCache& cache, std::string& error)
{
    using GeometryKey = std::pair<const MeshResource*, const MaterialResource*>;
    struct GeometryKeyHash
    {
        std::size_t operator()(const GeometryKey& key) const noexcept
        {
            const std::size_t meshHash = std::hash<const void*>{}(key.first);
            const std::size_t materialHash = std::hash<const void*>{}(key.second);
            return meshHash ^ (materialHash + 0x9e3779b9U + (meshHash << 6U) + (meshHash >> 2U));
        }
    };

    const bool resourceGenerationChanged = cache.resourceRevision != frame.resourceRevision;
    if (resourceGenerationChanged)
    {
        cache.geometry = {};
        cache.drawKey.clear();
        cache.geometryKey.clear();
        cache.geometryTemplates.clear();
        cache.materialTextureKeys.clear();
        cache.resourceRevision = frame.resourceRevision;
    }

    if (frame.draws.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Submitted scene exceeds the 32-bit draw range limits.";
        return false;
    }

    // The common case changes only instance transforms. Compare resource pointers
    // directly against the cached key instead of allocating and filling a temporary
    // vector of 8624 mesh/material pairs on every frame.
    bool cachedDrawSequenceMatches = !resourceGenerationChanged &&
        cache.drawKey.size() == frame.draws.size() &&
        cache.geometry.draws.size() == frame.draws.size();
    for (std::size_t index = 0; index < frame.draws.size(); ++index)
    {
        const DrawItem& draw = frame.draws[index];
        if (!draw.mesh || !draw.material)
        {
            error = "Scene frame contains a draw with a missing mesh or material.";
            return false;
        }
        if (cachedDrawSequenceMatches &&
            (cache.drawKey[index].first != draw.mesh.get() ||
             cache.drawKey[index].second != draw.material.get()))
            cachedDrawSequenceMatches = false;
    }

    if (cachedDrawSequenceMatches)
    {
        for (std::size_t index = 0; index < frame.draws.size(); ++index)
        {
            const math::Mat4& world = frame.draws[index].world;
            if (!IsValidSubmittedWorldTransform(world))
            {
                error = "Submitted instance has a singular, non-finite, or non-affine world transform.";
                return false;
            }
            cache.geometry.draws[index].world = world;
        }
        error.clear();
        return true;
    }

    std::vector<GeometryKey> drawKey;
    drawKey.reserve(frame.draws.size());
    for (const DrawItem& draw : frame.draws)
    {
        if (!IsValidSubmittedWorldTransform(draw.world))
        {
            error = "Submitted instance has a singular, non-finite, or non-affine world transform.";
            return false;
        }
        drawKey.emplace_back(draw.mesh.get(), draw.material.get());
    }

    const std::size_t pendingReserve = std::min<std::size_t>(frame.draws.size(), 128U);
    std::unordered_map<GeometryKey, std::uint32_t, GeometryKeyHash> geometryIndexByKey;
    geometryIndexByKey.reserve(cache.geometryKey.size() + pendingReserve);
    for (std::size_t index = 0; index < cache.geometryKey.size(); ++index)
        geometryIndexByKey.emplace(cache.geometryKey[index], static_cast<std::uint32_t>(index));

    struct PendingGeometry
    {
        GeometryKey key{};
        SubmittedSceneGeometry geometry;
    };
    std::vector<PendingGeometry> pendingGeometry;
    pendingGeometry.reserve(pendingReserve);
    std::unordered_map<GeometryKey, std::size_t, GeometryKeyHash> pendingIndexByKey;
    pendingIndexByKey.reserve(pendingReserve);

    for (const DrawItem& draw : frame.draws)
    {
        const GeometryKey key{draw.mesh.get(), draw.material.get()};
        if (geometryIndexByKey.contains(key) || pendingIndexByKey.contains(key))
            continue;

        SceneFrame singleDrawFrame;
        singleDrawFrame.view = frame.view;
        singleDrawFrame.resourceRevision = frame.resourceRevision;
        singleDrawFrame.draws.push_back(draw);
        SubmittedSceneGeometry singleDrawGeometry;
        if (!BuildSubmittedSceneGeometry(singleDrawFrame, singleDrawGeometry, error))
            return false;
        if (singleDrawGeometry.draws.size() != 1U || singleDrawGeometry.batches.size() != 1U ||
            singleDrawGeometry.materialLayers.size() != 1U)
        {
            error = "Single submitted geometry cache entry did not produce exactly one draw, batch, and material layer.";
            return false;
        }
        pendingIndexByKey.emplace(key, pendingGeometry.size());
        pendingGeometry.push_back({key, std::move(singleDrawGeometry)});
    }

    std::uint64_t totalVertexCount = cache.geometry.vertices.size();
    std::uint64_t totalIndexCount = cache.geometry.indices.size();
    for (const PendingGeometry& pending : pendingGeometry)
    {
        if (pending.geometry.vertices.size() > std::numeric_limits<std::uint32_t>::max() - totalVertexCount ||
            pending.geometry.indices.size() > std::numeric_limits<std::uint32_t>::max() - totalIndexCount)
        {
            error = "Submitted scene exceeds the 32-bit draw range limits.";
            return false;
        }
        totalVertexCount += pending.geometry.vertices.size();
        totalIndexCount += pending.geometry.indices.size();
    }

    std::unordered_map<std::string, std::uint32_t> materialLayerByTextureKey;
    materialLayerByTextureKey.reserve(cache.materialTextureKeys.size() + pendingReserve);
    for (std::size_t index = 0; index < cache.materialTextureKeys.size(); ++index)
        materialLayerByTextureKey.emplace(cache.materialTextureKeys[index], static_cast<std::uint32_t>(index));

    for (PendingGeometry& pending : pendingGeometry)
    {
        const SubmittedDrawRange& sourceRange = pending.geometry.draws.front();
        const std::string textureKey = MaterialTextureKey(sourceRange.material);
        auto layer = materialLayerByTextureKey.find(textureKey);
        std::uint32_t materialLayer = 0U;
        if (layer == materialLayerByTextureKey.end())
        {
            if (cache.geometry.materialLayers.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                error = "Submitted scene exceeds the 32-bit material layer limit.";
                return false;
            }
            materialLayer = static_cast<std::uint32_t>(cache.geometry.materialLayers.size());
            cache.geometry.materialLayers.push_back(sourceRange.material);
            cache.materialTextureKeys.push_back(textureKey);
            materialLayerByTextureKey.emplace(textureKey, materialLayer);
        }
        else
        {
            materialLayer = layer->second;
        }

        const std::uint32_t firstVertex = static_cast<std::uint32_t>(cache.geometry.vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(cache.geometry.indices.size());
        for (SubmittedSceneVertex vertex : pending.geometry.vertices)
        {
            vertex.color[3] = static_cast<float>(materialLayer);
            cache.geometry.vertices.push_back(vertex);
        }
        cache.geometry.indices.insert(cache.geometry.indices.end(),
            pending.geometry.indices.begin(), pending.geometry.indices.end());

        SubmittedDrawRange geometryTemplate = sourceRange;
        geometryTemplate.firstVertex = firstVertex;
        geometryTemplate.firstIndex = firstIndex;
        geometryTemplate.materialLayer = materialLayer;
        geometryTemplate.world = math::Mat4::Identity();
        SubmittedSceneBatch batch;
        batch.firstVertex = firstVertex;
        batch.vertexCount = geometryTemplate.vertexCount;
        batch.firstIndex = firstIndex;
        batch.indexCount = geometryTemplate.indexCount;
        const std::uint32_t batchIndex = static_cast<std::uint32_t>(cache.geometry.batches.size());
        cache.geometry.batches.push_back(std::move(batch));
        cache.geometryKey.push_back(pending.key);
        cache.geometryTemplates.push_back(std::move(geometryTemplate));
        geometryIndexByKey.emplace(pending.key, batchIndex);
    }

    for (SubmittedSceneBatch& batch : cache.geometry.batches)
        batch.drawIndices.clear();
    cache.geometry.draws.clear();
    cache.geometry.draws.reserve(frame.draws.size());

    for (std::size_t index = 0; index < frame.draws.size(); ++index)
    {
        const DrawItem& draw = frame.draws[index];
        const GeometryKey key{draw.mesh.get(), draw.material.get()};
        const auto geometry = geometryIndexByKey.find(key);
        if (geometry == geometryIndexByKey.end())
        {
            error = "Submitted scene cache lost a mesh/material geometry entry.";
            return false;
        }
        const std::uint32_t batchIndex = geometry->second;
        SubmittedDrawRange range = cache.geometryTemplates[batchIndex];
        range.world = draw.world;
        const std::uint32_t drawIndex = static_cast<std::uint32_t>(cache.geometry.draws.size());
        cache.geometry.draws.push_back(std::move(range));
        cache.geometry.batches[batchIndex].drawIndices.push_back(drawIndex);
    }

    cache.drawKey = std::move(drawKey);
    if (resourceGenerationChanged || !pendingGeometry.empty())
    {
        ++cache.revision;
        if (cache.revision == 0U)
            ++cache.revision;
    }
    error.clear();
    return true;
}
} // namespace vanta::renderer
