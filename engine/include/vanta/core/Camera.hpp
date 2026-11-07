#pragma once

#include "vanta/math/Math.hpp"

#include <algorithm>
#include <cmath>

namespace vanta
{
class Camera final
{
public:
    explicit Camera(math::Vec3 position = {}, float yawRadians = 0.0F,
                    float pitchRadians = 0.0F) noexcept
        : m_position(position)
    {
        SetOrientation(yawRadians, pitchRadians);
    }

    [[nodiscard]] const math::Vec3& Position() const noexcept { return m_position; }
    [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
    [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

    void SetPosition(const math::Vec3& position) noexcept
    {
        if (IsFinite(position))
            m_position = position;
    }

    void SetOrientation(float yawRadians, float pitchRadians) noexcept
    {
        if (std::isfinite(yawRadians))
            m_yaw = std::remainder(yawRadians, 2.0F * math::kPi);
        if (std::isfinite(pitchRadians))
            m_pitch = std::clamp(pitchRadians, -kMaximumPitch, kMaximumPitch);
    }

    void Rotate(float yawDeltaRadians, float pitchDeltaRadians) noexcept
    {
        if (!std::isfinite(yawDeltaRadians) || !std::isfinite(pitchDeltaRadians))
            return;
        SetOrientation(m_yaw + yawDeltaRadians, m_pitch + pitchDeltaRadians);
    }

    [[nodiscard]] math::Vec3 Forward() const noexcept
    {
        const float cosPitch = std::cos(m_pitch);
        return {std::sin(m_yaw) * cosPitch, std::sin(m_pitch),
                std::cos(m_yaw) * cosPitch};
    }

    [[nodiscard]] math::Vec3 Right() const noexcept
    {
        return {std::cos(m_yaw), 0.0F, -std::sin(m_yaw)};
    }

    [[nodiscard]] math::Vec3 Up() const noexcept
    {
        return Forward().Cross(Right()).Normalized().value_or(math::Vec3{0.0F, 1.0F, 0.0F});
    }

    [[nodiscard]] math::Mat4 ViewMatrix() const noexcept
    {
        return math::Mat4::LookAtLH(m_position, m_position + Forward(),
                                    {0.0F, 1.0F, 0.0F})
            .value_or(math::Mat4::Identity());
    }

    // Local movement is normalized before applying speed and delta time, so diagonal
    // input cannot move faster than axial input. Vertical movement follows world up.
    void MoveLocal(float forward, float right, float vertical,
                   float speed, float deltaSeconds) noexcept
    {
        if (!std::isfinite(forward) || !std::isfinite(right) ||
            !std::isfinite(vertical) || !std::isfinite(speed) ||
            !std::isfinite(deltaSeconds) || speed < 0.0F || deltaSeconds <= 0.0F)
            return;

        math::Vec3 direction = Forward() * forward + Right() * right +
                               math::Vec3{0.0F, vertical, 0.0F};
        const auto normalized = direction.Normalized();
        if (!normalized)
            return;
        m_position += *normalized * (speed * deltaSeconds);
    }

private:
    static constexpr float kMaximumPitch = math::kPi * 0.5F - 0.01F;

    [[nodiscard]] static bool IsFinite(const math::Vec3& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    }

    math::Vec3 m_position{};
    float m_yaw = 0.0F;
    float m_pitch = 0.0F;
};
} // namespace vanta
