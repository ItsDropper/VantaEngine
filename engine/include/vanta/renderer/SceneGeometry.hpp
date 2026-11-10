#pragma once

#include "vanta/renderer/SceneSubmission.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vanta::renderer
{
struct SubmittedSceneVertex
{
    float position[3]{};
    float normal[3]{};
    float color[4]{};
    float uv[2]{};
    float material[4]{};
    float materialExtra0[4]{}; // emissive RGB, normal scale
    float materialExtra1[4]{}; // AO strength, alpha cutoff, alpha mode, double-sided
};
static_assert(sizeof(SubmittedSceneVertex) == 96);

struct SubmittedDrawRange
{
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialLayer = 0;
    math::Vec3 localBoundsMin{};
    math::Vec3 localBoundsMax{};
    math::Mat4 world = math::Mat4::Identity();
    MaterialDescription material;
};

struct SubmittedSceneBatch
{
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::vector<std::uint32_t> drawIndices;
};

struct SubmittedSceneGeometry
{
    std::vector<SubmittedSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    // One batch per unique mesh/material pair; instances reference draws by stable index.
    std::vector<SubmittedSceneBatch> batches;
    std::vector<SubmittedDrawRange> draws;
    // Texture arrays contain unique texture sets, not one layer per draw call.
    std::vector<MaterialDescription> materialLayers;
};

struct SubmittedInstanceBatchDraw
{
    std::uint32_t batchIndex = 0;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
};

struct SubmittedInstanceStreams
{
    // Each stream reserves element zero for the identity transform.
    std::vector<math::Mat4> shadowTransforms;
    std::vector<math::Mat4> sceneTransforms;
    std::vector<SubmittedInstanceBatchDraw> shadowBatches;
    std::vector<SubmittedInstanceBatchDraw> sceneBatches;
};

struct SubmittedSceneGeometryCache
{
    SubmittedSceneGeometry geometry;
    // Current visible draw sequence; changes do not invalidate immutable geometry.
    std::vector<std::pair<const MeshResource*, const MaterialResource*>> drawKey;
    // Stable unique mesh/material pairs, one per geometry batch/template.
    std::vector<std::pair<const MeshResource*, const MaterialResource*>> geometryKey;
    std::vector<SubmittedDrawRange> geometryTemplates;
    // Kept in the same order as geometry.materialLayers to preserve texture-array indices.
    std::vector<std::string> materialTextureKeys;
    std::uint64_t resourceRevision = 0;
    std::uint64_t revision = 0;
};

// Reuses immutable local-space geometry when only camera/instance transforms change.
[[nodiscard]] bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometryCache& cache, std::string& error);

[[nodiscard]] bool IsSubmittedDrawVisible(
    const SubmittedDrawRange& draw, const math::Mat4& viewProjection) noexcept;

// Builds the two instance streams in their final GPU-buffer order: all shadow
// transforms first, followed by all scene transforms. Scene batch offsets are
// absolute offsets into that concatenated buffer.
[[nodiscard]] bool BuildSubmittedInstanceStreams(
    const SubmittedSceneGeometry& geometry,
    const math::Mat4& shadowViewProjection,
    const math::Mat4& sceneViewProjection,
    SubmittedInstanceStreams& output,
    std::string& error,
    bool includeSceneInstances = true);

[[nodiscard]] bool RebaseSubmittedSceneBatchOffsets(
    std::vector<SubmittedInstanceBatchDraw>& sceneBatches,
    std::size_t sourceSceneStreamBase,
    std::size_t destinationSceneStreamBase,
    std::size_t sceneStreamSize,
    std::string& error);

// Builds backend-neutral local-space geometry and per-instance draw transforms.
// The caller owns the resulting arrays until its GPU upload/fence lifecycle ends.
[[nodiscard]] bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error);
} // namespace vanta::renderer
