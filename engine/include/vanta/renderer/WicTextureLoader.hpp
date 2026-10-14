#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace vanta::renderer
{
struct CpuTexture
{
    UINT width = 0;
    UINT height = 0;
    std::vector<std::uint8_t> rgba;
};

[[nodiscard]] bool LoadWicTexture(const std::filesystem::path& path, CpuTexture& output);
} // namespace vanta::renderer
