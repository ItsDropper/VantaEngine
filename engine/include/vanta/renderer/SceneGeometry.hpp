#pragma once

#include "vanta/renderer/SceneSubmission.hpp"

#include <cstdint>
#include <string>
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
    MaterialDescription material;
};

struct SubmittedSceneGeometry
{
    std::vector<SubmittedSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SubmittedDrawRange> draws;
};

// Flattens a stable SceneFrame into backend-neutral, world-space vertices.
// The caller owns the resulting arrays until its GPU upload/fence lifecycle ends.
[[nodiscard]] bool BuildSubmittedSceneGeometry(
    const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error);
} // namespace vanta::renderer
