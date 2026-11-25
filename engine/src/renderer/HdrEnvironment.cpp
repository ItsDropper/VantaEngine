#include "vanta/renderer/HdrEnvironment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <cstring>
#include <limits>
#include <string_view>

namespace vanta::renderer
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;
constexpr float kInvPi = 1.0F / kPi;

struct Vec3 { float x, y, z; };

Vec3 Normalize(Vec3 v) noexcept
{
    const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
    if (!(lengthSquared > 1.0e-20F) || !std::isfinite(lengthSquared))
        return {0.0F, 1.0F, 0.0F};
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {v.x * inverseLength, v.y * inverseLength, v.z * inverseLength};
}
float Dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 Add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 Mul(Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
Vec3 Cross(Vec3 a, Vec3 b) noexcept
{
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
Vec3 ToVec3(EnvironmentRadiance c) noexcept { return {c.r,c.g,c.b}; }
EnvironmentRadiance ToRadiance(Vec3 c) noexcept { return {c.x,c.y,c.z}; }

bool ReadLine(std::span<const std::uint8_t> bytes, std::size_t& cursor, std::string_view& line)
{
    if (cursor >= bytes.size()) return false;
    const std::size_t start = cursor;
    while (cursor < bytes.size() && bytes[cursor] != '\n') ++cursor;
    const std::size_t end = cursor;
    if (cursor < bytes.size()) ++cursor;
    std::size_t trimmedEnd = end;
    if (trimmedEnd > start && bytes[trimmedEnd - 1U] == '\r') --trimmedEnd;
    line = std::string_view(reinterpret_cast<const char*>(bytes.data() + start), trimmedEnd - start);
    return true;
}

bool DecodeScanline(std::span<const std::uint8_t> bytes, std::size_t& cursor,
    std::uint32_t width, std::vector<std::array<std::uint8_t, 4>>& scanline)
{
    scanline.resize(width);
    if (width >= 8U && width <= 32767U && cursor + 4U <= bytes.size() &&
        bytes[cursor] == 2U && bytes[cursor + 1U] == 2U &&
        (bytes[cursor + 2U] & 0x80U) == 0U &&
        ((static_cast<std::uint32_t>(bytes[cursor + 2U]) << 8U) | bytes[cursor + 3U]) == width)
    {
        cursor += 4U;
        std::vector<std::uint8_t> channel(width);
        for (std::size_t component = 0; component < 4U; ++component)
        {
            std::uint32_t x = 0U;
            while (x < width)
            {
                if (cursor >= bytes.size()) return false;
                const std::uint8_t code = bytes[cursor++];
                if (code > 128U)
                {
                    const std::uint32_t count = static_cast<std::uint32_t>(code) - 128U;
                    if (count == 0U || count > width - x || cursor >= bytes.size()) return false;
                    const std::uint8_t value = bytes[cursor++];
                    std::fill_n(channel.begin() + x, count, value);
                    x += count;
                }
                else
                {
                    const std::uint32_t count = code;
                    if (count == 0U || count > width - x || cursor + count > bytes.size()) return false;
                    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), count, channel.begin() + x);
                    cursor += count;
                    x += count;
                }
            }
            for (std::uint32_t xIndex = 0; xIndex < width; ++xIndex)
                scanline[xIndex][component] = channel[xIndex];
        }
        return true;
    }

    if (cursor + static_cast<std::size_t>(width) * 4U > bytes.size()) return false;
    for (std::uint32_t x = 0; x < width; ++x)
        for (std::size_t c = 0; c < 4U; ++c)
            scanline[x][c] = bytes[cursor++];
    return true;
}

EnvironmentRadiance DecodeRgbe(const std::array<std::uint8_t, 4>& rgbe) noexcept
{
    if (rgbe[3] == 0U) return {};
    const float scale = std::ldexp(1.0F, static_cast<int>(rgbe[3]) - (128 + 8));
    return {static_cast<float>(rgbe[0]) * scale,
        static_cast<float>(rgbe[1]) * scale, static_cast<float>(rgbe[2]) * scale};
}

float RadicalInverse(std::uint32_t bits) noexcept
{
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}
Vec3 FaceDirection(std::size_t face, float u, float v) noexcept
{
    switch (face)
    {
    case 0: return Normalize({1.0F, -v, -u});
    case 1: return Normalize({-1.0F, -v, u});
    case 2: return Normalize({u, 1.0F, v});
    case 3: return Normalize({u, -1.0F, -v});
    case 4: return Normalize({u, -v, 1.0F});
    default: return Normalize({-u, -v, -1.0F});
    }
}
Vec3 TangentSample(Vec3 normal, float x, float y, float z) noexcept
{
    const Vec3 up = std::abs(normal.y) < 0.999F ? Vec3{0.0F,1.0F,0.0F} : Vec3{1.0F,0.0F,0.0F};
    const Vec3 tangent = Normalize(Cross(up, normal));
    const Vec3 bitangent = Cross(normal, tangent);
    return Normalize(Add(Add(Mul(tangent, x), Mul(bitangent, y)), Mul(normal, z)));
}
Vec3 ImportanceSampleGgx(float xiX, float xiY, float roughness, Vec3 normal) noexcept
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float phi = 2.0F * kPi * xiX;
    const float cosTheta = std::sqrt((1.0F - xiY) /
        std::max(1.0e-7F, 1.0F + (alphaSquared - 1.0F) * xiY));
    const float sinTheta = std::sqrt(std::max(0.0F, 1.0F - cosTheta * cosTheta));
    return TangentSample(normal, std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
}
}

