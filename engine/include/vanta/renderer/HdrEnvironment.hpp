#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vanta::renderer
{
struct EnvironmentRadiance
{
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
};

struct HdrEnvironmentImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<EnvironmentRadiance> pixels;

    [[nodiscard]] bool IsValid() const noexcept;
};

enum class EnvironmentCubeFace : std::uint8_t
{
    PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ
};

struct EnvironmentCubeMip
{
    std::uint32_t size = 0;
    std::array<std::vector<EnvironmentRadiance>, 6> faces;
};

struct PrefilteredEnvironment
{
    EnvironmentCubeMip diffuseIrradiance;
    std::vector<EnvironmentCubeMip> specularMips;
};

struct EnvironmentPrefilterOptions
{
    std::uint32_t cubeSize = 128;
    std::uint32_t diffuseSize = 32;
    std::uint32_t specularSampleCount = 256;
    std::uint32_t diffuseSampleCount = 256;
};

[[nodiscard]] bool DecodeRadianceHdr(std::span<const std::uint8_t> encoded,
    HdrEnvironmentImage& output, std::string& error);
[[nodiscard]] bool BuildPrefilteredEnvironment(const HdrEnvironmentImage& source,
    const EnvironmentPrefilterOptions& options, PrefilteredEnvironment& output,
    std::string& error);
[[nodiscard]] EnvironmentRadiance SampleEquirectangular(const HdrEnvironmentImage& image,
    float directionX, float directionY, float directionZ) noexcept;
[[nodiscard]] bool ValidatePrefilteredEnvironment(const PrefilteredEnvironment& environment,
    std::string& error) noexcept;
} // namespace vanta::renderer
