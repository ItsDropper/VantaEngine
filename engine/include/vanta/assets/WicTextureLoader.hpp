#pragma once

#include "vanta/assets/CpuTexture.hpp"

#include <filesystem>
#include <span>
#include <string>

namespace vanta::assets
{
[[nodiscard]] bool LoadWicTexture(const std::filesystem::path& path, CpuTexture& output);
[[nodiscard]] bool LoadWicTexture(std::span<const std::uint8_t> encodedBytes,
                                  CpuTexture& output, std::string& error);
} // namespace vanta::assets