bool HdrEnvironmentImage::IsValid() const noexcept
{
    return width >= 2U && height >= 2U &&
        pixels.size() == static_cast<std::size_t>(width) * height;
}

bool DecodeRadianceHdr(std::span<const std::uint8_t> encoded,
    HdrEnvironmentImage& output, std::string& error)
{
    output = {};
    error.clear();
    std::size_t cursor = 0U;
    std::string_view line;
    if (!ReadLine(encoded, cursor, line) || (line != "#?RADIANCE" && line != "#?RGBE"))
    {
        error = "HDR environment must use the Radiance RGBE format.";
        return false;
    }
    bool formatFound = false;
    while (ReadLine(encoded, cursor, line) && !line.empty())
    {
        if (line == "FORMAT=32-bit_rle_rgbe") formatFound = true;
    }
    if (!formatFound || !ReadLine(encoded, cursor, line))
    {
        error = "Radiance HDR header is incomplete or uses an unsupported encoding.";
        return false;
    }

    int height = 0;
    int width = 0;
    char ySign = 0;
    char xSign = 0;
    char yAxis = 0;
    char xAxis = 0;
    if (std::sscanf(std::string(line).c_str(), " %c%c %d %c%c %d",
        &ySign, &yAxis, &height, &xSign, &xAxis, &width) != 6 ||
        ySign != '-' || yAxis != 'Y' || xSign != '+' || xAxis != 'X' ||
        width < 2 || height < 2 || width > 32767 || height > 32767)
    {
        error = "Radiance HDR resolution must use the -Y height +X width orientation and dimensions from 2 to 32767.";
        return false;
    }

    HdrEnvironmentImage decoded;
    decoded.width = static_cast<std::uint32_t>(width);
    decoded.height = static_cast<std::uint32_t>(height);
    decoded.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<std::array<std::uint8_t, 4>> scanline;
    for (int y = 0; y < height; ++y)
    {
        if (!DecodeScanline(encoded, cursor, decoded.width, scanline))
        {
            error = "Radiance HDR pixel data is truncated or contains an invalid RLE packet.";
            return false;
        }
        for (int x = 0; x < width; ++x)
        {
            const EnvironmentRadiance pixel = DecodeRgbe(scanline[static_cast<std::size_t>(x)]);
            if (!std::isfinite(pixel.r) || !std::isfinite(pixel.g) || !std::isfinite(pixel.b))
            {
                error = "Radiance HDR contains non-finite radiance values.";
                return false;
            }
            decoded.pixels[static_cast<std::size_t>(y) * width + x] = pixel;
        }
    }
    output = std::move(decoded);
    return true;
}

EnvironmentRadiance SampleEquirectangular(const HdrEnvironmentImage& image,
    float directionX, float directionY, float directionZ) noexcept
{
    if (!image.IsValid()) return {};
    const Vec3 direction = Normalize({directionX,directionY,directionZ});
    float u = std::atan2(direction.z, direction.x) * (0.5F * kInvPi) + 0.5F;
    float v = std::acos(std::clamp(direction.y, -1.0F, 1.0F)) * kInvPi;
    u = u * static_cast<float>(image.width) - 0.5F;
    v = std::clamp(v * static_cast<float>(image.height) - 0.5F, 0.0F,
        static_cast<float>(image.height - 1U));
    const int x0Raw = static_cast<int>(std::floor(u));
    const int x0 = (x0Raw % static_cast<int>(image.width) + static_cast<int>(image.width)) %
        static_cast<int>(image.width);
    const int x1 = (x0 + 1) % static_cast<int>(image.width);
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(v));
    const std::uint32_t y1 = std::min(y0 + 1U, image.height - 1U);
    const float tx = u - std::floor(u);
    const float ty = v - static_cast<float>(y0);
    const auto at = [&image](int x, std::uint32_t y) {
        return ToVec3(image.pixels[static_cast<std::size_t>(y) * image.width + static_cast<std::uint32_t>(x)]);
    };
    const Vec3 top = Add(Mul(at(x0,y0),1.0F-tx),Mul(at(x1,y0),tx));
    const Vec3 bottom = Add(Mul(at(x0,y1),1.0F-tx),Mul(at(x1,y1),tx));
    return ToRadiance(Add(Mul(top,1.0F-ty),Mul(bottom,ty)));
}

