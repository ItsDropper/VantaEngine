#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vanta::assets
{
struct CpuTexture
{
    UINT width = 0;
    UINT height = 0;
    std::vector<std::uint8_t> rgba;
};

[[nodiscard]] bool LoadWicTexture(const std::filesystem::path& path, CpuTexture& output);
[[nodiscard]] bool LoadWicTexture(std::span<const std::uint8_t> encodedBytes,
                                  CpuTexture& output, std::string& error);
} // namespace vanta::assets
