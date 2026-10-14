#pragma once

#include <Windows.h>

#include <string_view>

namespace vanta
{
class Window final
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool Create(std::wstring_view title, int width, int height);
    void PollEvents();

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] HWND NativeHandle() const noexcept { return m_handle; }
    [[nodiscard]] int Width() const noexcept { return m_width; }
    [[nodiscard]] int Height() const noexcept { return m_height; }

private:
    static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND m_handle = nullptr;
    int m_width = 1280;
    int m_height = 720;
    bool m_isOpen = false;
};
} // namespace vanta
