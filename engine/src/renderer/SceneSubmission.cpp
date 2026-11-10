#include "vanta/renderer/SceneSubmission.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace vanta::renderer
{
namespace
{
std::atomic<std::uint64_t> g_nextResourceRevision{1U};

std::uint64_t NextResourceRevision() noexcept
{
    std::uint64_t revision = g_nextResourceRevision.fetch_add(1U, std::memory_order_relaxed);
    if (revision == 0U)
        revision = g_nextResourceRevision.fetch_add(1U, std::memory_order_relaxed);
    return revision;
}

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

bool IsFiniteMatrix(const math::Mat4& matrix) noexcept
{
    return std::all_of(matrix.elements.begin(), matrix.elements.end(),
        [](const float value) { return std::isfinite(value); });
}
} // namespace

SceneSubmission::SceneSubmission()
    : m_resourceRevision(NextResourceRevision())
{
}

bool SceneSubmission::IsFinite(const math::Mat4& matrix) noexcept
{
    for (const float value : matrix.elements)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

bool SceneSubmission::IsValidWorldTransform(const math::Mat4& matrix) noexcept
{
    const float a = matrix.At(0, 0), b = matrix.At(0, 1), c = matrix.At(0, 2);
    const float d = matrix.At(1, 0), e = matrix.At(1, 1), f = matrix.At(1, 2);
    const float g = matrix.At(2, 0), h = matrix.At(2, 1), i = matrix.At(2, 2);
    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-8F;
}

bool SceneSubmission::IsValid(const MaterialDescription& description) noexcept
{
    const std::array<const std::filesystem::path*, 5> texturePaths = {
        &description.baseColorTexture, &description.normalTexture,
        &description.roughnessTexture, &description.metallicTexture,
        &description.ambientOcclusionTexture
    };
    const bool pathsValid = std::all_of(texturePaths.begin(), texturePaths.end(),
        [](const std::filesystem::path* path)
        {
            const std::filesystem::path::string_type& native = path->native();
            return native.find(std::filesystem::path::value_type{}) ==
                    std::filesystem::path::string_type::npos &&
                (path->empty() || path->is_absolute());
        });
    return pathsValid &&
           std::isfinite(description.baseColor.x) && description.baseColor.x >= 0.0F &&
           std::isfinite(description.baseColor.y) && description.baseColor.y >= 0.0F &&
           std::isfinite(description.baseColor.z) && description.baseColor.z >= 0.0F &&
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
            !std::isfinite(vertex.u) || !std::isfinite(vertex.v) ||
            !std::isfinite(vertex.normal.LengthSquared()) ||
            vertex.normal.LengthSquared() <= 1.0e-12F)
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
    m_resourceRevision = NextResourceRevision();
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
    m_resourceRevision = NextResourceRevision();
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
    m_resourceRevision = NextResourceRevision();
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
    m_resourceRevision = NextResourceRevision();
    return true;
}

bool SceneSubmission::Contains(const MaterialHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

InstanceHandle SceneSubmission::CreateInstance(
    const MeshHandle mesh, const MaterialHandle material, const math::Mat4& world)
{
    if (Find(mesh) == nullptr || Find(material) == nullptr || !IsFinite(world) ||
        !IsValidWorldTransform(world))
        return {};

    // SetVisible is noexcept, so reserve enough space here for every live instance
    // before mutating slot state. Grow geometrically to avoid reallocating per spawn.
    const std::size_t requiredVisibleCapacity = m_instanceCount + 1U;
    if (m_visibleInstances.capacity() < requiredVisibleCapacity)
    {
        const std::size_t capacity = m_visibleInstances.capacity();
        const std::size_t maxCapacity = m_visibleInstances.max_size();
        const std::size_t doubledCapacity = capacity == 0U ? 8U :
            (capacity > maxCapacity / 2U ? maxCapacity : capacity * 2U);
        m_visibleInstances.reserve(std::max(requiredVisibleCapacity, doubledCapacity));
    }

    std::uint32_t index = InstanceHandle::kInvalidIndex;
    if (m_freeInstanceHead != InstanceHandle::kInvalidIndex)
    {
        index = m_freeInstanceHead;
        InstanceSlot& freeSlot = m_instances[index];
        m_freeInstanceHead = freeSlot.nextFreeIndex;
        freeSlot.nextFreeIndex = InstanceHandle::kInvalidIndex;
    }
    else
    {
        if (m_instances.size() >= InstanceHandle::kInvalidIndex)
            return {};
        index = static_cast<std::uint32_t>(m_instances.size());
        m_instances.emplace_back();
    }
    auto& slot = m_instances[index];
    slot.mesh = mesh;
    slot.material = material;
    slot.world = world;
    slot.visible = true;
    slot.occupied = true;
    slot.visiblePosition = static_cast<std::uint32_t>(m_visibleInstances.size());
    m_visibleInstances.push_back(index);
    ++m_instanceCount;
    return {static_cast<std::uint32_t>(index), slot.generation};
}

bool SceneSubmission::DestroyInstance(const InstanceHandle handle) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    if (slot->visible)
    {
        const std::uint32_t position = slot->visiblePosition;
        const std::uint32_t lastIndex = m_visibleInstances.back();
        m_visibleInstances[position] = lastIndex;
        m_instances[lastIndex].visiblePosition = position;
        m_visibleInstances.pop_back();
    }
    slot->occupied = false;
    slot->visible = false;
    slot->visiblePosition = InstanceHandle::kInvalidIndex;
    slot->mesh = {};
    slot->material = {};
    slot->world = math::Mat4::Identity();
    slot->nextFreeIndex = m_freeInstanceHead;
    m_freeInstanceHead = handle.index;
    AdvanceGeneration(*slot);
    --m_instanceCount;
    return true;
}

bool SceneSubmission::SetResources(
    const InstanceHandle handle, const MeshHandle mesh, const MaterialHandle material) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr || Find(mesh) == nullptr || Find(material) == nullptr)
        return false;
    slot->mesh = mesh;
    slot->material = material;
    return true;
}

