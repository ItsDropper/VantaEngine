#include "vanta/physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>

namespace vanta
{
namespace
{
constexpr float kDirectionEpsilon = 1.0e-8F;

[[nodiscard]] bool Intersects(const Aabb& left, const Aabb& right) noexcept
{
    return left.minimum.x <= right.maximum.x && left.maximum.x >= right.minimum.x &&
           left.minimum.y <= right.maximum.y && left.maximum.y >= right.minimum.y &&
           left.minimum.z <= right.maximum.z && left.maximum.z >= right.minimum.z;
}

[[nodiscard]] math::Vec3 ClosestPoint(const Aabb& bounds, const math::Vec3& point) noexcept
{
    return {
        std::clamp(point.x, bounds.minimum.x, bounds.maximum.x),
        std::clamp(point.y, bounds.minimum.y, bounds.maximum.y),
        std::clamp(point.z, bounds.minimum.z, bounds.maximum.z)
    };
}
}

bool PhysicsWorld::IsFinite(const math::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool PhysicsWorld::IsValid(const Aabb& bounds) noexcept
{
    return IsFinite(bounds.minimum) && IsFinite(bounds.maximum) &&
           bounds.minimum.x <= bounds.maximum.x &&
           bounds.minimum.y <= bounds.maximum.y &&
           bounds.minimum.z <= bounds.maximum.z;
}

bool PhysicsWorld::Matches(const Slot& slot, const std::uint32_t layerMask,
                           const bool includeTriggers) noexcept
{
    return slot.occupied && (slot.description.layer & layerMask) != 0U &&
           (includeTriggers || !slot.description.isTrigger);
}

const PhysicsWorld::Slot* PhysicsWorld::Find(const ColliderHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.index >= m_slots.size())
        return nullptr;
    const Slot& slot = m_slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

PhysicsWorld::Slot* PhysicsWorld::Find(const ColliderHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= m_slots.size())
        return nullptr;
    Slot& slot = m_slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

ColliderHandle PhysicsWorld::AddCollider(const ColliderDescription& description)
{
    if (!IsValid(description.bounds) || description.layer == 0U)
        return {};

    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        Slot& slot = m_slots[index];
        if (!slot.occupied)
        {
            slot.description = description;
            slot.occupied = true;
            ++m_colliderCount;
            return {static_cast<std::uint32_t>(index), slot.generation};
        }
    }

    if (m_slots.size() >= ColliderHandle::kInvalidIndex)
        return {};
    Slot slot{};
    slot.description = description;
    slot.occupied = true;
    m_slots.push_back(slot);
    ++m_colliderCount;
    return {static_cast<std::uint32_t>(m_slots.size() - 1U), slot.generation};
}

bool PhysicsWorld::UpdateCollider(
    const ColliderHandle handle, const ColliderDescription& description) noexcept
{
    if (!IsValid(description.bounds) || description.layer == 0U)
        return false;
    Slot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    slot->description = description;
    return true;
}

bool PhysicsWorld::RemoveCollider(const ColliderHandle handle) noexcept
{
    Slot* slot = Find(handle);
    if (slot == nullptr)
        return false;
    slot->occupied = false;
    slot->description = {};
    ++slot->generation;
    if (slot->generation == 0U)
        slot->generation = 1U;
    --m_colliderCount;
    return true;
}

bool PhysicsWorld::Contains(const ColliderHandle handle) const noexcept
{
    return Find(handle) != nullptr;
}

bool PhysicsWorld::Raycast(
    const math::Vec3& origin,
    const math::Vec3& direction,
    const float maximumDistance,
    RaycastHit& hit,
    const std::uint32_t layerMask,
    const bool includeTriggers) const noexcept
{
    hit = {};
    if (!IsFinite(origin) || !IsFinite(direction) || !std::isfinite(maximumDistance) ||
        maximumDistance < 0.0F || layerMask == 0U)
        return false;

    const double directionLengthSquared =
        static_cast<double>(direction.x) * direction.x +
        static_cast<double>(direction.y) * direction.y +
        static_cast<double>(direction.z) * direction.z;
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 1.0e-16)
        return false;
    const float inverseDirectionLength = static_cast<float>(1.0 / std::sqrt(directionLengthSquared));
    const math::Vec3 normalizedDirection = direction * inverseDirectionLength;

