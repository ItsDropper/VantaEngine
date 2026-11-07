#include "vanta/core/Engine.hpp"

#include "vanta/core/FixedStepClock.hpp"
#include "vanta/core/Log.hpp"
#include "vanta/core/Camera.hpp"
#include "vanta/platform/Window.hpp"
#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/D3D12Renderer.hpp"
#include "vanta/renderer/NvidiaRendering.hpp"

#include <chrono>

namespace vanta
{
namespace
{
template <typename Renderer>
int RunCameraLoop(Window& window, Renderer& renderer)
{
    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    float elapsedSeconds = 0.0F;
    Camera camera({3.4F, 2.6F, -5.5F}, -0.554F, -0.360F);
    FixedStepClock simulationClock;
    double pendingMouseYaw = 0.0;
    double pendingMousePitch = 0.0;

    LogInfo("Camera controls: W/A/S/D move, Q/E descend/ascend, arrow keys look, hold right mouse to look, Escape exits.");

    while (window.IsOpen())
    {
        window.PollEvents();
        if (!window.IsOpen())
            break;

        const auto now = Clock::now();
        const double frameSeconds = std::chrono::duration<double>(now - previousFrame).count();
        previousFrame = now;

        const InputState& input = window.Input();
        constexpr float moveSpeed = 4.5F;
        constexpr float lookSpeed = 1.65F;
        constexpr double mouseSensitivity = 0.0025;
        float forward = 0.0F;
        float right = 0.0F;
        float vertical = 0.0F;
        if (input.IsKeyDown('W')) forward += 1.0F;
        if (input.IsKeyDown('S')) forward -= 1.0F;
        if (input.IsKeyDown('D')) right += 1.0F;
        if (input.IsKeyDown('A')) right -= 1.0F;
        if (input.IsKeyDown('E')) vertical += 1.0F;
        if (input.IsKeyDown('Q')) vertical -= 1.0F;

        float yawInput = 0.0F;
        float pitchInput = 0.0F;
        if (input.IsKeyDown(VK_LEFT)) yawInput -= lookSpeed;
        if (input.IsKeyDown(VK_RIGHT)) yawInput += lookSpeed;
        if (input.IsKeyDown(VK_UP)) pitchInput += lookSpeed;
        if (input.IsKeyDown(VK_DOWN)) pitchInput -= lookSpeed;

        if (input.IsMouseButtonDown(1))
        {
            pendingMouseYaw += static_cast<double>(input.MouseDeltaX()) * mouseSensitivity;
            pendingMousePitch -= static_cast<double>(input.MouseDeltaY()) * mouseSensitivity;
        }
        else
        {
            pendingMouseYaw = 0.0;
            pendingMousePitch = 0.0;
        }

        bool pendingMouseApplied = false;
        const FixedStepResult simulation = simulationClock.Advance(
            frameSeconds, [&](const double stepSeconds) {
                const float deltaSeconds = static_cast<float>(stepSeconds);
                camera.Rotate(yawInput * deltaSeconds, pitchInput * deltaSeconds);
                if (!pendingMouseApplied)
                {
                    camera.Rotate(static_cast<float>(pendingMouseYaw),
                                  static_cast<float>(pendingMousePitch));
                    pendingMouseApplied = true;
                }
                camera.MoveLocal(forward, right, vertical, moveSpeed, deltaSeconds);
                elapsedSeconds += deltaSeconds;
            });
        if (pendingMouseApplied)
        {
            pendingMouseYaw = 0.0;
            pendingMousePitch = 0.0;
        }

        if (simulation.droppedSeconds > 0.0)
            LogInfo("Simulation fell behind; excess whole steps were dropped to preserve frame responsiveness.");

        const int width = window.Width();
        const int height = window.Height();
        if (width > 0 && height > 0)
        {
            if (!renderer.Resize(width, height))
            {
                LogError("Renderer resize failed; stopping the engine loop.");
                return 1;
            }
            const math::Vec3& position = camera.Position();
            renderer.Render(elapsedSeconds, position.x, position.y, position.z,
                            camera.Yaw(), camera.Pitch());
        }
    }
    return 0;
}
} // namespace

int Engine::Run(const bool useD3D12)
{
    Window window;
    if (!window.Create(L"VantaEngine | Runtime Sample", 1280, 720))
    {
        LogError("Engine startup failed while creating the window.");
        return 1;
    }

    if (useD3D12)
    {
        LogInfo("Rendering backend: Direct3D 12.");
        D3D12Renderer renderer;
        if (!renderer.Initialize(window.NativeHandle(), window.Width(), window.Height()))
        {
            LogError("Engine startup failed while initializing the Direct3D 12 backend.");
            return 1;
        }
        const int result = RunCameraLoop(window, renderer);
        LogInfo("Direct3D 12 backend shut down cleanly.");
        return result;
    }

    const auto graphicsCapabilities = renderer::QueryNvidiaRenderingCapabilities();
    LogInfo(graphicsCapabilities.diagnostic.c_str());
    LogInfo("Rendering backend: Direct3D 11 (compatibility).");
    LogInfo("NVIDIA DLSS, AMD FSR, Intel XeSS, and DXR rendering are not integrated yet.");

    D3D11Renderer renderer;
    if (!renderer.Initialize(window.NativeHandle(), window.Width(), window.Height()))
    {
        LogError("Engine startup failed while initializing the Direct3D 11 backend.");
        return 1;
    }

    const int result = RunCameraLoop(window, renderer);
    LogInfo("Engine shut down cleanly.");
    return result;
}
} // namespace vanta
