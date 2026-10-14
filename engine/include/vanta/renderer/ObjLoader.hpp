#pragma once

#include "vanta/math/Math.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vanta::renderer
{
struct ModelVertex
{
    math::Vec3 position;
    math::Vec3 normal;
    float u = 0.0F;
    float v = 0.0F;
};

struct CpuModel
{
    std::vector<ModelVertex> vertices;
    std::vector<unsigned int> indices;
    std::filesystem::path diffuseTexture;
};

[[nodiscard]] bool LoadWavefrontObj(const std::filesystem::path& path, CpuModel& output, std::string& error);
} // namespace vanta::renderer
