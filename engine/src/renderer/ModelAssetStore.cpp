#include "vanta/renderer/ModelAssetStore.hpp"

#include "vanta/renderer/GltfLoader.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <system_error>
#include <limits>
#include <utility>

namespace vanta::renderer
{
namespace
{
[[nodiscard]] bool IsFinite(const math::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

std::wstring ModelAssetStore::MakeCacheKey(const std::filesystem::path& path)
{
    std::wstring key = path.native();
    std::transform(key.begin(), key.end(), key.begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return key;
}

const ModelAssetStore::Slot* ModelAssetStore::Find(const ModelHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.index >= m_slots.size())
        return nullptr;
    const Slot& slot = m_slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

ModelAssetStore::Slot* ModelAssetStore::Find(const ModelHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= m_slots.size())
        return nullptr;
    Slot& slot = m_slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

bool ModelAssetStore::NormalizeAndValidate(CpuModel& model, std::string& error)
{
    if (model.vertices.empty() || model.indices.empty() || model.indices.size() % 3U != 0U ||
        model.indices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Imported model has no indexed triangle geometry or exceeds the supported index range.";
        return false;
    }
    for (const ModelVertex& vertex : model.vertices)
    {
        if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) ||
            !std::isfinite(vertex.u) || !std::isfinite(vertex.v))
        {
            error = "Imported model contains non-finite vertex data.";
            return false;
        }
    }
    for (const unsigned int index : model.indices)
    {
        if (index >= model.vertices.size())
        {
            error = "Imported model contains an index outside its vertex array.";
            return false;
        }
    }

    if (model.materials.empty())
    {
        CpuMaterial material;
        material.diffuseTexture = model.diffuseTexture;
        material.normalTexture = model.normalTexture;
        material.roughnessTexture = model.roughnessTexture;
        material.metallicTexture = model.metallicTexture;
        material.ambientOcclusionTexture = model.ambientOcclusionTexture;
        material.baseColor = model.baseColor;
        material.roughness = model.roughness;
        material.metallic = model.metallic;
        model.materials.push_back(std::move(material));
    }

    if (model.submeshes.empty())
        model.submeshes.push_back({0U, static_cast<std::uint32_t>(model.indices.size()), 0U});

    for (const CpuMaterial& material : model.materials)
    {
        if (!IsFinite(material.baseColor) || !std::isfinite(material.roughness) ||
            !std::isfinite(material.metallic))
        {
            error = "Imported model contains non-finite material properties.";
            return false;
        }
    }
    for (const CpuSubmesh& submesh : model.submeshes)
    {
        const std::uint64_t end = static_cast<std::uint64_t>(submesh.firstIndex) +
                                  static_cast<std::uint64_t>(submesh.indexCount);
        if (submesh.indexCount == 0U || submesh.indexCount % 3U != 0U ||
            end > model.indices.size() || submesh.materialIndex >= model.materials.size())
        {
            error = "Imported model contains an invalid submesh range or material reference.";
            return false;
        }
    }
    return true;
}

bool ModelAssetStore::LoadModel(
    const std::filesystem::path& path, ModelHandle& output, std::string& error)
{
    output = {};
    error.clear();
    if (path.empty())
    {
        error = "Model path is empty.";
        return false;
    }

    std::error_code filesystemError;
    const std::filesystem::path canonicalPath = std::filesystem::canonical(path, filesystemError);
    if (filesystemError || !std::filesystem::is_regular_file(canonicalPath, filesystemError) ||
        filesystemError)
    {
        error = "Model path does not resolve to a readable regular file: " + path.string();
        return false;
    }

    const std::wstring cacheKey = MakeCacheKey(canonicalPath);
    const auto cached = m_pathCache.find(cacheKey);
    if (cached != m_pathCache.end())
    {
        if (Find(cached->second) != nullptr)
        {
            output = cached->second;
            return true;
        }
        m_pathCache.erase(cached);
    }

    std::wstring extension = canonicalPath.extension().native();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });

    CpuModel model;
    bool loaded = false;
    if (extension == L".obj")
        loaded = LoadWavefrontObj(canonicalPath, model, error);
    else if (extension == L".gltf" || extension == L".glb")
        loaded = LoadGltf(canonicalPath, model, error);
    else
    {
        error = "Unsupported model extension. Supported formats are OBJ, glTF and GLB.";
        return false;
    }
    if (!loaded)
        return false;
    if (!NormalizeAndValidate(model, error))
        return false;

    std::size_t slotIndex = m_slots.size();
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        if (!m_slots[index].occupied)
        {
            slotIndex = index;
            break;
        }
    }
    if (slotIndex >= ModelHandle::kInvalidIndex)
    {
        error = "Model asset store exhausted its handle index space.";
        return false;
    }

    if (slotIndex == m_slots.size())
        m_slots.emplace_back();
    Slot& slot = m_slots[slotIndex];
    slot.model = std::move(model);
    slot.sourcePath = canonicalPath;
    slot.cacheKey = cacheKey;
    slot.occupied = true;
    output = {static_cast<std::uint32_t>(slotIndex), slot.generation};
    m_pathCache.emplace(slot.cacheKey, output);
    ++m_loadedCount;
    return true;
}

bool ModelAssetStore::Unload(const ModelHandle handle) noexcept
{
    Slot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    m_pathCache.erase(slot->cacheKey);
    slot->model = {};
    slot->sourcePath.clear();
    slot->cacheKey.clear();
    slot->occupied = false;
    ++slot->generation;
    if (slot->generation == 0U)
        slot->generation = 1U;
    --m_loadedCount;
    return true;
}

void ModelAssetStore::Clear() noexcept
{
    m_pathCache.clear();
    for (Slot& slot : m_slots)
    {
        if (!slot.occupied)
            continue;
        slot.model = {};
        slot.sourcePath.clear();
        slot.cacheKey.clear();
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0U)
            slot.generation = 1U;
    }
    m_loadedCount = 0;
}

bool ModelAssetStore::Contains(const ModelHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

const CpuModel* ModelAssetStore::GetModel(const ModelHandle handle) const noexcept
{
    const Slot* slot = Find(handle);
    return slot == nullptr ? nullptr : &slot->model;
}

const std::filesystem::path* ModelAssetStore::GetSourcePath(const ModelHandle handle) const noexcept
{
    const Slot* slot = Find(handle);
    return slot == nullptr ? nullptr : &slot->sourcePath;
}
} // namespace vanta::renderer
