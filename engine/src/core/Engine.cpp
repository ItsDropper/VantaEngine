#include "vanta/core/Engine.hpp"

#include "vanta/core/FixedStepClock.hpp"
#include "vanta/core/Log.hpp"
#include "vanta/core/Camera.hpp"
#include "vanta/platform/Window.hpp"
#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/D3D12Renderer.hpp"
#include "vanta/renderer/NvidiaRendering.hpp"
#include "vanta/renderer/SceneSubmission.hpp"

#include <chrono>
#include <utility>

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
    Camera previousCamera = camera;
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
                previousCamera = camera;
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
            const Camera renderCamera = Camera::Interpolate(
                previousCamera, camera, static_cast<float>(simulation.interpolationAlpha));
            const math::Vec3& position = renderCamera.Position();
            renderer.Render(elapsedSeconds, position.x, position.y, position.z,
                            renderCamera.Yaw(), renderCamera.Pitch());
            if (renderer.IsDeviceLost()) {
                LogError("Graphics device was removed. Recreate the renderer and its window resources before resuming.");
                return 1;
            }
        }
    }
    return 0;
}
template <typename Renderer>
int RunSceneSubmissionLoop(Window& window, Renderer& renderBackend)
{
    renderer::SceneSubmission scene;
    renderer::MeshDescription cube;
    const auto appendFace = [&cube](const math::Vec3& normal, const math::Vec3& a,
        const math::Vec3& b, const math::Vec3& c, const math::Vec3& d) {
        const std::uint32_t base = static_cast<std::uint32_t>(cube.vertices.size());
        cube.vertices.push_back({a, normal, 0.0F, 1.0F});
        cube.vertices.push_back({b, normal, 0.0F, 0.0F});
        cube.vertices.push_back({c, normal, 1.0F, 0.0F});
        cube.vertices.push_back({d, normal, 1.0F, 1.0F});
        cube.indices.insert(cube.indices.end(),
            {base, base + 1U, base + 2U, base, base + 2U, base + 3U});
    };
    appendFace({0.0F, 0.0F, 1.0F}, {-1.0F, -1.0F, 1.0F}, {-1.0F, 1.0F, 1.0F},
        {1.0F, 1.0F, 1.0F}, {1.0F, -1.0F, 1.0F});
    appendFace({0.0F, 0.0F, -1.0F}, {1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, -1.0F},
        {-1.0F, 1.0F, -1.0F}, {-1.0F, -1.0F, -1.0F});
    appendFace({0.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 1.0F}, {-1.0F, 1.0F, -1.0F},
        {1.0F, 1.0F, -1.0F}, {1.0F, 1.0F, 1.0F});
    appendFace({0.0F, -1.0F, 0.0F}, {-1.0F, -1.0F, -1.0F}, {-1.0F, -1.0F, 1.0F},
        {1.0F, -1.0F, 1.0F}, {1.0F, -1.0F, -1.0F});
    appendFace({1.0F, 0.0F, 0.0F}, {1.0F, -1.0F, 1.0F}, {1.0F, 1.0F, 1.0F},
        {1.0F, 1.0F, -1.0F}, {1.0F, -1.0F, -1.0F});
    appendFace({-1.0F, 0.0F, 0.0F}, {-1.0F, -1.0F, -1.0F}, {-1.0F, 1.0F, -1.0F},
        {-1.0F, 1.0F, 1.0F}, {-1.0F, -1.0F, 1.0F});

    renderer::MeshDescription ground;
    ground.vertices = {
        {{-30.0F, -1.1F, -10.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        {{-30.0F, -1.1F, 60.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
        {{30.0F, -1.1F, 60.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 1.0F},
        {{30.0F, -1.1F, -10.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F}
    };
    ground.indices = {0U, 1U, 2U, 0U, 2U, 3U};

    const renderer::MeshHandle cubeMesh = scene.CreateMesh(std::move(cube));
    const renderer::MeshHandle groundMesh = scene.CreateMesh(std::move(ground));
    const renderer::MaterialHandle cubeMaterial = scene.CreateMaterial(
        {{0.10F, 0.42F, 0.92F}, 0.38F, 0.05F});
    const renderer::MaterialHandle groundMaterial = scene.CreateMaterial(
        {{0.22F, 0.27F, 0.20F}, 0.92F, 0.0F});
    if (!cubeMesh.IsValid() || !groundMesh.IsValid() ||
        !cubeMaterial.IsValid() || !groundMaterial.IsValid()) {
        LogError("Scene-submission sample could not create its mesh or material resources.");
        return 1;
    }

    const renderer::InstanceHandle cubeInstance = scene.CreateInstance(cubeMesh, cubeMaterial);
    const renderer::InstanceHandle groundInstance = scene.CreateInstance(groundMesh, groundMaterial);
    if (!cubeInstance.IsValid() || !groundInstance.IsValid()) {
        LogError("Scene-submission sample could not create its scene instances.");
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    float elapsedSeconds = 0.0F;
    Camera camera({0.0F, 1.4F, -7.0F}, 0.0F, -0.04F);
    Camera previousCamera = camera;
    FixedStepClock simulationClock;
    double pendingMouseYaw = 0.0;
    double pendingMousePitch = 0.0;
    std::uint64_t frameNumber = 0;
    LogInfo("Scene-submission sample: game-owned mesh/material/instance resources; WASD move, arrows or right mouse look, Escape exits.");

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
        if (input.IsMouseButtonDown(1)) {
            pendingMouseYaw += static_cast<double>(input.MouseDeltaX()) * mouseSensitivity;
            pendingMousePitch -= static_cast<double>(input.MouseDeltaY()) * mouseSensitivity;
        } else {
            pendingMouseYaw = 0.0;
            pendingMousePitch = 0.0;
        }

        bool pendingMouseApplied = false;
        const FixedStepResult simulation = simulationClock.Advance(frameSeconds,
            [&](const double stepSeconds) {
                const float deltaSeconds = static_cast<float>(stepSeconds);
                previousCamera = camera;
                camera.Rotate(yawInput * deltaSeconds, pitchInput * deltaSeconds);
                if (!pendingMouseApplied) {
                    camera.Rotate(static_cast<float>(pendingMouseYaw),
                        static_cast<float>(pendingMousePitch));
                    pendingMouseApplied = true;
                }
                camera.MoveLocal(forward, right, vertical, moveSpeed, deltaSeconds);
                elapsedSeconds += deltaSeconds;
            });
        if (pendingMouseApplied) {
            pendingMouseYaw = 0.0;
            pendingMousePitch = 0.0;
        }
        if (simulation.droppedSeconds > 0.0)
            LogInfo("Simulation fell behind; excess whole steps were dropped to preserve frame responsiveness.");

        const int width = window.Width();
        const int height = window.Height();
        if (width <= 0 || height <= 0)
            continue;
        if (!renderBackend.Resize(width, height)) {
            LogError("Scene-submission sample renderer resize failed.");
            return 1;
        }

        const Camera renderCamera = Camera::Interpolate(previousCamera, camera,
            static_cast<float>(simulation.interpolationAlpha));
        const math::Vec3& position = renderCamera.Position();
        const auto rotation = math::Quaternion::FromAxisAngle({0.0F, 1.0F, 0.0F},
            elapsedSeconds * 0.45F);
        const math::Mat4 cubeWorld = math::Mat4::Translation({0.0F, 0.0F, 5.0F}) *
            math::Mat4::Rotation(rotation.value_or(math::Quaternion::Identity()));
        if (!scene.SetTransform(cubeInstance, cubeWorld)) {
            LogError("Scene-submission sample failed to update its cube transform.");
            return 1;
        }

        renderer::FrameView view;
        view.view = renderCamera.ViewMatrix();
        view.projection = math::Mat4::Perspective(math::DegreesToRadians(60.0F),
            static_cast<float>(width) / static_cast<float>(height), 0.1F, 120.0F);
        view.cameraPosition = position;
        view.elapsedSeconds = elapsedSeconds;
        view.frameNumber = frameNumber++;
        const renderer::SceneFrame frame = scene.BuildFrame(view);
        if (!renderBackend.Render(frame)) {
            LogError("Scene-submission sample failed to render its game-owned frame.");
            return 1;
        }
        if (renderBackend.IsDeviceLost()) {
            LogError("Graphics device was removed. Recreate the renderer and its window resources before resuming.");
            return 1;
        }
    }
    return 0;
}

} // namespace

int Engine::Run(const bool useD3D12, const bool useSceneSubmissionSample)
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
        const int result = useSceneSubmissionSample
            ? RunSceneSubmissionLoop(window, renderer) : RunCameraLoop(window, renderer);
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

    const int result = useSceneSubmissionSample
        ? RunSceneSubmissionLoop(window, renderer) : RunCameraLoop(window, renderer);
    LogInfo("Engine shut down cleanly.");
    return result;
}
} // namespace vanta
