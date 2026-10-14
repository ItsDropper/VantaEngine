#include "vanta/platform/Window.hpp"

#include "vanta/core/Log.hpp"

#include <string>

namespace vanta
{
namespace
{
constexpr wchar_t kWindowClassName[] = L"VantaEngineNativeWindow";
}

Window::~Window()
{
    if (m_handle != nullptr && IsWindow(m_handle))
    {
        DestroyWindow(m_handle);
    }
}

bool Window::Create(const std::wstring_view title, const int width, const int height)
{
    m_width = width;
    m_height = height;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Window::WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        LogError("RegisterClassExW failed.");
        return false;
    }

    RECT bounds{0, 0, width, height};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    if (AdjustWindowRect(&bounds, style, FALSE) == FALSE)
    {
        LogError("AdjustWindowRect failed.");
        return false;
    }

    const int outerWidth = bounds.right - bounds.left;
    const int outerHeight = bounds.bottom - bounds.top;
    const std::wstring ownedTitle{title};

    m_handle = CreateWindowExW(
        0,
        kWindowClassName,
        ownedTitle.c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        outerWidth,
        outerHeight,
        nullptr,
        nullptr,
        instance,
        this);

    if (m_handle == nullptr)
    {
        LogError("CreateWindowExW failed.");
        return false;
    }

    m_isOpen = true;
    ShowWindow(m_handle, SW_SHOW);
    UpdateWindow(m_handle);
    LogInfo("Native window created.");
    return true;
}

void Window::PollEvents()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
    {
        if (message.message == WM_QUIT)
        {
            m_isOpen = false;
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK Window::WindowProcedure(
    const HWND handle,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<Window*>(creation->lpCreateParams);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        if (window != nullptr)
        {
            window->m_handle = handle;
        }
    }

    if (window != nullptr)
    {
        return window->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(handle, message, wParam, lParam);
}

LRESULT Window::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        m_width = static_cast<int>(LOWORD(lParam));
        m_height = static_cast<int>(HIWORD(lParam));
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(m_handle);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(m_handle);
        return 0;

    case WM_DESTROY:
        m_isOpen = false;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_handle, message, wParam, lParam);
}
} // namespace vanta
