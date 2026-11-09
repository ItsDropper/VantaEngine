#pragma once

#include "vanta/renderer/SceneSubmission.hpp"

#include <array>
#include <cstddef>

namespace vanta::renderer
{
// Nine real spherical-harmonic coefficients (L0-L2), RGB in xyz and zero in w.
// The first set is cosine-convolved diffuse irradiance; the remaining sets are
// progressively broader GGX-inspired specular prefilters for roughness 0..1.
struct EnvironmentLighting
{
    static constexpr std::size_t kCoefficientCount = 9;
    static constexpr std::size_t kSpecularLevels = 5;
    std::array<std::array<float, 4>, kCoefficientCount> diffuse{};
    std::array<std::array<std::array<float, 4>, kCoefficientCount>, kSpecularLevels> specular{};
};

[[nodiscard]] EnvironmentLighting BuildEnvironmentLighting(const EnvironmentSettings& settings) noexcept;
}
