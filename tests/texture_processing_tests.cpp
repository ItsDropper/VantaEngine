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

    std::vector<vanta::assets::TextureMip> invalid{{2U,2U,std::vector<std::uint8_t>(16U,0U)}};
    Check(!vanta::assets::WriteProcessedTexture(directory / "invalid.ttex", invalid,
          vanta::assets::TextureColorSpace::Linear, error) && !error.empty(),
          "rejects incomplete mip chains");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::cout << (failures == 0 ? "5/5" : "Some") << " texture processing checks passed.\n";
    return failures == 0 ? 0 : 1;
}
