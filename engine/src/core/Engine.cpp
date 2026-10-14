#include "vanta/core/Engine.hpp"

#include "vanta/core/FrameTime.hpp"
#include "vanta/core/Log.hpp"
#include "vanta/platform/Window.hpp"
#include "vanta/renderer/D3D11Renderer.hpp"

#include <chrono>

namespace vanta
{
int Engine::Run()
{
    Window window;
    if (!window.Create(L"VantaEngine | 3D Preview", 1280, 720))
    {
        LogError("Engine startup failed while creating the window.");
        return 1;
    }

    D3D11Renderer renderer;
    if (!renderer.Initialize(window.NativeHandle(), window.Width(), window.Height()))
    {
        LogError("Engine startup failed while initializing the renderer.");
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    float elapsedSeconds = 0.0F;

    LogInfo("3D scene running. Close the window or press Escape to exit.");

    while (window.IsOpen())
    {
        window.PollEvents();
        if (!window.IsOpen())
        {
            break;
        }

        const int width = window.Width();
        const int height = window.Height();
        if (width > 0 && height > 0 && !renderer.Resize(width, height))
        {
            return 1;
        }

        const auto now = Clock::now();
        const std::chrono::duration<double> elapsed = now - previousFrame;
        const double deltaSeconds = ClampDeltaSeconds(elapsed.count());
        previousFrame = now;
        elapsedSeconds += static_cast<float>(deltaSeconds);

        if (width > 0 && height > 0)
        {
            renderer.Render(elapsedSeconds);
        }
    }

    LogInfo("Engine shut down cleanly.");
    return 0;
}
} // namespace vanta
