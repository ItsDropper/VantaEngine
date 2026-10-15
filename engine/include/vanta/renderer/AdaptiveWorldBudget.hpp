#pragma once

#include <algorithm>
#include <chrono>

namespace vanta::renderer
{
// CPU-side frame-pacing feedback for world-detail decisions. This deliberately changes
// LOD thresholds, not the world's maximum draw distance, so overload does not make the
// environment abruptly disappear. A dead band and asymmetric response prevent flicker.
class AdaptiveWorldBudget final
{
public:
    [[nodiscard]] float Update()
    {
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        if (!m_initialized)
        {
            m_lastFrame = now;
            m_initialized = true;
            return m_detailScale;
        }

        const float seconds = std::chrono::duration<float>(now - m_lastFrame).count();
        m_lastFrame = now;
        return UpdateForFrameSeconds(seconds);
    }

    // Deterministic entry point for tests and offline replay of captured frame timings.
    // Invalid samples and long stalls are ignored so pauses/debugger breaks do not alter LOD.
    [[nodiscard]] float UpdateForFrameSeconds(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds <= 0.0F || seconds > 0.25F)
            return m_detailScale;

        const float sample = std::clamp(seconds, 0.001F, 0.1F);
        m_smoothedFrameSeconds += (sample - m_smoothedFrameSeconds) * 0.08F;
        constexpr float targetFrameSeconds = 1.0F / 60.0F;
        if (m_smoothedFrameSeconds > targetFrameSeconds * 1.08F)
            m_detailScale = std::max(0.68F, m_detailScale - 0.025F);
        else if (m_smoothedFrameSeconds < targetFrameSeconds * 0.82F)
            m_detailScale = std::min(1.0F, m_detailScale + 0.008F);

        return m_detailScale;
    }

    [[nodiscard]] float DetailScale() const noexcept { return m_detailScale; }

private:
    std::chrono::steady_clock::time_point m_lastFrame{};
    float m_smoothedFrameSeconds = 1.0F / 60.0F;
    float m_detailScale = 1.0F;
    bool m_initialized = false;
};
} // namespace vanta::renderer
