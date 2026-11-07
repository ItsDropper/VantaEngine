#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vanta
{
struct FixedStepResult
{
    std::uint32_t steps = 0;
    double interpolationAlpha = 0.0;
    double droppedSeconds = 0.0;
};

class FixedStepClock final
{
public:
    static constexpr double kDefaultStepSeconds = 1.0 / 60.0;
    static constexpr double kDefaultMaximumFrameSeconds = 0.25;
    static constexpr std::uint32_t kDefaultMaximumStepsPerFrame = 8;

    explicit FixedStepClock(
        double stepSeconds = kDefaultStepSeconds,
        std::uint32_t maximumStepsPerFrame = kDefaultMaximumStepsPerFrame,
        double maximumFrameSeconds = kDefaultMaximumFrameSeconds) noexcept
        : m_stepSeconds(std::isfinite(stepSeconds) && stepSeconds > 0.0
              ? stepSeconds : kDefaultStepSeconds),
          m_maximumStepsPerFrame(maximumStepsPerFrame > 0
              ? maximumStepsPerFrame : kDefaultMaximumStepsPerFrame),
          m_maximumFrameSeconds(std::isfinite(maximumFrameSeconds) && maximumFrameSeconds > 0.0
              ? maximumFrameSeconds : kDefaultMaximumFrameSeconds)
    {
    }

    template <typename Update>
    [[nodiscard]] FixedStepResult Advance(double frameSeconds, Update&& update)
    {
        if (!std::isfinite(frameSeconds) || frameSeconds <= 0.0)
            return {0, InterpolationAlpha(), 0.0};

        m_accumulator += std::min(frameSeconds, m_maximumFrameSeconds);
        const double epsilon = m_stepSeconds * 1.0e-9;
        const auto availableSteps = static_cast<std::uint64_t>(
            std::floor((m_accumulator + epsilon) / m_stepSeconds));
        const auto stepsToRun = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(availableSteps, m_maximumStepsPerFrame));

        for (std::uint32_t step = 0; step < stepsToRun; ++step)
        {
            update(m_stepSeconds);
            m_accumulator -= m_stepSeconds;
            if (m_accumulator < 0.0 && m_accumulator > -epsilon)
                m_accumulator = 0.0;
        }

        double droppedSeconds = 0.0;
        if (availableSteps > stepsToRun)
        {
            const auto droppedSteps = availableSteps - stepsToRun;
            droppedSeconds = static_cast<double>(droppedSteps) * m_stepSeconds;
            m_accumulator -= droppedSeconds;
            m_totalDroppedSeconds += droppedSeconds;
        }

        return {stepsToRun, InterpolationAlpha(), droppedSeconds};
    }

    void Reset() noexcept
    {
        m_accumulator = 0.0;
        m_totalDroppedSeconds = 0.0;
    }

    [[nodiscard]] double StepSeconds() const noexcept { return m_stepSeconds; }
    [[nodiscard]] double InterpolationAlpha() const noexcept
    {
        return std::clamp(m_accumulator / m_stepSeconds, 0.0, 1.0);
    }
    [[nodiscard]] double TotalDroppedSeconds() const noexcept { return m_totalDroppedSeconds; }

private:
    double m_stepSeconds;
    std::uint32_t m_maximumStepsPerFrame;
    double m_maximumFrameSeconds;
    double m_accumulator = 0.0;
    double m_totalDroppedSeconds = 0.0;
};
} // namespace vanta
