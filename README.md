# VantaEngine

VantaEngine is an early-stage native C++23 3D game engine designed for direct control, a small dependency footprint, and fast iteration without a heavyweight editor.

> **Current milestone:** native Direct3D 12 scene renderer with a deterministic procedural forest valley, terrain, cabins, road, creek, vegetation, indexed geometry, depth testing, directional lighting, atmospheric fog, and a free-fly camera. A Direct3D 11 compatibility renderer remains available.

## What runs right now

Launching the engine opens a 1280×720 native window and renders a real-time forest-valley environment: rolling terrain, a winding gravel road with worn center markings and shoulders, a rocky stream, multiple timber cabins with pitched roofs, board siding, windows, doors, porches and chimneys, timber fences, utility poles, fallen logs, angular boulders, undergrowth, and thousands of grass blades. These deterministic scene meshes are generated at startup and merged into a small number of GPU buffers. Geometry is rendered live through Direct3D 12; this is not a prerecorded animation or a UI mock-up.

The renderer now includes:
- Indexed triangle geometry with per-face normals and color.
- Left-handed perspective and view matrices from the engine's math layer.
- A depth/stencil buffer and depth testing.
- Cook–Torrance microfacet PBR shading using GGX normal distribution, Smith geometry, and Schlick Fresnel.
- Per-material base-color, roughness, and metallic factors read from MTL (`Kd`, `Pr`, `Pm`, with `Ns` as a legacy roughness fallback).
- sRGB diffuse texture sampling, GPU-generated mip chains for diffuse and linear PBR maps, seamless procedural grass/stone/wood albedo textures with triplanar projection, and ACES-style tone mapping for more physically plausible light response.
- A linear `R16G16B16A16_FLOAT` HDR scene target followed by full-screen ACES tone mapping, display transfer, thresholded 5×5 highlight bloom, and a depth-neighborhood screen-space contact-occlusion approximation. Scene shading remains linear until final composition.
- Temporal anti-aliasing with an eight-sample Halton projection-jitter sequence, depth-based world-position reconstruction, previous-frame view-projection reprojection, ping-pong HDR history, neighborhood clipping, and camera-teleport history rejection. This reduces sub-pixel shimmer, but there are no per-object motion vectors yet, so moving objects can still ghost.
- Analytic hemisphere/environment reflection lighting for PBR materials. This is an environment-lighting approximation, not full HDR environment-map convolution or a split-sum BRDF.
- A dynamic constant buffer for per-frame transforms and light-space transforms.
- Three camera-relative 2048×2048 cascaded directional shadow depth passes for terrain, forest, imported house, and props, with distance-based cascade selection, slope-aware depth bias, and 3×3 PCF filtering.
- Window resize handling and a continuously updated frame loop.
- Procedural 128×128 rolling terrain with gradient-derived normals, altitude/slope biome coloring, and tiled UVs.
- A deterministic conifer forest with varied trunk and crown sizes, layered boughs, exposed buttress roots, bark grooves, ferns, shrubs, angular boulders, fallen timber with branch stubs and cut ends, and merged geometry to limit draw calls.
- A winding gravel access road with shoulders, broken center paint, hundreds of individually varied gravel stones, and a stream with irregular rock banks.
- Multiple timber cabins with individual overlapping roof shingles, horizontal side-wall weatherboards, vertical front siding, pane mullions, door planks and latch, porch boards, stone foundation courses, chimney mortar seams, fascia, and steps.
- Timber fence runs, roadside utility poles, thousands of crossed grass blades, scattered leaf litter and twigs, small wildflowers, and layered world-space shader noise, seamless generated grass/stone/wood texture tiles, triplanar projection on natural surfaces, and procedural roughness breakup and derivative-based micro-bump shading to reduce obvious repetition and give surfaces subtle relief.
- A full-screen sunset sky with a solar glow, per-pixel specular highlights, hemisphere-style sky fill, warm directional lighting, and distance-based atmospheric fog for depth separation.
- A keyboard-controlled free-fly camera for inspecting the scene from different angles.
- Wavefront OBJ/MTL import with polygon triangulation, positive/negative position indices, UV coordinates, supplied normals, and generated face normals when normals are absent.
- glTF 2.0/GLB static-mesh import with scene-node transforms, indexed triangle primitives, UVs, imported or generated normals, per-primitive submeshes, multiple PBR materials, base-color/normal/occlusion maps, metallic-roughness factors, and the glTF packed G (roughness) / B (metallic) channel convention. External texture images are supported; embedded data-URI/GLB images currently return a clear import error.
- MTL diffuse, tangent-space normal, roughness, metallic, and ambient-occlusion map loading through Windows Imaging Component (WIC). Diffuse is decoded as sRGB; numeric data maps remain linear. Missing maps are optional, while referenced maps must decode and upload successfully.
- UV interpolation, linear wrap sampling, sRGB diffuse sampling, and GPU-generated mip chains for diffuse, normal, roughness, metallic, and ambient-occlusion maps. Numeric maps remain in linear space.
- Startup diagnostics for NVIDIA, AMD, and Intel adapter capabilities, including D3D12 and DXR support where available.
- A native Direct3D 12 scene pipeline with hardware adapter selection, WARP fallback, flip-model swap chain, per-frame command allocators, root signature, compiled shaders, vertex/index buffers, constant buffer, depth buffer, indexed terrain/forest scene rendering, resize handling, and fence synchronization. The first D3D12 milestone currently uses vertex colors and directional lighting; the D3D11 renderer retains the more advanced PBR, cascaded shadows, HDR, AO, bloom and TAA pipeline.
- A small sample house asset under `assets/models/` so the OBJ importer and texture path are exercised on startup. If `stone_house.glb` or `stone_house.gltf` is placed in that directory, the engine prefers it over the OBJ sample.

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
- renderer: Direct3D 12 scene pipeline (default) plus Direct3D 11 compatibility renderer; shaders, buffers, depth and temporal-history targets
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