bool SceneSubmission::SetTransform(
    const InstanceHandle handle, const math::Mat4& world) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr || !IsFinite(world) || !IsValidWorldTransform(world))
        return false;
    slot->world = world;
    return true;
}

bool SceneSubmission::SetVisible(const InstanceHandle handle, const bool visible) noexcept
{
    InstanceSlot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    if (slot->visible == visible)
        return true;

    if (visible)
    {
        slot->visiblePosition = static_cast<std::uint32_t>(m_visibleInstances.size());
        m_visibleInstances.push_back(handle.index);
        slot->visible = true;
        return true;
    }

    const std::uint32_t position = slot->visiblePosition;
    const std::uint32_t lastIndex = m_visibleInstances.back();
    m_visibleInstances[position] = lastIndex;
    m_instances[lastIndex].visiblePosition = position;
    m_visibleInstances.pop_back();
    slot->visible = false;
    slot->visiblePosition = InstanceHandle::kInvalidIndex;
    return true;
}

bool SceneSubmission::Contains(const InstanceHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

bool ValidateFrameView(const FrameView& view, std::string& error)
{
    if (!IsFiniteMatrix(view.view) || !IsFiniteMatrix(view.projection) ||
        !std::isfinite(view.cameraPosition.x) || !std::isfinite(view.cameraPosition.y) ||
        !std::isfinite(view.cameraPosition.z) || !std::isfinite(view.elapsedSeconds) ||
        view.pointLightCount > MaximumPointLightsPerFrame)
    {
        error = "Frame view contains invalid camera data or exceeds the point-light limit.";
        return false;
    }
    for (std::uint32_t index = 0U; index < view.pointLightCount; ++index)
    {
        const PointLight& light = view.pointLights[index];
        if (!std::isfinite(light.position.x) || !std::isfinite(light.position.y) ||
            !std::isfinite(light.position.z) || !std::isfinite(light.range) ||
            light.range <= 0.0F || light.range > 10000.0F ||
            !std::isfinite(light.color.x) || !std::isfinite(light.color.y) ||
            !std::isfinite(light.color.z) || light.color.x < 0.0F ||
            light.color.y < 0.0F || light.color.z < 0.0F ||
            light.color.x > 1000.0F || light.color.y > 1000.0F || light.color.z > 1000.0F ||
            !std::isfinite(light.intensity) || light.intensity < 0.0F ||
            light.intensity > 100000.0F)
        {
            error = "Frame view contains an invalid point-light position, range, color, or intensity.";
            return false;
        }
    }
    error.clear();
    return true;
}

SceneFrame SceneSubmission::BuildFrame(const FrameView& view) const
{
    SceneFrame frame;
    frame.view = view;
    frame.resourceRevision = m_resourceRevision;
    std::string validationError;
    if (!ValidateFrameView(view, validationError))
        return frame;

    frame.draws.reserve(m_visibleInstances.size());
    for (const std::uint32_t instanceIndex : m_visibleInstances)
    {
        const InstanceSlot& instance = m_instances[instanceIndex];
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
    m_freeInstanceHead = InstanceHandle::kInvalidIndex;
    m_visibleInstances.clear();
    for (std::size_t index = 0; index < m_instances.size(); ++index)
    {
        auto& slot = m_instances[index];
        if (slot.occupied)
        {
            slot.occupied = false;
            slot.visible = false;
            slot.visiblePosition = InstanceHandle::kInvalidIndex;
            slot.mesh = {};
            slot.material = {};
            slot.world = math::Mat4::Identity();
            AdvanceGeneration(slot);
        }
        slot.nextFreeIndex = m_freeInstanceHead;
        m_freeInstanceHead = static_cast<std::uint32_t>(index);
    }
    m_meshCount = 0;
    m_materialCount = 0;
    m_instanceCount = 0;
    m_resourceRevision = NextResourceRevision();
}
} // namespace vanta::renderer
