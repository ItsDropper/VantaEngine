#pragma once

#include "vanta/assets/ObjLoader.hpp"
#include "vanta/math/Math.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace vanta::renderer
{
template <typename Tag>
struct ResourceHandle
{
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr bool operator==(const ResourceHandle&) const noexcept = default;
};

struct MeshTag;
struct MaterialTag;
struct InstanceTag;
using MeshHandle = ResourceHandle<MeshTag>;
using MaterialHandle = ResourceHandle<MaterialTag>;
using InstanceHandle = ResourceHandle<InstanceTag>;

struct MeshDescription
{
    std::vector<assets::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct MaterialDescription
{
    math::Vec3 baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.8F;
    float metallic = 0.0F;
    std::filesystem::path baseColorTexture;
    std::filesystem::path normalTexture;
    std::filesystem::path roughnessTexture;
    std::filesystem::path metallicTexture;
    std::filesystem::path ambientOcclusionTexture;
    bool packedMetallicRoughness = false;
};

struct FrameView
{
    math::Mat4 view = math::Mat4::Identity();
    math::Mat4 projection = math::Mat4::Identity();
    math::Vec3 cameraPosition{};
    float elapsedSeconds = 0.0F;
    std::uint64_t frameNumber = 0;
};

struct MeshResource
{
    std::vector<assets::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct MaterialResource
{
    MaterialDescription description;
};

struct DrawItem
{
    std::shared_ptr<const MeshResource> mesh;
    std::shared_ptr<const MaterialResource> material;
    math::Mat4 world = math::Mat4::Identity();
};

struct SceneFrame
{
    FrameView view;
    std::vector<DrawItem> draws;
};

// Game-owned, renderer-independent render submission. Meshes and materials are immutable
// after creation; instances own only references and transforms. A frame snapshot retains
// shared ownership of submitted resources until the renderer has finished consuming it.
class SceneSubmission final
{
public:
    [[nodiscard]] MeshHandle CreateMesh(MeshDescription description);
    [[nodiscard]] bool DestroyMesh(MeshHandle handle) noexcept;
    [[nodiscard]] bool Contains(MeshHandle handle) const noexcept;

    [[nodiscard]] MaterialHandle CreateMaterial(MaterialDescription description);
    [[nodiscard]] bool DestroyMaterial(MaterialHandle handle) noexcept;
    [[nodiscard]] bool Contains(MaterialHandle handle) const noexcept;

    [[nodiscard]] InstanceHandle CreateInstance(
        MeshHandle mesh, MaterialHandle material,
        const math::Mat4& world = math::Mat4::Identity());
    [[nodiscard]] bool DestroyInstance(InstanceHandle handle) noexcept;
    [[nodiscard]] bool SetResources(InstanceHandle handle, MeshHandle mesh, MaterialHandle material) noexcept;
    [[nodiscard]] bool SetTransform(InstanceHandle handle, const math::Mat4& world) noexcept;
    [[nodiscard]] bool SetVisible(InstanceHandle handle, bool visible) noexcept;
    [[nodiscard]] bool Contains(InstanceHandle handle) const noexcept;

    [[nodiscard]] SceneFrame BuildFrame(const FrameView& view) const;
    void Clear() noexcept;

    [[nodiscard]] std::size_t MeshCount() const noexcept { return m_meshCount; }
    [[nodiscard]] std::size_t MaterialCount() const noexcept { return m_materialCount; }
    [[nodiscard]] std::size_t InstanceCount() const noexcept { return m_instanceCount; }

private:
    template <typename Resource>
    struct ResourceSlot
    {
        std::shared_ptr<const Resource> resource;
        std::uint32_t generation = 1;
    };

    struct InstanceSlot
    {
        MeshHandle mesh{};
        MaterialHandle material{};
        math::Mat4 world = math::Mat4::Identity();
        std::uint32_t generation = 1;
        bool occupied = false;
        bool visible = true;
    };

    [[nodiscard]] static bool IsFinite(const math::Mat4& matrix) noexcept;
    [[nodiscard]] static bool IsValidWorldTransform(const math::Mat4& matrix) noexcept;
    [[nodiscard]] static bool IsValid(const MaterialDescription& description) noexcept;
    [[nodiscard]] const MeshResource* Find(MeshHandle handle) const noexcept;
    [[nodiscard]] const MaterialResource* Find(MaterialHandle handle) const noexcept;
    [[nodiscard]] InstanceSlot* Find(InstanceHandle handle) noexcept;
    [[nodiscard]] const InstanceSlot* Find(InstanceHandle handle) const noexcept;

    std::deque<ResourceSlot<MeshResource>> m_meshes;
    std::deque<ResourceSlot<MaterialResource>> m_materials;
    std::deque<InstanceSlot> m_instances;
    std::size_t m_meshCount = 0;
    std::size_t m_materialCount = 0;
    std::size_t m_instanceCount = 0;
};
} // namespace vanta::renderer
