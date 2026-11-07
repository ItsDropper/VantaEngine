#pragma once

#include "vanta/math/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vanta
{
struct Aabb
{
    math::Vec3 minimum{};
    math::Vec3 maximum{};
};

struct ColliderHandle
{
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr bool operator==(const ColliderHandle&) const noexcept = default;
};

struct ColliderDescription
{
    Aabb bounds{};
    std::uint32_t layer = 1;
    bool isTrigger = false;
};

struct RaycastHit
{
    ColliderHandle collider{};
    float distance = 0.0F;
    math::Vec3 position{};
    math::Vec3 normal{};
    bool startedInside = false;
};

class PhysicsWorld final
{
public:
    [[nodiscard]] ColliderHandle AddCollider(const ColliderDescription& description);
    [[nodiscard]] bool UpdateCollider(ColliderHandle handle, const ColliderDescription& description) noexcept;
    [[nodiscard]] bool RemoveCollider(ColliderHandle handle) noexcept;
    [[nodiscard]] bool Contains(ColliderHandle handle) const noexcept;
    [[nodiscard]] std::size_t ColliderCount() const noexcept { return m_colliderCount; }

    [[nodiscard]] bool Raycast(
        const math::Vec3& origin,
        const math::Vec3& direction,
        float maximumDistance,
        RaycastHit& hit,
        std::uint32_t layerMask = 0xFFFFFFFFU,
        bool includeTriggers = true) const noexcept;

    [[nodiscard]] bool OverlapAabb(
        const Aabb& bounds,
        std::vector<ColliderHandle>& results,
        std::uint32_t layerMask = 0xFFFFFFFFU,
        bool includeTriggers = true) const;

    [[nodiscard]] bool OverlapSphere(
        const math::Vec3& center,
        float radius,
        std::vector<ColliderHandle>& results,
        std::uint32_t layerMask = 0xFFFFFFFFU,
        bool includeTriggers = true) const;

    [[nodiscard]] static bool IsValid(const Aabb& bounds) noexcept;

private:
    struct Slot
    {
        ColliderDescription description{};
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    [[nodiscard]] const Slot* Find(ColliderHandle handle) const noexcept;
    [[nodiscard]] Slot* Find(ColliderHandle handle) noexcept;
    [[nodiscard]] static bool IsFinite(const math::Vec3& value) noexcept;
    [[nodiscard]] static bool Matches(const Slot& slot, std::uint32_t layerMask,
                                      bool includeTriggers) noexcept;

    std::vector<Slot> m_slots;
    std::size_t m_colliderCount = 0;
};
} // namespace vanta
