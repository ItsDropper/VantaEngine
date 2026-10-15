#pragma once

#include <vector>

namespace vanta::renderer
{
struct WorldVertex
{
    float position[3];
    float normal[3];
    float color[4];
    float uv[2];
};

// Shared deterministic terrain function used by both render backends and all world props.
float TerrainHeightAt(float x, float z);

// Builds one deterministic, merged environment mesh to keep draw calls low.
void BuildDetailedWorld(std::vector<WorldVertex>& vertices, std::vector<unsigned int>& indices);
} // namespace vanta::renderer
