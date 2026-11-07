#pragma once

#include "vanta/math/Math.hpp"

#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace vanta
{
class Transform final
{
public:
    [[nodiscard]] const math::Vec3& Position() const noexcept { return m_position; }
    [[nodiscard]] const math::Quaternion& Rotation() const noexcept { return m_rotation; }
    [[nodiscard]] const math::Vec3& Scale() const noexcept { return m_scale; }

    [[nodiscard]] bool SetPosition(const math::Vec3& position) noexcept;
    [[nodiscard]] bool SetRotation(const math::Quaternion& rotation) noexcept;
    [[nodiscard]] bool SetScale(const math::Vec3& scale) noexcept;
    [[nodiscard]] math::Mat4 Matrix() const noexcept;

private:
    math::Vec3 m_position{};
    math::Quaternion m_rotation{};
    math::Vec3 m_scale{1.0F, 1.0F, 1.0F};
};

struct EntityHandle
{
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index != kInvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr bool operator==(const EntityHandle&) const noexcept = default;
};

struct SceneEntitySnapshot
{
    EntityHandle entity{};
    std::string name;
    math::Mat4 worldMatrix = math::Mat4::Identity();
    bool active = false;
};

// Scene mutation and snapshot generation are single-threaded. Transform/name pointers remain
// stable across entity creation, and are invalidated only when their entity is destroyed or the
// scene itself is destroyed. A snapshot owns its names
// and matrices, so callers may retain it while the scene is subsequently modified.
class Scene final
{
public:
    [[nodiscard]] EntityHandle CreateEntity(std::string name = {});
    [[nodiscard]] bool DestroyEntity(EntityHandle entity) noexcept;
    void Clear() noexcept;
    [[nodiscard]] bool IsAlive(EntityHandle entity) const noexcept;
    [[nodiscard]] std::size_t EntityCount() const noexcept { return m_entityCount; }

    [[nodiscard]] Transform* GetTransform(EntityHandle entity) noexcept;
    [[nodiscard]] const Transform* GetTransform(EntityHandle entity) const noexcept;
    [[nodiscard]] const std::string* GetName(EntityHandle entity) const noexcept;
    [[nodiscard]] bool SetName(EntityHandle entity, std::string name);
    [[nodiscard]] bool SetActive(EntityHandle entity, bool active) noexcept;
    [[nodiscard]] bool IsActive(EntityHandle entity) const noexcept;
    [[nodiscard]] bool IsActiveInHierarchy(EntityHandle entity) const noexcept;

    // A default/invalid parent handle makes the entity a root. When requested, world-space
    // transform is preserved if the resulting local matrix can be represented as TRS.
    [[nodiscard]] bool SetParent(EntityHandle entity, EntityHandle parent = {},
                                 bool preserveWorldTransform = false);
    [[nodiscard]] EntityHandle GetParent(EntityHandle entity) const noexcept;
    [[nodiscard]] std::optional<math::Mat4> GetWorldMatrix(EntityHandle entity) const;
    [[nodiscard]] std::vector<SceneEntitySnapshot> CreateSnapshot() const;

private:
    struct Slot
    {
        Transform transform{};
        std::string name;
        EntityHandle parent{};
        std::uint32_t generation = 1;
        bool occupied = false;
        bool active = true;
    };

    [[nodiscard]] const Slot* Find(EntityHandle entity) const noexcept;
    [[nodiscard]] Slot* Find(EntityHandle entity) noexcept;
    [[nodiscard]] std::optional<math::Mat4> BuildWorldMatrix(EntityHandle entity) const;

    std::deque<Slot> m_slots;
    std::size_t m_entityCount = 0;
};
} // namespace vanta
