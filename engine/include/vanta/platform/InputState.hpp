#pragma once

#include <array>
#include <cstdint>

namespace vanta
{
class InputState final
{
public:
    static constexpr std::size_t kKeyCount = 256;
    static constexpr std::size_t kMouseButtonCount = 5;

    void BeginFrame() noexcept
    {
        m_pressed.fill(false);
        m_released.fill(false);
        m_mousePressed.fill(false);
        m_mouseReleased.fill(false);
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
    }

    void SetKey(int virtualKey, bool down) noexcept
    {
        if (virtualKey < 0 || virtualKey >= static_cast<int>(kKeyCount))
            return;
        const auto index = static_cast<std::size_t>(virtualKey);
        if (m_down[index] == down)
            return;
        m_down[index] = down;
        (down ? m_pressed : m_released)[index] = true;
    }

    void SetMouseButton(int button, bool down) noexcept
    {
        if (button < 0 || button >= static_cast<int>(kMouseButtonCount))
            return;
        const auto index = static_cast<std::size_t>(button);
        if (m_mouseDown[index] == down)
            return;
        m_mouseDown[index] = down;
        (down ? m_mousePressed : m_mouseReleased)[index] = true;
    }

    void AddMouseDelta(int x, int y) noexcept
    {
        m_mouseDeltaX = SaturatingAdd(m_mouseDeltaX, x);
        m_mouseDeltaY = SaturatingAdd(m_mouseDeltaY, y);
    }

    void ReleaseAll() noexcept
    {
        for (std::size_t index = 0; index < kKeyCount; ++index)
        {
            if (m_down[index])
            {
                m_down[index] = false;
                m_released[index] = true;
            }
        }
        for (std::size_t index = 0; index < kMouseButtonCount; ++index)
        {
            if (m_mouseDown[index])
            {
                m_mouseDown[index] = false;
                m_mouseReleased[index] = true;
            }
        }
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
    }

    [[nodiscard]] bool IsKeyDown(int key) const noexcept { return Read(m_down, key); }
    [[nodiscard]] bool WasKeyPressed(int key) const noexcept { return Read(m_pressed, key); }
    [[nodiscard]] bool WasKeyReleased(int key) const noexcept { return Read(m_released, key); }
    [[nodiscard]] bool IsMouseButtonDown(int button) const noexcept { return Read(m_mouseDown, button); }
    [[nodiscard]] bool WasMouseButtonPressed(int button) const noexcept { return Read(m_mousePressed, button); }
    [[nodiscard]] bool WasMouseButtonReleased(int button) const noexcept { return Read(m_mouseReleased, button); }
    [[nodiscard]] int MouseDeltaX() const noexcept { return m_mouseDeltaX; }
    [[nodiscard]] int MouseDeltaY() const noexcept { return m_mouseDeltaY; }

private:
    template <std::size_t Size>
    [[nodiscard]] static bool Read(const std::array<bool, Size>& values, int index) noexcept
    {
        return index >= 0 && index < static_cast<int>(Size) &&
               values[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] static int SaturatingAdd(int left, int right) noexcept
    {
        constexpr int maximum = 1'000'000;
        constexpr int minimum = -maximum;
        const auto sum = static_cast<long long>(left) + static_cast<long long>(right);
        return static_cast<int>(sum > maximum ? maximum : (sum < minimum ? minimum : sum));
    }

    std::array<bool, kKeyCount> m_down{};
    std::array<bool, kKeyCount> m_pressed{};
    std::array<bool, kKeyCount> m_released{};
    std::array<bool, kMouseButtonCount> m_mouseDown{};
    std::array<bool, kMouseButtonCount> m_mousePressed{};
    std::array<bool, kMouseButtonCount> m_mouseReleased{};
    int m_mouseDeltaX = 0;
    int m_mouseDeltaY = 0;
};
} // namespace vanta
