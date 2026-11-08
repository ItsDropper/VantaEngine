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
};
static_assert(sizeof(SubmittedSceneVertex) == 64);

struct SubmittedDrawRange
{
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialLayer = 0;
    math::Mat4 world = math::Mat4::Identity();
    MaterialDescription material;
};

struct SubmittedSceneGeometry
{
    std::vector<SubmittedSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubmittedDrawRange> draws;
    // Texture arrays contain unique texture sets, not one layer per draw call.
    std::vector<MaterialDescription> materialLayers;
};

struct SubmittedSceneGeometryCache
{
    SubmittedSceneGeometry geometry;
    std::vector<std::pair<const MeshResource*, const MaterialResource*>> drawKey;
    std::uint64_t revision = 0;
};

// Reuses immutable local-space geometry when only camera/instance transforms change.
[[nodiscard]] bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometryCache& cache, std::string& error);

// Builds backend-neutral local-space geometry and per-instance draw transforms.
// The caller owns the resulting arrays until its GPU upload/fence lifecycle ends.
[[nodiscard]] bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error);
} // namespace vanta::renderer