    bool found = false;
    float closestDistance = maximumDistance;
    RaycastHit closestHit{};
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (!Matches(slot, layerMask, includeTriggers))
            continue;

        const Aabb& bounds = slot.description.bounds;
        const bool inside = origin.x >= bounds.minimum.x && origin.x <= bounds.maximum.x &&
                            origin.y >= bounds.minimum.y && origin.y <= bounds.maximum.y &&
                            origin.z >= bounds.minimum.z && origin.z <= bounds.maximum.z;
        float nearDistance = 0.0F;
        float farDistance = closestDistance;
        math::Vec3 nearNormal{};

        const float origins[] = {origin.x, origin.y, origin.z};
        const float directions[] = {normalizedDirection.x, normalizedDirection.y, normalizedDirection.z};
        const float minimums[] = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
        const float maximums[] = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};

        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float component = directions[axis];
            if (std::abs(component) <= kDirectionEpsilon)
            {
                if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                {
                    intersects = false;
                    break;
                }
                continue;
            }

            const float inverse = 1.0F / component;
            float first = (minimums[axis] - origins[axis]) * inverse;
            float second = (maximums[axis] - origins[axis]) * inverse;
            float normalSign = -1.0F;
            if (first > second)
            {
                std::swap(first, second);
                normalSign = 1.0F;
            }
            if (first > nearDistance)
            {
                nearDistance = first;
                nearNormal = {};
                if (axis == 0) nearNormal.x = normalSign;
                if (axis == 1) nearNormal.y = normalSign;
                if (axis == 2) nearNormal.z = normalSign;
            }
            farDistance = std::min(farDistance, second);
            if (nearDistance > farDistance)
            {
                intersects = false;
                break;
            }
        }

        if (!intersects || farDistance < 0.0F || nearDistance > closestDistance)
            continue;
        const float distance = inside ? 0.0F : nearDistance;
        if (distance > closestDistance)
            continue;
        closestDistance = distance;
        closestHit.collider = {static_cast<std::uint32_t>(index), slot.generation};
        closestHit.distance = distance;
        closestHit.position = origin + normalizedDirection * distance;
        closestHit.startedInside = inside;
        closestHit.normal = inside ? -normalizedDirection : nearNormal;
        found = true;
    }

    if (found)
        hit = closestHit;
    return found;
}

bool PhysicsWorld::OverlapAabb(
    const Aabb& bounds,
    std::vector<ColliderHandle>& results,
    const std::uint32_t layerMask,
    const bool includeTriggers) const
{
    results.clear();
    if (!IsValid(bounds) || layerMask == 0U)
        return false;
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (Matches(slot, layerMask, includeTriggers) &&
            Intersects(bounds, slot.description.bounds))
        {
            results.push_back({static_cast<std::uint32_t>(index), slot.generation});
        }
    }
    return true;
}

bool PhysicsWorld::OverlapSphere(
    const math::Vec3& center,
    const float radius,
    std::vector<ColliderHandle>& results,
    const std::uint32_t layerMask,
    const bool includeTriggers) const
{
    results.clear();
    if (!IsFinite(center) || !std::isfinite(radius) || radius < 0.0F || layerMask == 0U)
        return false;
    const float radiusSquared = radius * radius;
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (!Matches(slot, layerMask, includeTriggers))
            continue;
        const math::Vec3 closest = ClosestPoint(slot.description.bounds, center);
        const math::Vec3 offset = center - closest;
        if (offset.LengthSquared() <= radiusSquared)
            results.push_back({static_cast<std::uint32_t>(index), slot.generation});
    }
    return true;
}
} // namespace vanta
