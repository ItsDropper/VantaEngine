#include "vanta/physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>

namespace vanta
{
namespace
{
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


void PhysicsWorld::Clear() noexcept
{
    for (Slot& slot : m_slots)
    {
        if (!slot.occupied)
            continue;
        slot.occupied = false;
        slot.description = {};
        ++slot.generation;
        if (slot.generation == 0U)
            slot.generation = 1U;
    }
    m_colliderCount = 0;
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
    if (!std::isfinite(directionLengthSquared) || directionLengthSquared <= 0.0)
        return false;
    const double inverseDirectionLength = 1.0 / std::sqrt(directionLengthSquared);
    const math::Vec3 normalizedDirection{
        static_cast<float>(static_cast<double>(direction.x) * inverseDirectionLength),
        static_cast<float>(static_cast<double>(direction.y) * inverseDirectionLength),
        static_cast<float>(static_cast<double>(direction.z) * inverseDirectionLength)
    };

    bool found = false;
    double closestDistance = static_cast<double>(maximumDistance);
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
        double nearDistance = 0.0;
        double farDistance = closestDistance;
        math::Vec3 nearNormal{};

        const double origins[] = {origin.x, origin.y, origin.z};
        const double directions[] = {
            normalizedDirection.x, normalizedDirection.y, normalizedDirection.z
        };
        const double minimums[] = {
            bounds.minimum.x, bounds.minimum.y, bounds.minimum.z
        };
        const double maximums[] = {
            bounds.maximum.x, bounds.maximum.y, bounds.maximum.z
        };

        bool intersects = true;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double component = directions[axis];
            if (component == 0.0)
            {
                if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis])
                {
                    intersects = false;
                    break;
                }
                continue;
            }

            const double inverse = 1.0 / component;
            double first = (minimums[axis] - origins[axis]) * inverse;
            double second = (maximums[axis] - origins[axis]) * inverse;
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

        if (!intersects || farDistance < 0.0 || nearDistance > closestDistance)
            continue;
        const double distance = inside ? 0.0 : nearDistance;
        if (distance > closestDistance)
            continue;
        closestDistance = distance;
        closestHit.collider = {static_cast<std::uint32_t>(index), slot.generation};
        closestHit.distance = static_cast<float>(distance);
        closestHit.position = origin + normalizedDirection * closestHit.distance;
        closestHit.startedInside = inside;
        closestHit.normal = inside ? -normalizedDirection : nearNormal;
        found = true;
    }

    if (found)
        hit = closestHit;
    return found;
}

bool PhysicsWorld::ComputeOverlaps(
    std::vector<ColliderPair>& results,
    const std::uint32_t layerMask) const
{
    results.clear();
    if (layerMask == 0U)
        return false;

    std::vector<std::size_t> sortedIndices;
    sortedIndices.reserve(m_colliderCount);
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (slot.occupied && (slot.description.layer & layerMask) != 0U)
            sortedIndices.push_back(index);
    }

    std::sort(sortedIndices.begin(), sortedIndices.end(),
        [this](const std::size_t left, const std::size_t right) {
            const float leftMinimum = m_slots[left].description.bounds.minimum.x;
            const float rightMinimum = m_slots[right].description.bounds.minimum.x;
            return leftMinimum < rightMinimum ||
                   (leftMinimum == rightMinimum && left < right);
        });

    std::vector<ColliderPair> pairs;
    for (std::size_t firstPosition = 0; firstPosition < sortedIndices.size(); ++firstPosition)
    {
        const std::size_t firstIndex = sortedIndices[firstPosition];
        const Slot& first = m_slots[firstIndex];
        const Aabb& firstBounds = first.description.bounds;
        for (std::size_t secondPosition = firstPosition + 1U;
             secondPosition < sortedIndices.size(); ++secondPosition)
        {
            const std::size_t secondIndex = sortedIndices[secondPosition];
            const Slot& second = m_slots[secondIndex];
            const Aabb& secondBounds = second.description.bounds;
            if (secondBounds.minimum.x > firstBounds.maximum.x)
                break;
            if (firstBounds.minimum.y > secondBounds.maximum.y ||
                firstBounds.maximum.y < secondBounds.minimum.y ||
                firstBounds.minimum.z > secondBounds.maximum.z ||
                firstBounds.maximum.z < secondBounds.minimum.z)
                continue;

            ColliderHandle firstHandle{
                static_cast<std::uint32_t>(firstIndex), first.generation};
            ColliderHandle secondHandle{
                static_cast<std::uint32_t>(secondIndex), second.generation};
            if (secondHandle.index < firstHandle.index)
                std::swap(firstHandle, secondHandle);
            pairs.push_back({
                firstHandle,
                secondHandle,
                first.description.isTrigger || second.description.isTrigger
            });
        }
    }

    std::sort(pairs.begin(), pairs.end(),
        [](const ColliderPair& left, const ColliderPair& right) {
            if (left.first.index != right.first.index)
                return left.first.index < right.first.index;
            return left.second.index < right.second.index;
        });
    results.swap(pairs);
    return true;
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
    const double radiusSquared = static_cast<double>(radius) * radius;
    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (!Matches(slot, layerMask, includeTriggers))
            continue;
        const math::Vec3 closest = ClosestPoint(slot.description.bounds, center);
        const double dx = static_cast<double>(center.x) - closest.x;
        const double dy = static_cast<double>(center.y) - closest.y;
        const double dz = static_cast<double>(center.z) - closest.z;
        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared <= radiusSquared)
            results.push_back({static_cast<std::uint32_t>(index), slot.generation});
    }
    return true;
}

bool PhysicsWorld::OverlapVerticalCapsule(
    const math::Vec3& center,
    const float radius,
    const float halfHeight,
    std::vector<ColliderHandle>& results,
    const std::uint32_t layerMask,
    const bool includeTriggers) const
{
    results.clear();
    if (!IsFinite(center) || !std::isfinite(radius) || !std::isfinite(halfHeight) ||
        radius < 0.0F || halfHeight < radius || layerMask == 0U)
        return false;

    const double segmentHalfLength = static_cast<double>(halfHeight) - radius;
    const double segmentMinimumY = static_cast<double>(center.y) - segmentHalfLength;
    const double segmentMaximumY = static_cast<double>(center.y) + segmentHalfLength;
    const double radiusSquared = static_cast<double>(radius) * radius;

    for (std::size_t index = 0; index < m_slots.size(); ++index)
    {
        const Slot& slot = m_slots[index];
        if (!Matches(slot, layerMask, includeTriggers))
            continue;

        const Aabb& bounds = slot.description.bounds;
        const double closestX = std::clamp(
            static_cast<double>(center.x), static_cast<double>(bounds.minimum.x),
            static_cast<double>(bounds.maximum.x));
        const double closestZ = std::clamp(
            static_cast<double>(center.z), static_cast<double>(bounds.minimum.z),
            static_cast<double>(bounds.maximum.z));
        double verticalDistance = 0.0;
        if (segmentMaximumY < bounds.minimum.y)
            verticalDistance = static_cast<double>(bounds.minimum.y) - segmentMaximumY;
        else if (segmentMinimumY > bounds.maximum.y)
            verticalDistance = segmentMinimumY - static_cast<double>(bounds.maximum.y);

        const double dx = static_cast<double>(center.x) - closestX;
        const double dz = static_cast<double>(center.z) - closestZ;
        if (dx * dx + verticalDistance * verticalDistance + dz * dz <= radiusSquared)
            results.push_back({static_cast<std::uint32_t>(index), slot.generation});
    }
    return true;
}
} // namespace vanta
