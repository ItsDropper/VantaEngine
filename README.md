# VantaEngine

VantaEngine is an early-stage, native C++23 3D game engine. It is designed around a small dependency footprint, clear subsystem boundaries, and a build/test workflow that does not require a heavyweight editor.

> **Current milestone:** native Win32 window, Direct3D 11 device and swap chain, runtime-compiled shaders, a GPU-rendered colored triangle, resize handling, a frame loop, and automated core tests. This is the rendering foundation—not yet a complete game engine.

## Design goals

- Native executable; no browser runtime or mandatory editor.
- Keep platform, renderer, core runtime, and game code separate.
- Make ownership explicit and use RAII for graphics resources.
- Keep the default development loop fast.
- Test platform-independent logic without opening a window or requiring a GPU.
- Add capabilities incrementally: input/actions, math, scene/entity model, resources, materials, meshes, camera, lighting, and editor tooling.

## Current architecture

```text
engine/
├── include/vanta/
│   ├── core/
│   │   ├── Engine.hpp        # Runtime entry point
│   │   ├── FrameTime.hpp     # Frame-delta safety utility
│   │   └── Log.hpp           # Minimal logging
│   ├── platform/
│   │   └── Window.hpp        # Native window interface
│   └── renderer/
│       └── D3D11Renderer.hpp # Renderer interface/resources
└── src/
    ├── core/Engine.cpp       # Startup, frame loop, shutdown
    ├── platform/Window.cpp   # Win32 event and window lifecycle
    ├── renderer/D3D11Renderer.cpp
    └── main.cpp
tests/
└── engine_tests.cpp          # Fast, GPU-independent unit tests
```

### Runtime flow

1. Create a native Win32 window.
2. Initialize the Direct3D 11 device and swap chain.
3. Compile the initial HLSL vertex/pixel shaders.
4. Enter the frame loop: pump window messages, handle resize, compute a bounded delta time, render, and present.
5. Release Direct3D resources through COM smart pointers when the renderer is destroyed.

The initial renderer is deliberately small. It proves that the engine can create a graphics device and submit GPU work before introducing a larger rendering abstraction.

## Requirements

- Windows 10/11 x64.
- Visual Studio with the MSVC x64 C++ toolchain.
- Visual Studio's CMake and Ninja components (or CMake 3.25+ and Ninja available on PATH).
- PowerShell 5.1+.
- VS Code is optional; the command-line build works without it.

There are no third-party runtime dependencies. The renderer uses the Windows SDK's Direct3D 11, DXGI, and shader compiler libraries.

## Build and run

From the repository root in PowerShell:

```powershell
git pull origin main
.\scripts\build.ps1 -Test -Run
```

The first command updates your local checkout. The script configures CMake, builds the engine and test executable, runs CTest, then launches the engine. A VantaEngine window should appear with a colored triangle on a dark background. Press **Escape** or close the window to exit.

Build only:

```powershell
.\scripts\build.ps1
```

Release build:

```powershell
.\scripts\build.ps1 -Configuration Release
```

Build and run automated tests:

```powershell
.\scripts\build.ps1 -Test
```

If the CMake generator or toolchain changes, regenerate from a clean build directory:

```powershell
.\scripts\build.ps1 -Clean -Test
```

The clean option deletes only the generated `build/` directory.

## Tests and CI

Tests use CTest and do not create a window or need a GPU:

- `VantaEngine.Startup` — verifies the executable's self-test entry point.
- `VantaEngine.UnitTests` — checks frame-delta clamping behavior.

GitHub Actions builds and tests both Debug and Release on Windows for pushes, pull requests, and manual runs.

## VS Code workflow

- **Ctrl+Shift+B** — build Debug.
- **Run Task → VantaEngine: Test** — build and run all tests.
- **F5** — build and launch under the MSVC debugger (requires Microsoft's C/C++ extension).

## Current limitations

- Windows-only backend for now.
- First renderer milestone: one GPU-rendered triangle, no depth buffer or 3D scene graph yet.
- No asset importer, material system, camera, audio, physics, scripting, or editor yet.
- The main loop computes delta time, but a simulation/update subsystem has not been introduced yet.

## Roadmap

1. **Native runtime** — window, renderer, frame loop, logging, tests. *(Current milestone.)*
2. **Engine core** — input state, fixed/variable update policy, time, math library.
3. **3D scene** — camera, transforms, mesh resources, depth testing, indexed geometry.
4. **Assets** — texture/material loading, model import, shader management and cache.
5. **Gameplay** — entity/component model, game module boundary, serialization.
6. **Tools** — diagnostics, profiler, in-engine debug UI, editor only when the runtime is solid.
7. **Quality** — graphics-device recovery, resource lifetime validation, performance tests, packaged sample game.

The roadmap is ordered to keep the engine usable at every stage instead of building a large editor before the runtime works.