No third-party runtime dependencies: the engine uses Win32, Direct3D 11/12, DXGI, and the Windows SDK shader compiler.

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

- Windows backend only. The D3D12 renderer now draws the procedural valley with depth testing, directional lighting and distance fog. Advanced PBR materials, imported asset rendering, cascaded shadows, HDR post-processing, AO, bloom and TAA are not yet ported from the D3D11 compatibility renderer.
- The procedural valley now includes denser ground clutter, tree roots, bark seams, road gravel, ferns, leaf litter, twigs, and layered world-space material variation. It remains a handcrafted prototype, not photorealistic scanned assets: geometry silhouettes and most environment materials still need authored models and complete PBR texture sets. Terrain and environment geometry are generated once at startup; there is no streaming, LOD system, foliage animation, or baked lighting yet.
- The active scene renderer is Direct3D 11 and uses one directional light. Screen-space contact occlusion and analytic environment lighting are approximations; neither replaces a production SSAO/GTAO implementation or image-based lighting from prefiltered HDR environment maps. TAA uses camera reprojection but lacks per-object motion vectors and robust disocclusion history rejection, so fast-moving objects may ghost. The environment's grass, stone and timber texture tiles are synthesized deterministically at startup; they are not artist-authored scanned materials.
- The D3D12 renderer does not yet use DXR or path tracing, and its first scene pipeline uses a single merged upload-heap mesh with vertex colors rather than the full D3D11 PBR material system. Production-quality asset streaming, default-heap geometry uploads, cascaded shadow passes, HDR post-processing, motion vectors, and render-graph/resource lifetime management remain future work. DXR additionally needs acceleration structures and ray-generation/miss/hit shaders.
- NVIDIA DLSS is **not integrated yet**. DLSS needs NVIDIA Streamline (or the supported DLSS SDK path), motion vectors, depth, render-resolution management, and a compatible D3D12 render pipeline. Capability detection alone does not enable DLSS.
- No mouse-look, audio, physics, scripting, or game packaging yet.

## Roadmap

1. Native runtime and automated workflow. Implemented.
2. 3D math foundation. Implemented.
3. Perspective camera, depth buffer, and transform-aware indexed mesh rendering. Implemented.
4. Keyboard input and controllable free-fly camera. Implemented.
5. OBJ mesh import and textured diffuse materials. Implemented as an initial pipeline.
6. glTF 2.0 import, multi-material meshes, authored asset packs, mip-chain generation, and environment IBL.
7. Direct3D 12 scene pipeline implemented as the default backend; port the advanced PBR materials, cascaded shadows, HDR post-processing and temporal AA, then add DXR acceleration structures, ray-traced shadows/reflections, and a path-tracing test scene.
8. NVIDIA Streamline/DLSS integration after temporal jitter, motion vectors, depth, and render-resolution control are available.
9. PCF directional shadow mapping. Implemented as an initial single-map pass; cascades, contact hardening, and temporal stabilization remain future work.
10. Initial depth-based contact occlusion and temporal anti-aliasing implemented; improve AO, motion vectors, disocclusion handling, and temporal stability.
11. Replace procedural placeholder assets with authored glTF environment kits and full PBR texture sets; add mouse-look, collision, and playable character movement.
