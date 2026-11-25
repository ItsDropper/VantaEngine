# VantaEngine Editor Boundary

The editor is a separate native Rust application, not a mode compiled into the
runtime and not a dependency of game builds. It consumes the public Python
asset SDK as a build worker; its window, UI, project state, and asset browser
are Rust-owned.

## Ownership

| Area | Owns | Must not own |
| --- | --- | --- |
| Engine Core (`VantaEngine::Core`) | Scene/entity state, transforms, physics, input action maps, CPU assets, procedural-world data and deterministic runtime services | Window creation, graphics devices, GPU allocations, editor selection or panels |
| Asset Pipeline (`VantaEngine::AssetPipeline`) | Texture processing and Windows-backed image/file adapters | Graphics devices, GPU resource ownership or editor state |
| GPU / Renderer (`VantaEngine::Renderer`) | D3D11/D3D12 devices, command submission, GPU resources, shaders, render passes, scene upload and presentation | Gameplay rules, persistent editor state, project UX or scene-authoring policy |
| Runtime (`VantaEngine::Runtime`) | Current Win32 application composition and frame loop | Editor-only dependencies or editor state |
| Editor (`editor/app`) | Project/workspace lifecycle, file and asset browsers, build orchestration, scene hierarchy, inspectors, viewport interaction, gizmos and play/stop UX | Private D3D backend types, direct GPU resource ownership, duplicate scene/entity model |

## Dependency direction

- Core is the lowest-level runtime target and must remain free of Win32 and D3D
  headers/link libraries.
- Asset Pipeline may depend on Core for CPU asset types and provides platform
  image/file adapters without pulling graphics APIs into Core.
- Renderer may depend on Core and Asset Pipeline, and translates immutable
  scene/frame data into backend resources and GPU work.
- Runtime composes Core, platform services and a renderer for the sample app.
- The editor is a separate Rust executable. It currently consumes project
  manifests and the Python asset SDK through a subprocess. A future native
  bridge must use a versioned public API, never private D3D implementation
  details or undocumented renderer globals.
- Editor project files and serialized scene data must be usable by a packaged
  game without the editor installed.

## Workstream boundaries

### Engine Core
1. Stabilize scene/entity/component ownership and generation-safe handles.
2. Define serialization and versioned project/scene asset contracts.
3. Complete runtime resource lifetime, asynchronous asset loading and job
   scheduling without graphics API dependencies.
4. Keep deterministic tests runnable without a window or GPU.

### GPU / Rendering
1. Separate backend-independent render data from D3D11/D3D12 implementation.
2. Introduce explicit frame/resource lifetime and synchronization contracts.
3. Build a render graph only when pass dependencies, resource transitions and
   transient allocation can be represented and validated end to end.
4. Add GPU timing, validation and device-loss recovery around real submissions.
5. Expose an editor viewport through a renderer-owned offscreen surface and
   explicit frame contract; the editor must not own a D3D device.

### Editor
1. Establish project creation/open/save and safe asset-path handling.
2. Implement scene hierarchy and inspector against Core APIs.
3. Add a viewport through the renderer's public interface, then selection,
   transform gizmos and play/stop isolation.
4. Add asset import/inspection and build/package workflows only after project
   and serialization contracts are stable.

A workstream may not bypass another by reaching into its private implementation.
Cross-workstream changes should extend the public contract first and test both
sides of the boundary.
