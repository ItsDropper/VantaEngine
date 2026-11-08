#include "vanta/assets/TextureProcessing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace vanta::assets
{
namespace
{
constexpr std::uint64_t MaximumMipBytes = 768ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumContainerBytes = MaximumMipBytes + 32ULL + 12ULL * 32ULL;
constexpr std::array<std::uint16_t, 256> SrgbToLinear16 = { 0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863, 1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500, 7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001, 18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143, 34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567, 57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535 };

[[nodiscard]] std::uint8_t Linear16ToSrgb(const std::uint32_t value) noexcept
{
    const auto found = std::lower_bound(SrgbToLinear16.begin(), SrgbToLinear16.end(), value);
    if (found == SrgbToLinear16.begin()) return 0U;
    if (found == SrgbToLinear16.end()) return 255U;
    const auto upper = static_cast<std::uint32_t>(*found);
    const auto lower = static_cast<std::uint32_t>(*(found - 1));
    const auto upperDistance = upper - value;
    const auto lowerDistance = value - lower;
    return static_cast<std::uint8_t>(upperDistance < lowerDistance
        ? std::distance(SrgbToLinear16.begin(), found)
        : std::distance(SrgbToLinear16.begin(), found - 1));
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

struct NormalVector final
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 1.0F;
};

[[nodiscard]] NormalVector NormalizeNormal(const NormalVector value) noexcept
{
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!(lengthSquared > 1.0e-12F))
        return {};
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

[[nodiscard]] NormalVector DecodeNormal(const std::uint8_t* rgba) noexcept
{
    constexpr float inverseByteMidpoint = 1.0F / 127.5F;
    return NormalizeNormal({
        static_cast<float>(rgba[0]) * inverseByteMidpoint - 1.0F,
        static_cast<float>(rgba[1]) * inverseByteMidpoint - 1.0F,
        static_cast<float>(rgba[2]) * inverseByteMidpoint - 1.0F
    });
}

void EncodeNormal(const NormalVector normal, std::uint8_t* rgba) noexcept
{
    const auto encode = [](const float component) noexcept
    {
        const long value = std::lround((component * 0.5F + 0.5F) * 255.0F);
        return static_cast<std::uint8_t>(std::clamp(value, 0L, 255L));
    };
    rgba[0] = encode(normal.x);
    rgba[1] = encode(normal.y);
    rgba[2] = encode(normal.z);
}
} // namespace

bool BuildTextureMipChain(const CpuTexture& source, const TextureColorSpace colorSpace,
                          std::vector<TextureMip>& output, std::string& error)
{
    error.clear();
    if (source.width == 0U || source.height == 0U ||
        static_cast<std::uint64_t>(source.width) > MaximumMipBytes / 4ULL / source.height)
    {
        error = "Source texture dimensions are zero or exceed the 768 MiB safety limit.";
        return false;
    }
    const std::uint64_t expectedSourceBytes =
        static_cast<std::uint64_t>(source.width) * source.height * 4ULL;
    if (source.rgba.size() != expectedSourceBytes)
    {
        error = "Source texture RGBA8 storage does not match its dimensions.";
        return false;
    }
    if (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb)
    {
        error = "Texture colour-space value is not supported.";
        return false;
    }

    std::vector<TextureMip> candidate;
    candidate.push_back({source.width, source.height, source.rgba});
    std::uint64_t totalBytes = expectedSourceBytes;
    while (candidate.back().width > 1U || candidate.back().height > 1U)
    {
        const TextureMip& previous = candidate.back();
        TextureMip next;
        next.width = std::max(1U, (previous.width + 1U) / 2U);
        next.height = std::max(1U, (previous.height + 1U) / 2U);
        const std::uint64_t nextBytes = static_cast<std::uint64_t>(next.width) * next.height * 4ULL;
        if (nextBytes > MaximumMipBytes - totalBytes)
        {
            error = "Complete mip chain exceeds the 768 MiB processing safety limit.";
            return false;
        }
        next.rgba.resize(static_cast<std::size_t>(nextBytes));
        for (std::uint32_t y = 0; y < next.height; ++y)
        {
            for (std::uint32_t x = 0; x < next.width; ++x)
            {
                std::array<std::uint32_t, 4> sums{};
                for (std::uint32_t dy = 0; dy < 2U; ++dy)
                {
                    const std::uint32_t sourceY = std::min(previous.height - 1U, y * 2U + dy);
                    for (std::uint32_t dx = 0; dx < 2U; ++dx)
                    {
                        const std::uint32_t sourceX = std::min(previous.width - 1U, x * 2U + dx);
                        const std::size_t offset =
                            (static_cast<std::size_t>(sourceY) * previous.width + sourceX) * 4U;
                        for (std::size_t channel = 0; channel < 4U; ++channel)
                        {
                            const std::uint8_t sample = previous.rgba[offset + channel];
                            sums[channel] += channel < 3U && colorSpace == TextureColorSpace::Srgb
                                ? SrgbToLinear16[sample] : static_cast<std::uint32_t>(sample) * 257U;
                        }
                    }
                }
                const std::size_t destination =
                    (static_cast<std::size_t>(y) * next.width + x) * 4U;
                for (std::size_t channel = 0; channel < 4U; ++channel)
                {
                    const std::uint32_t average = (sums[channel] + 2U) / 4U;
                    next.rgba[destination + channel] =
                        channel < 3U && colorSpace == TextureColorSpace::Srgb
                            ? Linear16ToSrgb(average)
                            : static_cast<std::uint8_t>((average + 128U) / 257U);
                }
            }
        }
        totalBytes += nextBytes;
        candidate.push_back(std::move(next));
    }
    output = std::move(candidate);
    return true;
}


bool BuildNormalMapMipChain(const CpuTexture& source,
                            std::vector<TextureMip>& output, std::string& error)
{
    error.clear();
    if (source.width == 0U || source.height == 0U ||
        static_cast<std::uint64_t>(source.width) > MaximumMipBytes / 4ULL / source.height)
    {
        error = "Source normal map dimensions are zero or exceed the 768 MiB safety limit.";
        return false;
    }
    const std::uint64_t expectedSourceBytes =
        static_cast<std::uint64_t>(source.width) * source.height * 4ULL;
    if (source.rgba.size() != expectedSourceBytes)
    {
        error = "Source normal map RGBA8 storage does not match its dimensions.";
        return false;
    }

    std::vector<TextureMip> candidate;
    TextureMip base{source.width, source.height, source.rgba};
    for (std::size_t offset = 0U; offset < base.rgba.size(); offset += 4U)
        EncodeNormal(DecodeNormal(base.rgba.data() + offset), base.rgba.data() + offset);
    candidate.push_back(std::move(base));

    std::uint64_t totalBytes = expectedSourceBytes;
    while (candidate.back().width > 1U || candidate.back().height > 1U)
    {
        const TextureMip& previous = candidate.back();
        TextureMip next;
        next.width = std::max(1U, (previous.width + 1U) / 2U);
        next.height = std::max(1U, (previous.height + 1U) / 2U);
        const std::uint64_t nextBytes =
            static_cast<std::uint64_t>(next.width) * next.height * 4ULL;
        if (nextBytes > MaximumMipBytes - totalBytes)
        {
            error = "Complete normal-map mip chain exceeds the 768 MiB processing safety limit.";
            return false;
        }
        next.rgba.resize(static_cast<std::size_t>(nextBytes));
        for (std::uint32_t y = 0U; y < next.height; ++y)
        {
            for (std::uint32_t x = 0U; x < next.width; ++x)
            {
                NormalVector sum{0.0F, 0.0F, 0.0F};
                std::uint32_t alphaSum = 0U;
                for (std::uint32_t dy = 0U; dy < 2U; ++dy)
                {
                    const std::uint32_t sourceY = std::min(previous.height - 1U, y * 2U + dy);
                    for (std::uint32_t dx = 0U; dx < 2U; ++dx)
                    {
                        const std::uint32_t sourceX = std::min(previous.width - 1U, x * 2U + dx);
                        const std::size_t sourceOffset =
                            (static_cast<std::size_t>(sourceY) * previous.width + sourceX) * 4U;
                        const NormalVector normal = DecodeNormal(previous.rgba.data() + sourceOffset);
                        sum.x += normal.x;
                        sum.y += normal.y;
                        sum.z += normal.z;
                        alphaSum += previous.rgba[sourceOffset + 3U];
                    }
                }

                const std::size_t destination =
                    (static_cast<std::size_t>(y) * next.width + x) * 4U;
                EncodeNormal(NormalizeNormal(sum), next.rgba.data() + destination);
                next.rgba[destination + 3U] =
                    static_cast<std::uint8_t>((alphaSum + 2U) / 4U);
            }
        }
        totalBytes += nextBytes;
        candidate.push_back(std::move(next));
    }

    output = std::move(candidate);
    return true;
}

bool ReadProcessedTexture(std::span<const std::uint8_t> source,
                          ProcessedTexture& output, std::string& error)
{
    error.clear();
    constexpr std::array<std::uint8_t, 8> magic = {'T','V','T','T','E','X','0','1'};
    if (source.size() < 32U || source.size() > MaximumContainerBytes ||
        !std::equal(magic.begin(), magic.end(), source.begin()))
    {
        error = "Processed texture has an invalid signature, truncated header, or exceeds the container size limit.";
        return false;
    }
    std::size_t cursor = 8U;
    const auto readHeader = [&source, &cursor]() -> std::uint32_t
    {
        const std::uint32_t value = ReadU32(source, cursor);
        cursor += 4U;
        return value;
    };
    const std::uint32_t version = readHeader();
    const std::uint32_t baseWidth = readHeader();
    const std::uint32_t baseHeight = readHeader();
    const std::uint32_t pixelFormat = readHeader();
    const std::uint32_t colorSpaceValue = readHeader();
    const std::uint32_t mipCount = readHeader();
    if (version != 1U || baseWidth == 0U || baseHeight == 0U || pixelFormat != 1U ||
        colorSpaceValue > static_cast<std::uint32_t>(TextureColorSpace::Srgb) ||
        mipCount == 0U || mipCount > 32U)
    {
        error = "Processed texture header contains an unsupported version, dimension, pixel format, colour space, or mip count.";
        return false;
    }

    ProcessedTexture candidate;
    candidate.colorSpace = static_cast<TextureColorSpace>(colorSpaceValue);
    candidate.mipChain.reserve(mipCount);
    std::uint32_t expectedWidth = baseWidth;
    std::uint32_t expectedHeight = baseHeight;
    std::uint64_t totalBytes = 0U;
    for (std::uint32_t level = 0U; level < mipCount; ++level)
    {
        if (source.size() - cursor < 12U)
        {
            error = "Processed texture mip header is truncated.";
            return false;
        }
        const std::uint32_t width = readHeader();
        const std::uint32_t height = readHeader();
        const std::uint32_t byteCount = readHeader();
        if (width != expectedWidth || height != expectedHeight || width == 0U || height == 0U ||
            static_cast<std::uint64_t>(width) > MaximumMipBytes / 4ULL / height)
        {
            error = "Processed texture mip dimensions are inconsistent or exceed the pixel safety limit.";
            return false;
        }
        const std::uint64_t expectedBytes = static_cast<std::uint64_t>(width) * height * 4ULL;
        if (byteCount != expectedBytes || expectedBytes > MaximumMipBytes - totalBytes ||
            expectedBytes > source.size() - cursor)
        {
            error = "Processed texture mip byte count is invalid or pixel data is truncated.";
            return false;
        }
        TextureMip mip;
        mip.width = width;
        mip.height = height;
        mip.rgba.assign(source.begin() + static_cast<std::ptrdiff_t>(cursor),
                        source.begin() + static_cast<std::ptrdiff_t>(cursor + expectedBytes));
        candidate.mipChain.push_back(std::move(mip));
        cursor += static_cast<std::size_t>(expectedBytes);
        totalBytes += expectedBytes;
        expectedWidth = std::max(1U, (expectedWidth + 1U) / 2U);
        expectedHeight = std::max(1U, (expectedHeight + 1U) / 2U);
    }
    if (candidate.mipChain.back().width != 1U || candidate.mipChain.back().height != 1U ||
        cursor != source.size())
    {
        error = "Processed texture mip chain is incomplete or contains trailing bytes.";
        return false;
    }
    output = std::move(candidate);
    return true;
}

bool LoadProcessedTexture(const std::filesystem::path& path,
                          ProcessedTexture& output, std::string& error)
{
    error.clear();
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        error = "Could not open processed texture container.";
        return false;
    }
    const std::streamoff end = static_cast<std::streamoff>(stream.tellg());
    if (end < 0 || static_cast<std::uint64_t>(end) > MaximumContainerBytes)
    {
        error = "Processed texture container is unreadable or exceeds the file size limit.";
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty())
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size())
    {
        error = "Could not read the complete processed texture container.";
        return false;
    }
    return ReadProcessedTexture(bytes, output, error);
}

