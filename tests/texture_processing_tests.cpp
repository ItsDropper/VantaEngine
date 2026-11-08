#include "vanta/assets/TextureProcessing.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void Check(bool condition, const char* message)
{
    if (condition) std::cout << "PASS: " << message << '\n';
    else { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    vanta::assets::CpuTexture source;
    source.width = 2U;
    source.height = 2U;
    source.rgba = {
        0U,0U,0U,0U, 255U,255U,255U,255U,
        0U,0U,0U,0U, 255U,255U,255U,255U
    };
    std::vector<vanta::assets::TextureMip> mips;
    std::string error;
    Check(vanta::assets::BuildTextureMipChain(source, vanta::assets::TextureColorSpace::Srgb, mips, error),
          "builds a full sRGB mip chain");
    Check(mips.size() == 2U && mips.back().width == 1U && mips.back().height == 1U,
          "mip chain terminates at 1x1");
    Check(mips.back().rgba == std::vector<std::uint8_t>({188U,188U,188U,128U}),
          "filters sRGB RGB in linear light and alpha linearly");

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("vanta-texture-processing-" + std::to_string(stamp));
    const auto destination = directory / "snow.ttex";
    Check(vanta::assets::WriteProcessedTexture(destination, mips,
          vanta::assets::TextureColorSpace::Srgb, error), "writes processed texture container");
    std::ifstream firstStream(destination, std::ios::binary);
    const std::vector<char> first((std::istreambuf_iterator<char>(firstStream)), {});
    firstStream.close();
    Check(vanta::assets::WriteProcessedTexture(destination, mips,
          vanta::assets::TextureColorSpace::Srgb, error), "atomically replaces an existing output");
    std::ifstream secondStream(destination, std::ios::binary);
    const std::vector<char> second((std::istreambuf_iterator<char>(secondStream)), {});
    secondStream.close();
    Check(first == second, "serializes byte-identical output on repeated processing");
    Check(first.size() > 32U && std::string(first.begin(), first.begin() + 8) == "TVTTEX01",
          "writes the versioned deterministic TVTTEX01 container");
    vanta::assets::ProcessedTexture loaded;
    Check(vanta::assets::LoadProcessedTexture(destination, loaded, error),
          "reads the processed texture container back");
    Check(loaded.colorSpace == vanta::assets::TextureColorSpace::Srgb &&
          loaded.mipChain.size() == mips.size() && loaded.mipChain.back().rgba == mips.back().rgba,
          "round-trip preserves colour-space metadata and all mip pixels");
    std::vector<std::uint8_t> malformed(first.begin(), first.end());
    malformed.push_back(0U);
    loaded.colorSpace = vanta::assets::TextureColorSpace::Linear;
    Check(!vanta::assets::ReadProcessedTexture(malformed, loaded, error) &&
          loaded.colorSpace == vanta::assets::TextureColorSpace::Linear,
          "rejects trailing bytes without publishing partial output");

    std::vector<vanta::assets::TextureMip> invalid{{2U,2U,std::vector<std::uint8_t>(16U,0U)}};
    Check(!vanta::assets::WriteProcessedTexture(directory / "invalid.ttex", invalid,
          vanta::assets::TextureColorSpace::Linear, error) && !error.empty(),
          "rejects incomplete mip chains");

    vanta::assets::CpuTexture normalSource;
    normalSource.width = 2U;
    normalSource.height = 2U;
    normalSource.rgba = {
        128U,128U,255U,0U, 128U,128U,255U,64U,
        128U,128U,255U,128U, 128U,128U,255U,255U
    };
    std::vector<vanta::assets::TextureMip> normalMips;
    Check(vanta::assets::BuildNormalMapMipChain(normalSource, normalMips, error),
          "builds tangent-space normal mip chains");
    Check(normalMips.size() == 2U && normalMips.back().width == 1U &&
          normalMips.back().height == 1U,
          "normal mip chain terminates at 1x1");
    Check(normalMips.front().rgba[0] == 128U && normalMips.front().rgba[1] == 128U &&
          normalMips.front().rgba[2] == 255U,
          "normalizes source-level tangent vectors");
    Check(normalMips.back().rgba == std::vector<std::uint8_t>({128U,128U,255U,112U}),
          "renormalizes filtered normals and averages alpha linearly");

    vanta::assets::CpuTexture opposingNormals;
    opposingNormals.width = 2U;
    opposingNormals.height = 2U;
    opposingNormals.rgba = {
        255U,128U,128U,255U, 0U,127U,127U,255U,
        128U,255U,128U,255U, 127U,0U,127U,255U
    };
    std::vector<vanta::assets::TextureMip> opposingMips;
    Check(vanta::assets::BuildNormalMapMipChain(opposingNormals, opposingMips, error) &&
          opposingMips.back().rgba == std::vector<std::uint8_t>({128U,128U,255U,255U}),
          "uses a stable +Z fallback when opposing normals cancel");

    vanta::assets::CpuTexture oddNormalSource;
    oddNormalSource.width = 3U;
    oddNormalSource.height = 1U;
    oddNormalSource.rgba = {
        128U,128U,255U,255U, 255U,128U,128U,255U, 128U,255U,128U,255U
    };
    std::vector<vanta::assets::TextureMip> oddNormalMips;
    Check(vanta::assets::BuildNormalMapMipChain(oddNormalSource, oddNormalMips, error) &&
          oddNormalMips.size() == 3U && oddNormalMips[1].width == 2U &&
          oddNormalMips[2].width == 1U,
          "handles odd-width normal maps through a complete mip chain");

    std::vector<vanta::assets::TextureMip> preservedNormalOutput{
        {1U,1U,std::vector<std::uint8_t>{1U,2U,3U,4U}}
    };
    vanta::assets::CpuTexture invalidNormalSource;
    invalidNormalSource.width = 2U;
    invalidNormalSource.height = 2U;
    Check(!vanta::assets::BuildNormalMapMipChain(invalidNormalSource,
          preservedNormalOutput, error) && !error.empty() &&
          preservedNormalOutput.size() == 1U &&
          preservedNormalOutput.front().rgba == std::vector<std::uint8_t>({1U,2U,3U,4U}),
          "rejects inconsistent normal-map storage without changing output");

    vanta::assets::CpuTexture overflowingTexture;
    overflowingTexture.width = 1U << 31U;
    overflowingTexture.height = 1U << 31U;
    std::vector<vanta::assets::TextureMip> preservedColorOutput{
        {1U,1U,std::vector<std::uint8_t>{5U,6U,7U,8U}}
    };
    Check(!vanta::assets::BuildTextureMipChain(overflowingTexture,
          vanta::assets::TextureColorSpace::Linear, preservedColorOutput, error) &&
          !error.empty() && preservedColorOutput.front().rgba ==
          std::vector<std::uint8_t>({5U,6U,7U,8U}),
          "rejects overflowing source dimensions before allocating or changing output");
    Check(!vanta::assets::BuildNormalMapMipChain(overflowingTexture,
          preservedNormalOutput, error) && !error.empty() &&
          preservedNormalOutput.front().rgba == std::vector<std::uint8_t>({1U,2U,3U,4U}),
          "rejects overflowing normal-map dimensions before allocating or changing output");
    std::vector<vanta::assets::TextureMip> overflowingMipChain{
        {1U << 31U,1U << 31U,{}}
    };
    Check(!vanta::assets::WriteProcessedTexture(directory / "overflow.ttex",
          overflowingMipChain, vanta::assets::TextureColorSpace::Linear, error) &&
          !error.empty(),
          "rejects overflowing processed mip dimensions before serializing");

    Check(vanta::assets::WriteProcessedTexture(directory / "normal.ttex", normalMips,
          vanta::assets::TextureColorSpace::Linear, error),
          "writes normal mip data as linear processed texture");
    vanta::assets::ProcessedTexture loadedNormal;
    Check(vanta::assets::LoadProcessedTexture(directory / "normal.ttex", loadedNormal, error) &&
          loadedNormal.colorSpace == vanta::assets::TextureColorSpace::Linear &&
          loadedNormal.mipChain.size() == normalMips.size() &&
          loadedNormal.mipChain.back().rgba == normalMips.back().rgba,
          "round-trips normalized normal-map mip pixels as linear data");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::cout << (failures == 0 ? "All" : "Some") << " texture processing checks passed.\n";
    return failures == 0 ? 0 : 1;
}
