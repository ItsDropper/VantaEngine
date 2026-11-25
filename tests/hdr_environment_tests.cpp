#include "vanta/renderer/HdrEnvironment.hpp"
#include "vanta/renderer/EnvironmentLighting.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace vanta::renderer;

namespace
{
void Require(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "HDR environment test failed at line " << line << '\n';
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(value) Require((value), __LINE__)
}

int main()
{
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    std::vector<std::uint8_t> encoded(header.begin(), header.end());
    // Width four uses the legacy flat RGBE scanline representation.
    for (int pixel = 0; pixel < 8; ++pixel)
    {
        encoded.push_back(128U);
        encoded.push_back(64U);
        encoded.push_back(32U);
        encoded.push_back(129U);
    }

    HdrEnvironmentImage image;
    std::string error;
    CHECK(DecodeRadianceHdr(encoded, image, error));
    CHECK(error.empty());
    CHECK(image.IsValid());
    CHECK(image.width == 4U && image.height == 2U);
    CHECK(std::abs(image.pixels[0].r - 1.0F) < 0.0001F);
    CHECK(std::abs(image.pixels[0].g - 0.5F) < 0.0001F);
    CHECK(std::abs(image.pixels[0].b - 0.25F) < 0.0001F);

    const EnvironmentRadiance sample = SampleEquirectangular(image, 1.0F, 0.0F, 0.0F);
    CHECK(std::abs(sample.r - 1.0F) < 0.0001F);
    CHECK(std::abs(sample.g - 0.5F) < 0.0001F);

    const EnvironmentLighting hdrLighting = BuildEnvironmentLighting(image);
    CHECK(std::isfinite(hdrLighting.diffuse[0][0]));
    CHECK(hdrLighting.diffuse[0][0] > 0.0F);
    CHECK(std::abs(hdrLighting.diffuse[0][0] * 0.282095F - 3.14159265F) < 0.08F);
    CHECK(std::abs(hdrLighting.diffuse[0][1] / hdrLighting.diffuse[0][0] - 0.5F) < 0.001F);
    CHECK(std::abs(hdrLighting.diffuse[0][2] / hdrLighting.diffuse[0][0] - 0.25F) < 0.001F);
    CHECK(std::abs(hdrLighting.specular[0][0][0] - hdrLighting.specular[4][0][0]) < 0.0001F);

    EnvironmentPrefilterOptions options;
    options.cubeSize = 8U;
    options.diffuseSize = 2U;
    options.specularSampleCount = 32U;
    options.diffuseSampleCount = 32U;
    PrefilteredEnvironment prefiltered;
    CHECK(BuildPrefilteredEnvironment(image, options, prefiltered, error));
    CHECK(error.empty());
    CHECK(prefiltered.diffuseIrradiance.size == 2U);
    CHECK(prefiltered.specularMips.size() == 4U);
    CHECK(prefiltered.specularMips.front().size == 8U);
    CHECK(prefiltered.specularMips.back().size == 1U);
    CHECK(ValidatePrefilteredEnvironment(prefiltered, error));

    for (const EnvironmentCubeMip& mip : prefiltered.specularMips)
    {
        for (const auto& face : mip.faces)
        {
            for (const EnvironmentRadiance pixel : face)
            {
                CHECK(std::isfinite(pixel.r));
                CHECK(std::isfinite(pixel.g));
                CHECK(std::isfinite(pixel.b));
                CHECK(std::abs(pixel.r - 1.0F) < 0.02F);
                CHECK(std::abs(pixel.g - 0.5F) < 0.02F);
                CHECK(std::abs(pixel.b - 0.25F) < 0.02F);
            }
        }
    }

    const std::vector<std::uint8_t> invalid{'n','o','t',' ','h','d','r'};
    CHECK(!DecodeRadianceHdr(invalid, image, error));
    CHECK(!error.empty());
    CHECK(!BuildPrefilteredEnvironment(HdrEnvironmentImage{}, options, prefiltered, error));
    CHECK(!BuildPrefilteredEnvironment(image,
        EnvironmentPrefilterOptions{2U, 2U, 32U, 32U}, prefiltered, error));
    return EXIT_SUCCESS;
}
