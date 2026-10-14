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
- sRGB diffuse texture sampling, GPU-generated mip chains for diffuse and linear PBR maps, seamless procedural grass/stone/wood albedo textures with triplanar projection, and ACES-style tone mapping for more physically plausible light response.
- A dynamic constant buffer for per-frame transforms and light-space transforms.
- A 2048×2048 directional shadow depth pass for terrain, forest, imported house, and props, with slope-aware depth bias and 3×3 PCF filtering.
- Window resize handling and a continuously updated frame loop.
- Procedural 128×128 rolling terrain with gradient-derived normals, altitude/slope biome coloring, and tiled UVs.
- A deterministic conifer forest with varied trunk and crown sizes, layered boughs, exposed buttress roots, bark grooves, ferns, shrubs, angular boulders, fallen timber with branch stubs and cut ends, and merged geometry to limit draw calls.
- A winding gravel access road with shoulders, broken center paint, hundreds of individually varied gravel stones, and a stream with irregular rock banks.
- Multiple timber cabins with individual overlapping roof shingles, horizontal side-wall weatherboards, vertical front siding, pane mullions, door planks and latch, porch boards, stone foundation courses, chimney mortar seams, fascia, and steps.
- Timber fence runs, roadside utility poles, thousands of crossed grass blades, scattered leaf litter and twigs, small wildflowers, and layered world-space shader noise, seamless generated grass/stone/wood texture tiles, triplanar projection on natural surfaces, and procedural roughness breakup and derivative-based micro-bump shading to reduce obvious repetition and give surfaces subtle relief.
- A full-screen sunset sky with a solar glow, per-pixel specular highlights, hemisphere-style sky fill, warm directional lighting, and distance-based atmospheric fog for depth separation.
- A keyboard-controlled free-fly camera for inspecting the scene from different angles.
- Wavefront OBJ import with polygon triangulation, positive/negative position indices, UV coordinates, supplied normals, and generated face normals when normals are absent.
- MTL diffuse, tangent-space normal, roughness, metallic, and ambient-occlusion map loading through Windows Imaging Component (WIC). Diffuse is decoded as sRGB; numeric data maps remain linear. Missing maps are optional, while referenced maps must decode and upload successfully.
- UV interpolation, linear wrap sampling, sRGB diffuse sampling, and GPU-generated mip chains for diffuse, normal, roughness, metallic, and ambient-occlusion maps. Numeric maps remain in linear space.
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
- The procedural valley now includes denser ground clutter, tree roots, bark seams, road gravel, ferns, leaf litter, twigs, and layered world-space material variation. It remains a handcrafted prototype, not photorealistic scanned assets: geometry silhouettes and most environment materials still need authored models and complete PBR texture sets. Terrain and environment geometry are generated once at startup; there is no streaming, LOD system, foliage animation, or baked lighting yet.
- The active renderer is still Direct3D 11 and uses one directional light. Imported OBJ/MTL materials now sample optional normal, roughness, metallic, and ambient-occlusion maps in addition to sRGB base color. Normal mapping currently builds a tangent frame from pixel derivatives; there is no glTF 2.0 importer, multi-material/submesh support, or environment IBL yet. Texture mip chains are generated on the GPU at startup. The environment's grass, stone and timber texture tiles are synthesized deterministically at startup and sampled with world-space/triplanar mapping; they are not artist-authored scanned materials.
- The engine now reports D3D12/DXR hardware capability, but it does **not** yet render with DXR or path tracing. That requires a separate D3D12 renderer, acceleration structures, ray-generation/miss/hit shaders, and a working dispatch/presentation path.
- NVIDIA DLSS is **not integrated yet**. DLSS needs NVIDIA Streamline (or the supported DLSS SDK path), motion vectors, depth, render-resolution management, and a compatible D3D12 render pipeline. Capability detection alone does not enable DLSS.
- No mouse-look, audio, physics, scripting, or game packaging yet.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. Implemented.
3. Perspective camera, depth buffer, and transform-aware indexed mesh rendering. Implemented.
4. Keyboard input and controllable free-fly camera. Implemented.
5. OBJ mesh import and textured diffuse materials. Implemented as an initial pipeline.
6. glTF 2.0 import, multi-material meshes, authored asset packs, mip-chain generation, and environment IBL.
7. Direct3D 12 renderer with DXR acceleration structures, ray-traced shadows/reflections, and a small path-tracing test scene.
8. NVIDIA Streamline/DLSS integration after temporal jitter, motion vectors, depth, and render-resolution control are available.
9. PCF directional shadow mapping. Implemented as an initial single-map pass; cascades, contact hardening, and temporal stabilization remain future work.
10. Screen-space ambient occlusion, temporal anti-aliasing, and scene representation.
11. Replace procedural placeholder assets with authored glTF environment kits and full PBR texture sets; add mouse-look, collision, and playable character movement.
