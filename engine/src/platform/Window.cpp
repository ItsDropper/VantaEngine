#include "vanta/platform/Window.hpp"

#include "vanta/core/Log.hpp"

#include <string>

namespace vanta
{
namespace
{
constexpr wchar_t kWindowClassName[] = L"VantaEngineNativeWindow";
constexpr int kLeftMouseButton = 0;
constexpr int kRightMouseButton = 1;
constexpr int kMiddleMouseButton = 2;
}

Window::~Window()
{
    if (m_handle != nullptr && IsWindow(m_handle))
    {
        if (GetCapture() == m_handle)
            ReleaseCapture();
        DestroyWindow(m_handle);
    }
}

bool Window::Create(const std::wstring_view title, const int width, const int height)
{
    if (width <= 0 || height <= 0)
    {
        LogError("Window dimensions must be positive.");
        return false;
    }
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
        0, kWindowClassName, ownedTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, outerWidth, outerHeight,
        nullptr, nullptr, instance, this);

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
    m_input.BeginFrame();
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
    const HWND handle, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        window = static_cast<Window*>(creation->lpCreateParams);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        if (window != nullptr)
            window->m_handle = handle;
    }
    return window != nullptr ? window->HandleMessage(message, wParam, lParam)
                             : DefWindowProcW(handle, message, wParam, lParam);
}

LRESULT Window::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        m_width = static_cast<int>(LOWORD(lParam));
        m_height = static_cast<int>(HIWORD(lParam));
        m_hasMousePosition = false;
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(m_handle);
            return 0;
        }
        if (wParam < InputState::kKeyCount)
            m_input.SetKey(static_cast<int>(wParam), true);
        return DefWindowProcW(m_handle, message, wParam, lParam);

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < InputState::kKeyCount)
            m_input.SetKey(static_cast<int>(wParam), false);
        return DefWindowProcW(m_handle, message, wParam, lParam);

    case WM_MOUSEMOVE:
    {
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        if (m_hasMousePosition)
            m_input.AddMouseDelta(x - m_lastMouseX, y - m_lastMouseY);
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_hasMousePosition = true;
        return 0;
    }

    case WM_LBUTTONDOWN:
        m_input.SetMouseButton(kLeftMouseButton, true);
        SetCapture(m_handle);
        return 0;
    case WM_LBUTTONUP:
        m_input.SetMouseButton(kLeftMouseButton, false);
        if (!m_input.IsMouseButtonDown(kLeftMouseButton) &&
            !m_input.IsMouseButtonDown(kRightMouseButton) &&
            !m_input.IsMouseButtonDown(kMiddleMouseButton) && GetCapture() == m_handle)
            ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        m_input.SetMouseButton(kRightMouseButton, true);
        SetCapture(m_handle);
        return 0;
    case WM_RBUTTONUP:
        m_input.SetMouseButton(kRightMouseButton, false);
        if (!m_input.IsMouseButtonDown(kLeftMouseButton) &&
            !m_input.IsMouseButtonDown(kRightMouseButton) &&
            !m_input.IsMouseButtonDown(kMiddleMouseButton) && GetCapture() == m_handle)
            ReleaseCapture();
        return 0;
    case WM_MBUTTONDOWN:
        m_input.SetMouseButton(kMiddleMouseButton, true);
        SetCapture(m_handle);
        return 0;
    case WM_MBUTTONUP:
        m_input.SetMouseButton(kMiddleMouseButton, false);
        if (!m_input.IsMouseButtonDown(kLeftMouseButton) &&
            !m_input.IsMouseButtonDown(kRightMouseButton) &&
            !m_input.IsMouseButtonDown(kMiddleMouseButton) && GetCapture() == m_handle)
            ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        m_input.SetMouseButton(kLeftMouseButton, false);
        m_input.SetMouseButton(kRightMouseButton, false);
        m_input.SetMouseButton(kMiddleMouseButton, false);
        return 0;

    case WM_KILLFOCUS:
        m_input.ReleaseAll();
        m_hasMousePosition = false;
        if (GetCapture() == m_handle)
            ReleaseCapture();
        return 0;

    case WM_CLOSE:
        DestroyWindow(m_handle);
        return 0;

    case WM_DESTROY:
        m_input.ReleaseAll();
        m_isOpen = false;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_handle, message, wParam, lParam);
}
} // namespace vanta
