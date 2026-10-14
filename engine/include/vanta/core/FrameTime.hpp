#pragma once

namespace vanta
{
// Prevent a long debugger pause or window drag from destabilizing future simulation steps.
[[nodiscard]] constexpr double ClampDeltaSeconds(const double seconds) noexcept
{
    if (seconds < 0.0)
    {
        return 0.0;
    }
    if (seconds > 0.25)
    {
        return 0.25;
    }
    return seconds;
}
} // namespace vanta
