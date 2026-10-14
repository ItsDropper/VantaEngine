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

// Builds one deterministic, merged environment mesh to keep draw calls low.
void BuildDetailedWorld(std::vector<WorldVertex>& vertices, std::vector<unsigned int>& indices);
} // namespace vanta::renderer
