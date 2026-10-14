# VantaEngine

VantaEngine is an early-stage native C++23 3D game engine designed for direct control, a small dependency footprint, and fast iteration without a heavyweight editor.

> **Current milestone:** native Win32 runtime, keyboard-controlled free-fly camera, Wavefront OBJ model import, MTL diffuse-texture discovery, and WIC-backed image textures.

## What runs right now

Launching the engine opens a 1280×720 native window and renders a real-time procedural landscape: rolling terrain, a deterministic low-poly pine forest, the imported stone house, and test props. The terrain and forest are generated at startup and uploaded as merged GPU meshes. Geometry is uploaded to Direct3D 11 buffers and the diffuse image is uploaded as a GPU texture; this is not a prerecorded animation or a UI mock-up.

The renderer now includes:
- Indexed triangle geometry with per-face normals and color.
- Left-handed perspective and view matrices from the engine's math layer.
- A depth/stencil buffer and depth testing.
- A directional diffuse-lighting shader.
- A dynamic constant buffer for per-frame transforms.
- Window resize handling and a continuously updated frame loop.
- Procedural 128×128 rolling terrain with gradient-derived normals, altitude/slope biome coloring, and tiled UVs.
- A deterministic pine forest with varied tree heights, tiered low-poly foliage, trunks, and merged geometry to limit draw calls.
- A full-screen sunset sky with a solar glow, per-pixel specular highlights, hemisphere-style sky fill, warm directional lighting, and distance-based atmospheric fog for depth separation.
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

- Windows backend only; the renderer is currently Direct3D 11.
- The demo house and procedural forest are stylized low-poly assets, not photorealistic scans. Terrain and tree geometry are generated once at startup; there is no streaming or level-of-detail system yet.
- Lighting currently uses a single directional light and approximate specular/hemisphere shading. Shadow maps, screen-space ambient occlusion, physically based metallic/roughness materials, normal maps, temporal anti-aliasing, and post-processing are not implemented yet.
- NVIDIA DLSS and hardware ray tracing are not enabled: DLSS integration needs a supported NVIDIA SDK/runtime and suitable render pipeline, while RTX ray tracing requires a Direct3D 12/DXR backend rather than this D3D11 renderer.
- No mouse-look, audio, physics, scripting, or game packaging yet.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. Implemented.
3. Perspective camera, depth buffer, and transform-aware indexed mesh rendering. Implemented.
4. Keyboard input and controllable free-fly camera. Implemented.
5. OBJ mesh import and textured diffuse materials. Implemented as an initial pipeline.
6. PCF shadow mapping, screen-space ambient occlusion, and better material/lighting controls (techniques used in high-quality real-time renderers).
7. Direct3D 12 backend for DXR experiments and optional NVIDIA DLSS integration where supported.
8. Multiple material groups, normal maps/PBR, scene representation, and game-facing APIs.
