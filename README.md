# VantaEngine

VantaEngine is an early-stage native C++23 3D game engine designed for direct control, a small dependency footprint, and fast iteration without a heavyweight editor.

> **Current milestone:** native Direct3D 11 renderer with Wavefront OBJ/MTL assets, Cook–Torrance PBR material shading, ACES-style tone mapping, a 2048×2048 directional shadow map with 3×3 percentage-closer filtering, and D3D12/DXR hardware capability diagnostics.

## What runs right now

Launching the engine opens a 1280×720 native window and renders a real-time forest-valley environment: rolling terrain, a winding gravel road with worn center markings and shoulders, a rocky stream, multiple timber cabins with pitched roofs, board siding, windows, doors, porches and chimneys, timber fences, utility poles, fallen logs, angular boulders, undergrowth, and thousands of grass blades. These deterministic scene meshes are generated at startup and merged into a small number of GPU buffers. Geometry is rendered live through Direct3D 11; this is not a prerecorded animation or a UI mock-up.

The renderer now includes:
- Indexed triangle geometry with per-face normals and color.
- Left-handed perspective and view matrices from the engine's math layer.
- A depth/stencil buffer and depth testing.
- Cook–Torrance microfacet PBR shading using GGX normal distribution, Smith geometry, and Schlick Fresnel.
- Per-material base-color, roughness, and metallic factors read from MTL (`Kd`, `Pr`, `Pm`, with `Ns` as a legacy roughness fallback).
- sRGB diffuse texture sampling and ACES-style tone mapping for more physically plausible light response.
- A dynamic constant buffer for per-frame transforms and light-space transforms.
- A 2048×2048 directional shadow depth pass for terrain, forest, imported house, and props, with slope-aware depth bias and 3×3 PCF filtering.
- Window resize handling and a continuously updated frame loop.
- Procedural 128×128 rolling terrain with gradient-derived normals, altitude/slope biome coloring, and tiled UVs.
- A deterministic, dense conifer forest with varied trunk and crown sizes, layered boughs, shrubs, angular boulders, fallen timber, and merged geometry to limit draw calls.
- A winding gravel access road with shoulders, broken center paint, and a stream with irregular rock banks.
- Multiple timber cabins with pitched roofs, fascia, siding seams, stone footings, doors, window frames, porch boards, steps, and chimneys.
- Timber fence runs, roadside utility poles, thousands of crossed grass blades, and stable world-space surface grain for more visual breakup.
- A full-screen sunset sky with a solar glow, per-pixel specular highlights, hemisphere-style sky fill, warm directional lighting, and distance-based atmospheric fog for depth separation.
- A keyboard-controlled free-fly camera for inspecting the scene from different angles.
- Wavefront OBJ import with polygon triangulation, positive/negative position indices, UV coordinates, supplied normals, and generated face normals when normals are absent.
- MTL `map_Kd` diffuse-texture loading through Windows Imaging Component (WIC), plus path discovery for common normal/roughness/metallic map tags. Only the diffuse map is currently uploaded and sampled.
- UV interpolation, linear wrap sampling, and sRGB diffuse texture sampling in the Direct3D 11 pixel shader.
- Startup diagnostics for the high-performance D3D12 adapter and its DXR tier, when available.
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
- The procedural valley has substantially more environmental geometry, but it is still a handcrafted/stylized prototype—not a finished CS2/Fortnite-quality map and not photorealistic scanned assets. Most environment materials still use vertex colors plus shader grain rather than authored PBR texture sets. Terrain and environment geometry are generated once at startup; there is no streaming, LOD system, foliage animation, or baked lighting yet.
- The active renderer is still Direct3D 11 and uses one directional light. The new PBR shader uses scalar roughness/metallic values, but it does not yet sample normal/roughness/metallic textures or support multiple material groups. The importer remains OBJ/MTL; glTF 2.0 support is not implemented yet.
- The engine now reports D3D12/DXR hardware capability, but it does **not** yet render with DXR or path tracing. That requires a separate D3D12 renderer, acceleration structures, ray-generation/miss/hit shaders, and a working dispatch/presentation path.
- NVIDIA DLSS is **not integrated yet**. DLSS needs NVIDIA Streamline (or the supported DLSS SDK path), motion vectors, depth, render-resolution management, and a compatible D3D12 render pipeline. Capability detection alone does not enable DLSS.
- No mouse-look, audio, physics, scripting, or game packaging yet.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. Implemented.
3. Perspective camera, depth buffer, and transform-aware indexed mesh rendering. Implemented.
4. Keyboard input and controllable free-fly camera. Implemented.
5. OBJ mesh import and textured diffuse materials. Implemented as an initial pipeline.
6. glTF 2.0 import, multi-material meshes, and sampling base-color, normal, roughness, and metallic textures.
7. Direct3D 12 renderer with DXR acceleration structures, ray-traced shadows/reflections, and a small path-tracing test scene.
8. NVIDIA Streamline/DLSS integration after temporal jitter, motion vectors, depth, and render-resolution control are available.
9. PCF directional shadow mapping. Implemented as an initial single-map pass; cascades, contact hardening, and temporal stabilization remain future work.
10. Screen-space ambient occlusion, temporal anti-aliasing, and scene representation.
11. Replace procedural placeholder assets with authored glTF environment kits and full PBR texture sets; add mouse-look, collision, and playable character movement.
