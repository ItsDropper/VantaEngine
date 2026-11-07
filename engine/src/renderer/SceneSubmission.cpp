#include "vanta/renderer/SceneSubmission.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace vanta::renderer
{
namespace
{
template <typename Slots>
std::uint32_t FindFreeSlot(const Slots& slots)
{
    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        if (!slots[index].resource)
            return static_cast<std::uint32_t>(index);
    }
    if (slots.size() >= MeshHandle::kInvalidIndex)
        return MeshHandle::kInvalidIndex;
    return static_cast<std::uint32_t>(slots.size());
}

template <typename Slot>
void AdvanceGeneration(Slot& slot) noexcept
{
    ++slot.generation;
    if (slot.generation == 0U)
        slot.generation = 1U;
}
} // namespace

bool SceneSubmission::IsFinite(const math::Mat4& matrix) noexcept
{
    for (const float value : matrix.elements)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

bool SceneSubmission::IsValid(const MaterialDescription& description) noexcept
{
    return std::isfinite(description.baseColor.x) &&
           std::isfinite(description.baseColor.y) &&
           std::isfinite(description.baseColor.z) &&
           std::isfinite(description.roughness) &&
           std::isfinite(description.metallic) &&
           description.roughness >= 0.0F && description.roughness <= 1.0F &&
           description.metallic >= 0.0F && description.metallic <= 1.0F;
}

const MeshResource* SceneSubmission::Find(const MeshHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.index >= m_meshes.size())
        return nullptr;
    const auto& slot = m_meshes[handle.index];
    return slot.generation == handle.generation ? slot.resource.get() : nullptr;
}

const MaterialResource* SceneSubmission::Find(const MaterialHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.index >= m_materials.size())
        return nullptr;
    const auto& slot = m_materials[handle.index];
    return slot.generation == handle.generation ? slot.resource.get() : nullptr;
}

SceneSubmission::InstanceSlot* SceneSubmission::Find(const InstanceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= m_instances.size())
        return nullptr;
    InstanceSlot& slot = m_instances[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const SceneSubmission::InstanceSlot* SceneSubmission::Find(const InstanceHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.index >= m_instances.size())
        return nullptr;
    const InstanceSlot& slot = m_instances[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

MeshHandle SceneSubmission::CreateMesh(MeshDescription description)
{
    if (description.vertices.empty() || description.indices.empty() ||
        description.indices.size() % 3U != 0U ||
        description.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        description.indices.size() > std::numeric_limits<std::uint32_t>::max())
        return {};

    for (const auto& vertex : description.vertices)
    {
        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
            !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
            !std::isfinite(vertex.u) || !std::isfinite(vertex.v))
            return {};
    }
    for (const std::uint32_t index : description.indices)
    {
        if (index >= description.vertices.size())
            return {};
    }

    const std::uint32_t index = FindFreeSlot(m_meshes);
    if (index == MeshHandle::kInvalidIndex)
        return {};

    auto resource = std::make_shared<MeshResource>();
    resource->vertices = std::move(description.vertices);
    resource->indices = std::move(description.indices);

    if (index == m_meshes.size())
        m_meshes.emplace_back();
    auto& slot = m_meshes[index];
    slot.resource = std::move(resource);
    ++m_meshCount;
    return {index, slot.generation};
}

bool SceneSubmission::DestroyMesh(const MeshHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= m_meshes.size())
        return false;
    auto& slot = m_meshes[handle.index];
    if (slot.generation != handle.generation || !slot.resource)
        return false;

    for (const auto& instance : m_instances)
    {
        if (instance.occupied && instance.mesh == handle)
            return false;
    }

    slot.resource.reset();
    AdvanceGeneration(slot);
    --m_meshCount;
    return true;
}

