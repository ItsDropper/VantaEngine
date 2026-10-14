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
    std::filesystem::path normalTexture;
    std::filesystem::path roughnessTexture;
    std::filesystem::path metallicTexture;
    math::Vec3 baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.8F;
    float metallic = 0.0F;
};

[[nodiscard]] bool LoadWavefrontObj(const std::filesystem::path& path, CpuModel& output, std::string& error);
} // namespace vanta::renderer
