# VantaEngine

VantaEngine is an early-stage native C++23 3D game engine designed for direct control, a small dependency footprint, and fast iteration without a heavyweight editor.

> **Current milestone:** native Win32 window, Direct3D 11 triangle renderer, and a standalone tested 3D math foundation. The screen still displays the renderer test triangle; camera and mesh rendering are the next milestone.

## Architecture

- core: runtime entry point, logging, frame-time policy
- math: Vec3, Vec4, Quaternion, Mat4
- platform: Win32 window/event lifecycle
- renderer: Direct3D 11 device, shaders, buffers, swap chain
- tests: GPU-independent runtime and math tests

## Math conventions

The math layer is header-only and has no Windows or graphics API dependencies.

- Vec3 supports arithmetic, dot/cross products, and safe normalization. Near-zero vectors return no value instead of producing NaNs.
- Quaternion supports axis-angle creation, normalization, conjugation, multiplication, and vector rotation.
- Mat4 uses column-major storage, column vectors, and matrix-times-vector transforms. Translation occupies column 3.
- LookAtLH is a left-handed view matrix where +Z is forward.
- Perspective is left-handed and maps normalized depth to Direct3D's [0, 1] range.
- Matrix composition T * R * S * point applies scale, then rotation, then translation.
- Invalid projection inputs return a zero matrix; degenerate look-at vectors and invalid quaternion axes return no value.

These conventions match the current Direct3D 11 backend and must be respected by renderer code.

## Requirements

- Windows 10/11 x64
- Visual Studio with MSVC x64 C++ tools
- Visual Studio CMake/Ninja components, or CMake 3.25+ and Ninja on PATH
- PowerShell 5.1+

No third-party runtime dependencies: the engine uses Win32, Direct3D 11, DXGI, and the Windows SDK shader compiler.

## Build, test, run

From the repository root:

    git pull origin main
    .\scripts\build.ps1 -Test -Run

This builds the engine, runs CTest, then opens the VantaEngine window. Press Escape or close the window to exit.

Other commands:

    .\scripts\build.ps1
    .\scripts\build.ps1 -Configuration Release
    .\scripts\build.ps1 -Test
    .\scripts\build.ps1 -Clean -Test

The clean option removes only generated output in build/.

## Tests and CI

CTest runs without creating a window or requiring a GPU. Coverage includes frame-delta clamping, vector operations, degenerate normalization, point-versus-direction transforms, matrix composition, quaternion rotation, Direct3D perspective depth mapping, and look-at edge cases.

GitHub Actions builds and tests Debug and Release on Windows. VS Code tasks provide build/test shortcuts; F5 launches under the MSVC debugger when the C/C++ extension is installed.

## Current limitations

- Windows backend only.
- Renderer still draws one triangle; math is not yet wired into rendering.
- No depth buffer, controllable camera, mesh loading, scene model, audio, physics, scripting, or editor.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. This milestone.
3. Camera, depth buffer, and transform-aware 3D mesh rendering.
4. Input system and controllable camera.
5. Mesh/material/resource lifecycle and scene representation.
6. Lighting, model import, profiling, and tools.
7. Game-facing APIs and packaging.