bool SceneSubmission::Contains(const MeshHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

MaterialHandle SceneSubmission::CreateMaterial(MaterialDescription description)
{
    if (!IsValid(description))
        return {};

    const std::uint32_t index = FindFreeSlot(m_materials);
    if (index == MaterialHandle::kInvalidIndex)
        return {};

    auto resource = std::make_shared<MaterialResource>();
    resource->description = std::move(description);
    if (index == m_materials.size())
        m_materials.emplace_back();
    auto& slot = m_materials[index];
    slot.resource = std::move(resource);
    ++m_materialCount;
    return {index, slot.generation};
}

bool SceneSubmission::DestroyMaterial(const MaterialHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= m_materials.size())
        return false;
    auto& slot = m_materials[handle.index];
    if (slot.generation != handle.generation || !slot.resource)
        return false;

    for (const auto& instance : m_instances)
    {
        if (instance.occupied && instance.material == handle)
            return false;
    }

    slot.resource.reset();
    AdvanceGeneration(slot);
    --m_materialCount;
    return true;
}

bool SceneSubmission::Contains(const MaterialHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

InstanceHandle SceneSubmission::CreateInstance(
    const MeshHandle mesh, const MaterialHandle material, const math::Mat4& world)
{
    if (Find(mesh) == nullptr || Find(material) == nullptr || !IsFinite(world))
        return {};

    std::size_t index = 0;
    for (; index < m_instances.size(); ++index)
    {
        if (!m_instances[index].occupied)
            break;
    }
    if (index >= InstanceHandle::kInvalidIndex)
        return {};

    if (index == m_instances.size())
        m_instances.emplace_back();
    auto& slot = m_instances[index];
    slot.mesh = mesh;
    slot.material = material;
    slot.world = world;
    slot.visible = true;
    slot.occupied = true;
    ++m_instanceCount;
    return {static_cast<std::uint32_t>(index), slot.generation};
}

bool SceneSubmission::DestroyInstance(const InstanceHandle handle) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    slot->occupied = false;
    slot->visible = false;
    slot->mesh = {};
    slot->material = {};
    slot->world = math::Mat4::Identity();
    AdvanceGeneration(*slot);
    --m_instanceCount;
    return true;
}

bool SceneSubmission::SetTransform(
    const InstanceHandle handle, const math::Mat4& world) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr || !IsFinite(world))
        return false;
    slot->world = world;
    return true;
}

bool SceneSubmission::SetVisible(const InstanceHandle handle, const bool visible) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    slot->visible = visible;
    return true;
}

bool SceneSubmission::Contains(const InstanceHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

SceneFrame SceneSubmission::BuildFrame(const FrameView& view) const
{
    SceneFrame frame;
    frame.view = view;
    if (!IsFinite(view.view) || !IsFinite(view.projection) ||
        !std::isfinite(view.cameraPosition.x) || !std::isfinite(view.cameraPosition.y) ||
        !std::isfinite(view.cameraPosition.z) || !std::isfinite(view.elapsedSeconds))
        return frame;

    frame.draws.reserve(m_instanceCount);
    for (const InstanceSlot& instance : m_instances)
    {
        if (!instance.occupied || !instance.visible)
            continue;
        const auto* meshSlot = instance.mesh.index < m_meshes.size()
            ? &m_meshes[instance.mesh.index] : nullptr;
        const auto* materialSlot = instance.material.index < m_materials.size()
            ? &m_materials[instance.material.index] : nullptr;
        if (meshSlot == nullptr || materialSlot == nullptr ||
            meshSlot->generation != instance.mesh.generation ||
            materialSlot->generation != instance.material.generation ||
            !meshSlot->resource || !materialSlot->resource)
            continue;

        frame.draws.push_back({meshSlot->resource, materialSlot->resource, instance.world});
    }
    return frame;
}

void SceneSubmission::Clear() noexcept
{
    for (auto& slot : m_meshes)
    {
        if (slot.resource)
        {
            slot.resource.reset();
            AdvanceGeneration(slot);
        }
    }
    for (auto& slot : m_materials)
    {
        if (slot.resource)
        {
            slot.resource.reset();
            AdvanceGeneration(slot);
        }
    }
    for (auto& slot : m_instances)
    {
        if (slot.occupied)
        {
            slot.occupied = false;
            slot.visible = false;
            slot.mesh = {};
            slot.material = {};
            slot.world = math::Mat4::Identity();
            AdvanceGeneration(slot);
        }
    }
    m_meshCount = 0;
    m_materialCount = 0;
    m_instanceCount = 0;
}
} // namespace vanta::renderer
