#include "vanta/core/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vanta
{
namespace
{
[[nodiscard]] bool IsFinite(const math::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinite(const math::Quaternion& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] std::optional<math::Mat4> Invert(const math::Mat4& matrix) noexcept
{
    double augmented[4][8]{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const double value = matrix.At(row, column);
            if (!std::isfinite(value))
                return std::nullopt;
            augmented[row][column] = value;
        }
        augmented[row][row + 4] = 1.0;
    }

    for (int column = 0; column < 4; ++column)
    {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row)
            if (std::abs(augmented[row][column]) >
                std::abs(augmented[pivot][column]))
                pivot = row;
        if (std::abs(augmented[pivot][column]) <= 1.0e-12)
            return std::nullopt;
        if (pivot != column)
            for (int item = 0; item < 8; ++item)
                std::swap(augmented[pivot][item], augmented[column][item]);

        const double divisor = augmented[column][column];
        for (double& value : augmented[column])
            value /= divisor;
        for (int row = 0; row < 4; ++row)
        {
            if (row == column)
                continue;
            const double factor = augmented[row][column];
            for (int item = 0; item < 8; ++item)
                augmented[row][item] -= factor * augmented[column][item];
        }
    }

    math::Mat4 inverse{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
        {
            const double value = augmented[row][column + 4];
            if (!std::isfinite(value) ||
                std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
                return std::nullopt;
            inverse.At(row, column) = static_cast<float>(value);
        }
    return inverse;
}

struct DecomposedTransform
{
    math::Vec3 position{};
    math::Quaternion rotation{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

[[nodiscard]] std::optional<DecomposedTransform> DecomposeTrs(
    const math::Mat4& matrix) noexcept
{
    constexpr double tolerance = 1.0e-4;
    if (std::abs(matrix.At(3, 0)) > tolerance ||
        std::abs(matrix.At(3, 1)) > tolerance ||
        std::abs(matrix.At(3, 2)) > tolerance ||
        std::abs(matrix.At(3, 3) - 1.0F) > tolerance)
        return std::nullopt;

    math::Vec3 columns[3]{
        {matrix.At(0, 0), matrix.At(1, 0), matrix.At(2, 0)},
        {matrix.At(0, 1), matrix.At(1, 1), matrix.At(2, 1)},
        {matrix.At(0, 2), matrix.At(1, 2), matrix.At(2, 2)}
    };
    double lengths[3]{};
    for (int column = 0; column < 3; ++column)
    {
        const auto& value = columns[column];
        lengths[column] = std::sqrt(
            static_cast<double>(value.x) * value.x +
            static_cast<double>(value.y) * value.y +
            static_cast<double>(value.z) * value.z);
        if (!std::isfinite(lengths[column]) || lengths[column] <= 1.0e-8 ||
            lengths[column] > std::numeric_limits<float>::max())
            return std::nullopt;
        columns[column] = value / static_cast<float>(lengths[column]);
    }

    const double determinant =
        static_cast<double>(columns[0].x) *
            (static_cast<double>(columns[1].y) * columns[2].z -
             static_cast<double>(columns[1].z) * columns[2].y) -
        static_cast<double>(columns[1].x) *
            (static_cast<double>(columns[0].y) * columns[2].z -
             static_cast<double>(columns[0].z) * columns[2].y) +
        static_cast<double>(columns[2].x) *
            (static_cast<double>(columns[0].y) * columns[1].z -
             static_cast<double>(columns[0].z) * columns[1].y);
    if (!std::isfinite(determinant) || std::abs(std::abs(determinant) - 1.0) > tolerance ||
        std::abs(columns[0].Dot(columns[1])) > tolerance ||
        std::abs(columns[0].Dot(columns[2])) > tolerance ||
        std::abs(columns[1].Dot(columns[2])) > tolerance)
        return std::nullopt;

    math::Vec3 scale{static_cast<float>(lengths[0]),
                     static_cast<float>(lengths[1]),
                     static_cast<float>(lengths[2])};
    if (determinant < 0.0)
    {
        scale.x = -scale.x;
        columns[0] = -columns[0];
    }

    const double r00 = columns[0].x, r10 = columns[0].y, r20 = columns[0].z;
    const double r01 = columns[1].x, r11 = columns[1].y, r21 = columns[1].z;
    const double r02 = columns[2].x, r12 = columns[2].y, r22 = columns[2].z;
    const double trace = r00 + r11 + r22;
    math::Quaternion rotation{};
    if (trace > 0.0)
    {
        const double factor = std::sqrt(trace + 1.0) * 2.0;
        rotation = {static_cast<float>((r21 - r12) / factor),
                    static_cast<float>((r02 - r20) / factor),
                    static_cast<float>((r10 - r01) / factor),
                    static_cast<float>(0.25 * factor)};
    }
    else if (r00 > r11 && r00 > r22)
    {
        const double factor = std::sqrt(1.0 + r00 - r11 - r22) * 2.0;
        rotation = {static_cast<float>(0.25 * factor),
                    static_cast<float>((r01 + r10) / factor),
                    static_cast<float>((r02 + r20) / factor),
                    static_cast<float>((r21 - r12) / factor)};
    }
    else if (r11 > r22)
    {
        const double factor = std::sqrt(1.0 + r11 - r00 - r22) * 2.0;
        rotation = {static_cast<float>((r01 + r10) / factor),
                    static_cast<float>(0.25 * factor),
                    static_cast<float>((r12 + r21) / factor),
                    static_cast<float>((r02 - r20) / factor)};
    }
    else
    {
        const double factor = std::sqrt(1.0 + r22 - r00 - r11) * 2.0;
        rotation = {static_cast<float>((r02 + r20) / factor),
                    static_cast<float>((r12 + r21) / factor),
                    static_cast<float>(0.25 * factor),
                    static_cast<float>((r10 - r01) / factor)};
    }

    const auto normalizedRotation = rotation.Normalized();
    if (!normalizedRotation)
        return std::nullopt;
    DecomposedTransform result{
        {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)},
        *normalizedRotation,
        scale
    };
    if (!IsFinite(result.position) || !IsFinite(result.scale))
        return std::nullopt;
    return result;
}
}

bool Transform::SetPosition(const math::Vec3& position) noexcept
{
    if (!IsFinite(position))
        return false;
    m_position = position;
    return true;
}

bool Transform::SetRotation(const math::Quaternion& rotation) noexcept
{
    if (!IsFinite(rotation))
        return false;
    const double lengthSquared =
        static_cast<double>(rotation.x) * rotation.x +
        static_cast<double>(rotation.y) * rotation.y +
        static_cast<double>(rotation.z) * rotation.z +
        static_cast<double>(rotation.w) * rotation.w;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12)
        return false;
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    m_rotation = {rotation.x * inverseLength, rotation.y * inverseLength,
                  rotation.z * inverseLength, rotation.w * inverseLength};
    return true;
}

bool Transform::SetScale(const math::Vec3& scale) noexcept
{
    if (!IsFinite(scale))
        return false;
    m_scale = scale;
    return true;
}

math::Mat4 Transform::Matrix() const noexcept
{
    return math::Mat4::Translation(m_position) *
           math::Mat4::Rotation(m_rotation) *
           math::Mat4::Scale(m_scale);
}

const Scene::Slot* Scene::Find(const EntityHandle entity) const noexcept
{
    if (!entity.IsValid() || entity.index >= m_slots.size())
        return nullptr;
    const Slot& slot = m_slots[entity.index];
    return slot.occupied && slot.generation == entity.generation ? &slot : nullptr;
}

Scene::Slot* Scene::Find(const EntityHandle entity) noexcept
{
    if (!entity.IsValid() || entity.index >= m_slots.size())
        return nullptr;
    Slot& slot = m_slots[entity.index];
    return slot.occupied && slot.generation == entity.generation ? &slot : nullptr;
}

EntityHandle Scene::CreateEntity(std::string name)
{
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        Slot& slot = m_slots[index];
        if (!slot.occupied)
        {
            slot.transform = {};
            slot.name = std::move(name);
            slot.parent = {};
            slot.active = true;
            slot.occupied = true;
            ++m_entityCount;
            return {static_cast<std::uint32_t>(index), slot.generation};
        }
    }

    if (m_slots.size() >= EntityHandle::kInvalidIndex)
        return {};
    Slot slot{};
    slot.name = std::move(name);
    slot.occupied = true;
    m_slots.push_back(std::move(slot));
    ++m_entityCount;
    return {static_cast<std::uint32_t>(m_slots.size() - 1U), m_slots.back().generation};
}

bool Scene::DestroyEntity(const EntityHandle entity) noexcept
{
    Slot* slot = Find(entity);
    if (slot == nullptr)
        return false;

    for (Slot& candidate : m_slots)
    {
        if (candidate.occupied && candidate.parent == entity)
            candidate.parent = {};
    }

    slot->occupied = false;
    slot->active = false;
    slot->parent = {};
    slot->name.clear();
    slot->transform = {};
    ++slot->generation;
    if (slot->generation == 0U)
        slot->generation = 1U;
    --m_entityCount;
    return true;
}


void Scene::Clear() noexcept
{
    for (Slot& slot : m_slots)
    {
        if (!slot.occupied)
            continue;
        slot.occupied = false;
        slot.active = false;
        slot.parent = {};
        slot.name.clear();
        slot.transform = {};
        ++slot.generation;
        if (slot.generation == 0U)
            slot.generation = 1U;
    }
    m_entityCount = 0;
}

bool Scene::IsAlive(const EntityHandle entity) const noexcept
{
    return Find(entity) != nullptr;
}

Transform* Scene::GetTransform(const EntityHandle entity) noexcept
{
    Slot* slot = Find(entity);
    return slot == nullptr ? nullptr : &slot->transform;
}

const Transform* Scene::GetTransform(const EntityHandle entity) const noexcept
{
    const Slot* slot = Find(entity);
    return slot == nullptr ? nullptr : &slot->transform;
}

const std::string* Scene::GetName(const EntityHandle entity) const noexcept
{
    const Slot* slot = Find(entity);
    return slot == nullptr ? nullptr : &slot->name;
}

bool Scene::SetName(const EntityHandle entity, std::string name)
{
    Slot* slot = Find(entity);
    if (slot == nullptr)
        return false;
    slot->name = std::move(name);
    return true;
}

bool Scene::SetActive(const EntityHandle entity, const bool active) noexcept
{
    Slot* slot = Find(entity);
    if (slot == nullptr)
        return false;
    slot->active = active;
    return true;
}

bool Scene::IsActive(const EntityHandle entity) const noexcept
{
    const Slot* slot = Find(entity);
    return slot != nullptr && slot->active;
}

bool Scene::IsActiveInHierarchy(const EntityHandle entity) const noexcept
{
    const Slot* slot = Find(entity);
    if (slot == nullptr)
        return false;
    EntityHandle current = entity;
    std::size_t visited = 0;
    while (current.IsValid())
    {
        const Slot* ancestor = Find(current);
        if (ancestor == nullptr || !ancestor->active || ++visited > m_slots.size())
            return false;
        current = ancestor->parent;
    }
    return true;
}

bool Scene::SetParent(
    const EntityHandle entity,
    const EntityHandle parent,
    const bool preserveWorldTransform)
{
    Slot* child = Find(entity);
    if (child == nullptr || entity == parent)
        return false;
    if (parent.IsValid() && Find(parent) == nullptr)
        return false;

    EntityHandle ancestor = parent;
    std::size_t visited = 0;
    while (ancestor.IsValid())
    {
        if (ancestor == entity)
            return false;
        const Slot* ancestorSlot = Find(ancestor);
        if (ancestorSlot == nullptr || ++visited > m_slots.size())
            return false;
        ancestor = ancestorSlot->parent;
    }

    std::optional<DecomposedTransform> preservedTransform;
    if (preserveWorldTransform)
    {
        const auto oldWorld = BuildWorldMatrix(entity);
        const auto parentWorld = parent.IsValid()
            ? BuildWorldMatrix(parent)
            : std::optional<math::Mat4>{math::Mat4::Identity()};
        if (!oldWorld || !parentWorld)
            return false;
        const auto inverseParent = Invert(*parentWorld);
        if (!inverseParent)
            return false;
        preservedTransform = DecomposeTrs(*inverseParent * *oldWorld);
        if (!preservedTransform)
            return false;
    }

    child->parent = parent;
    if (preservedTransform)
    {
        (void)child->transform.SetPosition(preservedTransform->position);
        (void)child->transform.SetRotation(preservedTransform->rotation);
        (void)child->transform.SetScale(preservedTransform->scale);
    }
    return true;
}

EntityHandle Scene::GetParent(const EntityHandle entity) const noexcept
{
    const Slot* slot = Find(entity);
    return slot == nullptr ? EntityHandle{} : slot->parent;
}

std::optional<math::Mat4> Scene::BuildWorldMatrix(const EntityHandle entity) const
{
    if (Find(entity) == nullptr)
        return std::nullopt;
    std::vector<const Slot*> chain;
    chain.reserve(16);
    EntityHandle current = entity;
    while (current.IsValid())
    {
        const Slot* slot = Find(current);
        if (slot == nullptr || chain.size() >= m_slots.size())
            return std::nullopt;
        chain.push_back(slot);
        current = slot->parent;
    }

    math::Mat4 world = math::Mat4::Identity();
    for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
        world = world * (*iterator)->transform.Matrix();
    return world;
}

std::optional<math::Mat4> Scene::GetWorldMatrix(const EntityHandle entity) const
{
    return BuildWorldMatrix(entity);
}

std::vector<SceneEntitySnapshot> Scene::CreateSnapshot() const
{
    struct PendingEntity
    {
        std::size_t index = 0;
        math::Mat4 parentWorld = math::Mat4::Identity();
        bool parentActive = true;
    };

    std::vector<std::vector<std::size_t>> children(m_slots.size());
    std::vector<std::size_t> roots;
    roots.reserve(m_entityCount);
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (!slot.occupied)
            continue;
        if (slot.parent.IsValid() && Find(slot.parent) != nullptr)
            children[slot.parent.index].push_back(index);
        else
            roots.push_back(index);
    }

    std::vector<PendingEntity> pending;
    pending.reserve(m_entityCount);
    for (auto iterator = roots.rbegin(); iterator != roots.rend(); ++iterator)
        pending.push_back({*iterator, math::Mat4::Identity(), true});

    std::vector<SceneEntitySnapshot> snapshot;
    snapshot.reserve(m_entityCount);
    while (!pending.empty())
    {
        const PendingEntity current = pending.back();
        pending.pop_back();
        const Slot& slot = m_slots[current.index];
        if (!slot.occupied)
            continue;

        const math::Mat4 world = current.parentWorld * slot.transform.Matrix();
        const bool active = current.parentActive && slot.active;
        const EntityHandle entity{static_cast<std::uint32_t>(current.index), slot.generation};
        snapshot.push_back({entity, slot.name, world, active});

        const auto& entityChildren = children[current.index];
        for (auto iterator = entityChildren.rbegin(); iterator != entityChildren.rend(); ++iterator)
            pending.push_back({*iterator, world, active});
    }
    return snapshot;
}
} // namespace vanta