bool WriteProcessedTexture(const std::filesystem::path& destination,
                           const std::vector<TextureMip>& mipChain,
                           const TextureColorSpace colorSpace, std::string& error)
{
    error.clear();
    if (mipChain.empty() ||
        (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb))
    {
        error = "A non-empty mip chain and valid colour-space value are required.";
        return false;
    }
    std::uint32_t expectedWidth = mipChain.front().width;
    std::uint32_t expectedHeight = mipChain.front().height;
    if (expectedWidth == 0U || expectedHeight == 0U)
    {
        error = "Base mip dimensions must be non-zero.";
        return false;
    }
    std::uint64_t totalBytes = 0U;
    for (std::size_t level = 0; level < mipChain.size(); ++level)
    {
        const TextureMip& mip = mipChain[level];
        if (expectedWidth == 1U && expectedHeight == 1U && level + 1U != mipChain.size())
        {
            error = "Mip chain contains levels after its terminal 1x1 image.";
            return false;
        }
        if (mip.width == 0U || mip.height == 0U ||
            static_cast<std::uint64_t>(mip.width) > MaximumMipBytes / 4ULL / mip.height)
        {
            error = "Mip dimensions are zero or exceed the processing safety limit.";
            return false;
        }
        const std::uint64_t expectedBytes = static_cast<std::uint64_t>(mip.width) * mip.height * 4ULL;
        if (mip.width != expectedWidth || mip.height != expectedHeight ||
            mip.rgba.size() != expectedBytes || expectedBytes > MaximumMipBytes - totalBytes)
        {
            error = "Mip levels are inconsistent, incomplete, or exceed the processing safety limit.";
            return false;
        }
        totalBytes += expectedBytes;
        expectedWidth = std::max(1U, (expectedWidth + 1U) / 2U);
        expectedHeight = std::max(1U, (expectedHeight + 1U) / 2U);
    }
    if (mipChain.back().width != 1U || mipChain.back().height != 1U)
    {
        error = "Mip chain must terminate at 1x1.";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(32ULL + totalBytes + mipChain.size() * 12ULL));
    const std::array<std::uint8_t, 8> magic = {'T','V','T','T','E','X','0','1'};
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    AppendU32(bytes, 1U);
    AppendU32(bytes, mipChain.front().width);
    AppendU32(bytes, mipChain.front().height);
    AppendU32(bytes, 1U); // RGBA8
    AppendU32(bytes, static_cast<std::uint32_t>(colorSpace));
    AppendU32(bytes, static_cast<std::uint32_t>(mipChain.size()));
    for (const TextureMip& mip : mipChain)
    {
        AppendU32(bytes, mip.width);
        AppendU32(bytes, mip.height);
        AppendU32(bytes, static_cast<std::uint32_t>(mip.rgba.size()));
        bytes.insert(bytes.end(), mip.rgba.begin(), mip.rgba.end());
    }

    std::error_code filesystemError;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create the processed texture output directory.";
        return false;
    }
    static std::atomic_uint64_t temporarySequence{0U};
    std::filesystem::path temporary = destination;
    temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
                 std::to_string(temporarySequence.fetch_add(1U, std::memory_order_relaxed));
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Could not create the temporary processed texture file.";
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream)
        {
            stream.close();
            std::filesystem::remove(temporary, filesystemError);
            error = "Failed while writing the processed texture file.";
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, filesystemError);
        error = "Could not atomically replace the processed texture destination.";
        return false;
    }
    return true;
}
} // namespace vanta::assets
