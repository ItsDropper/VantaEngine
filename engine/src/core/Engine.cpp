#include "vanta/core/Engine.hpp"

#include "vanta/core/FrameTime.hpp"
#include "vanta/core/Log.hpp"
#include "vanta/platform/Window.hpp"
#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/NvidiaRendering.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vanta
{
int Engine::Run()
{
    Window window;
    if (!window.Create(L"VantaEngine | Camera Playground", 1280, 720))
    {
        LogError("Engine startup failed while creating the window.");
        return 1;
    }

    const auto graphicsCapabilities = renderer::QueryNvidiaRenderingCapabilities();
    LogInfo(graphicsCapabilities.diagnostic.c_str());
    LogInfo("Rendering backend: Direct3D 11 (active).");
    LogInfo("NVIDIA DLSS, AMD FSR, Intel XeSS, and DXR rendering are not integrated yet.");

    D3D11Renderer renderer;
    if (!renderer.Initialize(window.NativeHandle(), window.Width(), window.Height()))
    {
        LogError("Engine startup failed while initializing the renderer.");
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    float elapsedSeconds = 0.0F;
    float cameraX = 3.4F;
    float cameraY = 2.6F;
    float cameraZ = -5.5F;
    float cameraYaw = -0.554F;
    float cameraPitch = -0.360F;

    LogInfo("Camera controls: W/A/S/D move, Q/E descend/ascend, arrow keys look, Escape exits.");

    while (window.IsOpen())
    {
        window.PollEvents();
        if (!window.IsOpen())
        {
            break;
        }

        const auto now = Clock::now();
        const std::chrono::duration<double> elapsed = now - previousFrame;
        const float deltaSeconds = static_cast<float>(ClampDeltaSeconds(elapsed.count()));
        previousFrame = now;
        elapsedSeconds += deltaSeconds;

        constexpr float moveSpeed = 4.5F;
        constexpr float lookSpeed = 1.65F;
        if (window.IsKeyDown(VK_LEFT)) cameraYaw -= lookSpeed * deltaSeconds;
        if (window.IsKeyDown(VK_RIGHT)) cameraYaw += lookSpeed * deltaSeconds;
        if (window.IsKeyDown(VK_UP)) cameraPitch += lookSpeed * deltaSeconds;
        if (window.IsKeyDown(VK_DOWN)) cameraPitch -= lookSpeed * deltaSeconds;
        cameraPitch = std::clamp(cameraPitch, -1.50F, 1.50F);

        const float forwardX = std::sin(cameraYaw);
        const float forwardZ = std::cos(cameraYaw);
        const float rightX = std::cos(cameraYaw);
        const float rightZ = -std::sin(cameraYaw);
        if (window.IsKeyDown('W')) { cameraX += forwardX * moveSpeed * deltaSeconds; cameraZ += forwardZ * moveSpeed * deltaSeconds; }
        if (window.IsKeyDown('S')) { cameraX -= forwardX * moveSpeed * deltaSeconds; cameraZ -= forwardZ * moveSpeed * deltaSeconds; }
        if (window.IsKeyDown('D')) { cameraX += rightX * moveSpeed * deltaSeconds; cameraZ += rightZ * moveSpeed * deltaSeconds; }
        if (window.IsKeyDown('A')) { cameraX -= rightX * moveSpeed * deltaSeconds; cameraZ -= rightZ * moveSpeed * deltaSeconds; }
        if (window.IsKeyDown('E')) cameraY += moveSpeed * deltaSeconds;
        if (window.IsKeyDown('Q')) cameraY -= moveSpeed * deltaSeconds;

        const int width = window.Width();
        const int height = window.Height();
        if (width > 0 && height > 0 && !renderer.Resize(width, height))
        {
            return 1;
        }
        if (width > 0 && height > 0)
        {
            renderer.Render(elapsedSeconds, cameraX, cameraY, cameraZ, cameraYaw, cameraPitch);
        }
    }

    LogInfo("Engine shut down cleanly.");
    return 0;
}
} // namespace vanta
