#include "vanta/core/Camera.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
using vanta::Camera;
using vanta::math::Vec3;
constexpr float kTolerance = 1.0e-4F;

bool Near(float a, float b) { return std::abs(a - b) <= kTolerance; }

struct TestCase
{
    std::string_view name;
    bool (*run)();
};

bool initialPose()
{
    const Camera camera({1.0F, 2.0F, 3.0F}, 0.0F, 0.0F);
    return Near(camera.Position().x, 1.0F) && Near(camera.Position().y, 2.0F) &&
           Near(camera.Position().z, 3.0F) && Near(camera.Forward().z, 1.0F);
}

bool yawRotation()
{
    Camera camera;
    camera.Rotate(vanta::math::kPi * 0.5F, 0.0F);
    const Vec3 forward = camera.Forward();
    return Near(forward.x, 1.0F) && Near(forward.z, 0.0F);
}

bool pitchClamped()
{
    Camera camera;
    camera.Rotate(0.0F, 100.0F);
    return camera.Pitch() < vanta::math::kPi * 0.5F &&
           camera.Pitch() > 1.5F && std::isfinite(camera.Up().Length());
}

bool diagonalMovementIsNormalized()
{
    Camera axial;
    Camera diagonal;
    axial.MoveLocal(1.0F, 0.0F, 0.0F, 5.0F, 0.2F);
    diagonal.MoveLocal(1.0F, 1.0F, 0.0F, 5.0F, 0.2F);
    const float axialDistance = axial.Position().Length();
    const float diagonalDistance = diagonal.Position().Length();
    return Near(axialDistance, 1.0F) && Near(diagonalDistance, 1.0F);
}

bool movementUsesCameraYaw()
{
    Camera camera;
    camera.SetOrientation(vanta::math::kPi * 0.5F, 0.0F);
    camera.MoveLocal(1.0F, 0.0F, 0.0F, 2.0F, 0.5F);
    return Near(camera.Position().x, 1.0F) && Near(camera.Position().z, 0.0F);
}

bool invalidMovementIsIgnored()
{
    Camera camera;
    camera.MoveLocal(1.0F, 0.0F, 0.0F, 2.0F,
                     std::numeric_limits<float>::quiet_NaN());
    camera.MoveLocal(1.0F, 0.0F, 0.0F, -2.0F, 1.0F);
    return Near(camera.Position().Length(), 0.0F);
}

bool invalidPoseIsIgnored()
{
    Camera camera({1.0F, 2.0F, 3.0F});
    camera.SetPosition({std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    camera.SetOrientation(std::numeric_limits<float>::quiet_NaN(), 0.0F);
    return Near(camera.Position().x, 1.0F) && Near(camera.Yaw(), 0.0F);
}

bool viewMatrixTracksPose()
{
    Camera camera({0.0F, 0.0F, -5.0F});
    const Vec3 eye = [&] {
        const auto transformed = camera.ViewMatrix().Transform({0.0F, 0.0F, -5.0F, 1.0F});
        return Vec3{transformed.x, transformed.y, transformed.z};
    }();
    return Near(eye.x, 0.0F) && Near(eye.y, 0.0F) && Near(eye.z, 0.0F);
}

constexpr std::array kCases{
    TestCase{"initial camera pose", initialPose},
    TestCase{"yaw rotates forward direction", yawRotation},
    TestCase{"pitch remains below vertical singularity", pitchClamped},
    TestCase{"diagonal movement has normalized speed", diagonalMovementIsNormalized},
    TestCase{"movement follows camera yaw", movementUsesCameraYaw},
    TestCase{"invalid movement inputs are ignored", invalidMovementIsIgnored},
    TestCase{"invalid pose inputs are ignored", invalidPoseIsIgnored},
    TestCase{"view matrix follows camera position", viewMatrixTracksPose},
};
} // namespace

int main()
{
    int failures = 0;
    for (const TestCase& test : kCases)
    {
        if (!test.run())
        {
            std::cerr << "FAIL: " << test.name << '\n';
            ++failures;
        }
        else
        {
            std::cout << "PASS: " << test.name << '\n';
        }
    }
    std::cout << (kCases.size() - static_cast<std::size_t>(failures))
              << '/' << kCases.size() << " camera tests passed.\n";
    return failures == 0 ? 0 : 1;
}
