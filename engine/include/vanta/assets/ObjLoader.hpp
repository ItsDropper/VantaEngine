#pragma once

#include "vanta/math/Math.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vanta::assets
{
struct ModelVertex
{
    math::Vec3 position;
    math::Vec3 normal;
    float u = 0.0F;
    float v = 0.0F;
};

struct CpuEmbeddedImage
{
    std::vector<std::uint8_t> encodedBytes;
    std::string mimeType;
};

struct CpuMaterial
{
    std::string name;
    std::shared_ptr<const CpuEmbeddedImage> embeddedDiffuseTexture;
    std::shared_ptr<const CpuEmbeddedImage> embeddedNormalTexture;
    std::shared_ptr<const CpuEmbeddedImage> embeddedRoughnessTexture;
    std::shared_ptr<const CpuEmbeddedImage> embeddedMetallicTexture;
    std::shared_ptr<const CpuEmbeddedImage> embeddedAmbientOcclusionTexture;
    std::filesystem::path diffuseTexture;
    std::filesystem::path normalTexture;
    std::filesystem::path roughnessTexture;
    std::filesystem::path metallicTexture;
    std::filesystem::path ambientOcclusionTexture;
    math::Vec3 baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.8F;
    float metallic = 0.0F;
    bool packedMetallicRoughness = false;
};

struct CpuSubmesh
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
};

struct CpuModel
{
    std::vector<ModelVertex> vertices;
    std::vector<unsigned int> indices;
    std::filesystem::path diffuseTexture;
    std::filesystem::path normalTexture;
    std::filesystem::path roughnessTexture;
    std::filesystem::path metallicTexture;
    std::filesystem::path ambientOcclusionTexture;
    math::Vec3 baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.8F;
    float metallic = 0.0F;
    std::vector<std::filesystem::path> materialLibraries;
    std::vector<CpuMaterial> materials;
    std::vector<CpuSubmesh> submeshes;
};

[[nodiscard]] bool LoadWavefrontObj(const std::filesystem::path& path, CpuModel& output, std::string& error);
} // namespace vanta::assets
