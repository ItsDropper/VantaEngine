#pragma once

#include "vanta/assets/WicTextureLoader.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace vanta::assets
{
enum class TextureColorSpace : std::uint32_t
{
    Linear = 0,
    Srgb = 1
};

struct TextureMip final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

struct ProcessedTexture final
{
    TextureColorSpace colorSpace = TextureColorSpace::Srgb;
    std::vector<TextureMip> mipChain;
};

// Builds a complete box-filtered mip chain. sRGB RGB is averaged in linear
// light; alpha and linear data maps are averaged without gamma conversion.
[[nodiscard]] bool BuildTextureMipChain(const CpuTexture& source,
                                        TextureColorSpace colorSpace,
                                        std::vector<TextureMip>& output,
                                        std::string& error);
// Builds tangent-space normal mips by averaging decoded unit vectors and renormalizing.
// Alpha is filtered linearly. Output is unchanged on failure.
[[nodiscard]] bool BuildNormalMapMipChain(const CpuTexture& source,
                                          std::vector<TextureMip>& output,
                                          std::string& error);

// Reads and validates the complete TVTTEX01 container. Output is unchanged on failure.
[[nodiscard]] bool ReadProcessedTexture(std::span<const std::uint8_t> source,
                                         ProcessedTexture& output,
                                         std::string& error);
[[nodiscard]] bool LoadProcessedTexture(const std::filesystem::path& path,
                                         ProcessedTexture& output,
                                         std::string& error);

// Writes a deterministic little-endian TVTTEX01 container without timestamps,
// absolute paths, or host-endian metadata. Replacement is staged before publish.
[[nodiscard]] bool WriteProcessedTexture(const std::filesystem::path& destination,
                                         const std::vector<TextureMip>& mipChain,
                                         TextureColorSpace colorSpace,
                                         std::string& error);
} // namespace vanta::assets