bool BuildPrefilteredEnvironment(const HdrEnvironmentImage& source,
    const EnvironmentPrefilterOptions& options, PrefilteredEnvironment& output,
    std::string& error)
{
    output = {};
    error.clear();
    if (!source.IsValid())
    {
        error = "Cannot prefilter an invalid HDR environment image.";
        return false;
    }
    if (options.cubeSize < 4U || options.cubeSize > 1024U ||
        options.diffuseSize < 1U || options.diffuseSize > 256U ||
        options.specularSampleCount < 32U || options.specularSampleCount > 4096U ||
        options.diffuseSampleCount < 32U || options.diffuseSampleCount > 4096U)
    {
        error = "Environment prefilter settings are outside the supported quality and memory limits.";
        return false;
    }

    const auto generate = [&](std::uint32_t size, float roughness, bool diffuse,
        std::uint32_t sampleCount, EnvironmentCubeMip& mip)
    {
        mip.size = size;
        const std::size_t pixelCount = static_cast<std::size_t>(size) * size;
        for (auto& face : mip.faces) face.resize(pixelCount);
        for (std::size_t face = 0; face < 6U; ++face)
        {
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const float u = 2.0F * (static_cast<float>(x) + 0.5F) /
                        static_cast<float>(size) - 1.0F;
                    const float v = 2.0F * (static_cast<float>(y) + 0.5F) /
                        static_cast<float>(size) - 1.0F;
                    const Vec3 normal = FaceDirection(face,u,v);
                    Vec3 sum{};
                    float totalWeight = 0.0F;
                    for (std::uint32_t sample = 0; sample < sampleCount; ++sample)
                    {
                        const float xiX = (static_cast<float>(sample) + 0.5F) /
                            static_cast<float>(sampleCount);
                        const float xiY = RadicalInverse(sample);
                        Vec3 direction{};
                        float weight = 1.0F;
                        if (diffuse)
                        {
                            const float radius = std::sqrt(xiX);
                            const float phi = 2.0F * kPi * xiY;
                            direction = TangentSample(normal, radius * std::cos(phi),
                                radius * std::sin(phi), std::sqrt(std::max(0.0F,1.0F-xiX)));
                        }
                        else
                        {
                            const Vec3 halfVector = ImportanceSampleGgx(xiX,xiY,
                                std::max(roughness,0.001F),normal);
                            direction = Normalize(Add(Mul(halfVector,2.0F*Dot(normal,halfVector)),
                                Mul(normal,-1.0F)));
                            weight = std::max(0.0F,Dot(normal,direction));
                        }
                        if (weight <= 0.0F) continue;
                        const EnvironmentRadiance sampleRadiance = SampleEquirectangular(
                            source,direction.x,direction.y,direction.z);
                        sum = Add(sum,Mul(ToVec3(sampleRadiance),weight));
                        totalWeight += weight;
                    }
                    if (totalWeight > 0.0F) sum = Mul(sum, diffuse ? kPi / totalWeight : 1.0F / totalWeight);
                    mip.faces[face][static_cast<std::size_t>(y) * size + x] = ToRadiance(sum);
                }
            }
        }
    };

    generate(options.diffuseSize,1.0F,true,options.diffuseSampleCount,output.diffuseIrradiance);
    std::uint32_t mipSize = options.cubeSize;
    do
    {
        const std::size_t level = output.specularMips.size();
        const float roughness = output.specularMips.size() == 0U ? 0.0F :
            static_cast<float>(level) / static_cast<float>(static_cast<unsigned int>(
                std::floor(std::log2(static_cast<float>(options.cubeSize)))));
        EnvironmentCubeMip mip;
        generate(mipSize,roughness,false,options.specularSampleCount,mip);
        output.specularMips.push_back(std::move(mip));
        if (mipSize == 1U) break;
        mipSize = std::max(1U,mipSize / 2U);
    } while (true);

    return ValidatePrefilteredEnvironment(output,error);
}

bool ValidatePrefilteredEnvironment(const PrefilteredEnvironment& environment,
    std::string& error) noexcept
{
    error.clear();
    const auto validMip = [](const EnvironmentCubeMip& mip)
    {
        if (mip.size == 0U) return false;
        const std::size_t expected = static_cast<std::size_t>(mip.size) * mip.size;
        for (const auto& face : mip.faces)
        {
            if (face.size() != expected) return false;
            for (const EnvironmentRadiance pixel : face)
                if (!std::isfinite(pixel.r) || !std::isfinite(pixel.g) || !std::isfinite(pixel.b) ||
                    pixel.r < 0.0F || pixel.g < 0.0F || pixel.b < 0.0F)
                    return false;
        }
        return true;
    };
    if (!validMip(environment.diffuseIrradiance) || environment.specularMips.empty())
    {
        error = "Prefiltered environment is missing valid diffuse or specular cube data.";
        return false;
    }
    std::uint32_t expectedSize = environment.specularMips.front().size;
    for (const EnvironmentCubeMip& mip : environment.specularMips)
    {
        if (!validMip(mip) || mip.size != expectedSize)
        {
            error = "Prefiltered environment mip dimensions or radiance data are invalid.";
            return false;
        }
        expectedSize = std::max(1U,expectedSize / 2U);
    }
    if (environment.specularMips.back().size != 1U)
    {
        error = "Prefiltered environment specular chain must terminate at 1x1.";
        return false;
    }
    return true;
}
} // namespace vanta::renderer
