# VantaEngine

VantaEngine is an early-stage native C++23 3D game engine designed for direct control, a small dependency footprint, and fast iteration without a heavyweight editor.

> **Current milestone:** native Win32 runtime, keyboard-controlled free-fly camera, Wavefront OBJ model import, MTL diffuse-texture discovery, and WIC-backed image textures.

## What runs right now

Launching the engine opens a 1280×720 native window and renders a real-time 3D test environment: a broad ground surface, a low-poly house loaded from a Wavefront OBJ file with an MTL material and diffuse BMP texture, and smaller test objects. Geometry is uploaded to Direct3D 11 buffers and the diffuse image is uploaded as a GPU texture; this is not a prerecorded animation or a UI mock-up.

The renderer now includes:
- Indexed triangle geometry with per-face normals and color.
- Left-handed perspective and view matrices from the engine's math layer.
- A depth/stencil buffer and depth testing.
- A directional diffuse-lighting shader.
- A dynamic constant buffer for per-frame transforms.
- Window resize handling and a continuously updated frame loop.
- Multiple world-space objects rendered with individual transforms against a scaled ground slab.
- A keyboard-controlled free-fly camera for inspecting the scene from different angles.
- Wavefront OBJ import with polygon triangulation, positive/negative position indices, UV coordinates, supplied normals, and generated face normals when normals are absent.
- MTL `map_Kd` diffuse texture discovery and Windows Imaging Component (WIC) decoding for common Windows-supported image formats.
- UV interpolation, linear wrap sampling, and texture-aware diffuse lighting in the Direct3D 11 pixel shader.
- A small sample house asset under `assets/models/` so the importer and texture path are exercised on startup.

## Camera controls

The camera is controllable while the VantaEngine window is focused:

- **W / A / S / D** — move horizontally relative to the camera direction.
- **Q / E** — move down / up.
- **Arrow keys** — look around.
- **Escape** — exit.

Movement is delta-time based, so it remains consistent across frame rates. The camera uses the engine math layer to build a view matrix each frame.

## Architecture

- core: runtime entry point, logging, frame-time policy
- math: Vec3, Vec4, Quaternion, Mat4
- platform: Win32 window/event lifecycle
- renderer: Direct3D 11 device, shaders, buffers, swap chain, depth target
- tests: GPU-independent runtime and math tests

## Math conventions

The math layer is header-only and has no Windows or graphics API dependencies.

- Vec3 supports arithmetic, dot/cross products, and safe normalization.
- Quaternion supports axis-angle creation, normalization, conjugation, multiplication, and vector rotation.
- Mat4 uses column-major storage, column vectors, and matrix-times-vector transforms. Translation occupies column 3.
- LookAtLH is a left-handed view matrix where +Z is forward.
- Perspective is left-handed and maps normalized depth to Direct3D's [0, 1] range.
- Matrix composition T * R * S * point applies scale, then rotation, then translation.

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

CTest runs without creating a window or requiring a GPU. It covers the frame-time policy and the GPU-independent math foundation. The rendering path is exercised by launching the app locally.

GitHub Actions builds and tests Debug and Release on Windows. VS Code tasks provide build/test shortcuts; F5 launches under the MSVC debugger when the C/C++ extension is installed.

## Current limitations

- Windows backend only.
- The demo model is a low-poly sample house rather than a production-quality real-world asset. The importer currently handles one diffuse texture per OBJ and does not yet support multiple material groups, normal maps, metallic/roughness PBR, skeletal animation, or a scene editor.
- No mouse-look, model loading, audio, physics, scripting, or game packaging yet.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. Implemented.
3. Perspective camera, depth buffer, and transform-aware indexed mesh rendering. Implemented.
4. Keyboard input and controllable free-fly camera. Implemented.
5. OBJ mesh import and textured diffuse materials. Implemented as an initial pipeline.
6. Multiple material groups, material resource lifetime, normal maps/PBR, and scene representation.
7. Game-facing APIs and packaging.
