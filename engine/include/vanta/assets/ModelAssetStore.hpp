#pragma once

#include "vanta/assets/ObjLoader.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <string>

namespace vanta::assets
{
struct ModelHandle
{
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr bool operator==(const ModelHandle&) const noexcept = default;
};

// CPU-side model cache. It owns imported geometry/material data and deduplicates
// canonical file paths. GPU upload remains the responsibility of a renderer backend.
class ModelAssetStore final
{
public:
    [[nodiscard]] bool LoadModel(
        const std::filesystem::path& path, ModelHandle& output, std::string& error);
    [[nodiscard]] bool Unload(ModelHandle handle) noexcept;
    void Clear() noexcept;

    [[nodiscard]] bool Contains(ModelHandle handle) const noexcept;
    [[nodiscard]] const CpuModel* GetModel(ModelHandle handle) const noexcept;
    [[nodiscard]] const std::filesystem::path* GetSourcePath(ModelHandle handle) const noexcept;
    [[nodiscard]] std::size_t LoadedCount() const noexcept { return m_loadedCount; }

private:
    struct Slot
    {
        CpuModel model;
        std::filesystem::path sourcePath;
        std::wstring cacheKey;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    [[nodiscard]] const Slot* Find(ModelHandle handle) const noexcept;
    [[nodiscard]] Slot* Find(ModelHandle handle) noexcept;
    [[nodiscard]] static bool NormalizeAndValidate(CpuModel& model, std::string& error);
    [[nodiscard]] static std::wstring MakeCacheKey(const std::filesystem::path& path);

    std::deque<Slot> m_slots;
    std::map<std::wstring, ModelHandle> m_pathCache;
    std::size_t m_loadedCount = 0;
};
} // namespace vanta::assets
