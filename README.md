# VantaEngine

VantaEngine is a lightweight, native C++ game-engine project focused on fast iteration, explicit control over dependencies, and a clean development workflow.

> **Status:** early foundation. The current executable verifies the build and test pipeline; graphics, windowing, and asset systems have not been implemented yet.

## Principles

- Native C++; no embedded browser runtime.
- Keep dependencies deliberate and minimal.
- Reproducible CMake builds.
- Warnings enabled from the beginning.
- Automated smoke test and Windows CI.
- VS Code build, test, and debugging tasks.
- Add renderer and asset systems incrementally rather than building a giant editor first.

## Requirements

- Windows 10/11 x64.
- Visual Studio C++ Build Tools / Visual Studio with the MSVC x64 toolchain.
- CMake 3.25 or newer.
- Ninja (used by the included PowerShell build script).
- PowerShell 5.1 or newer.
- VS Code. The Microsoft C/C++ extension is recommended for F5 debugging.

The build script looks for CMake and Ninja bundled with Visual Studio before using PATH, so separate global installations are not required when those bundled tools are present.

## Build

Open the repository folder in VS Code, then run this in its PowerShell terminal:

```powershell
.\scripts\build.ps1
```

Build Release instead:

```powershell
.\scripts\build.ps1 -Configuration Release
```

Run the startup smoke test:

```powershell
.\scripts\build.ps1 -Test
```

Build and launch the executable:

```powershell
.\scripts\build.ps1 -Run
```

The executable is written to `build/bin/VantaEngine.exe` for the included Ninja workflow.

## VS Code workflow

- **Ctrl+Shift+B** — build Debug.
- **Run Task → VantaEngine: Test** — build and run the smoke test.
- **F5** — build, then launch under the MSVC debugger (requires the Microsoft C/C++ VS Code extension).

The tasks invoke the repository's build script, which initializes the MSVC environment for the current terminal session without permanently changing system environment variables.

## Project layout

```text
VantaEngine/
├── .github/workflows/ci.yml
├── .vscode/
│   ├── launch.json
│   ├── settings.json
│   └── tasks.json
├── assets/
│   ├── models/
│   ├── shaders/
│   └── textures/
├── engine/
│   ├── include/vanta/
│   ├── src/main.cpp
│   └── CMakeLists.txt
├── game/
├── scripts/build.ps1
├── tests/
├── .gitignore
├── CMakeLists.txt
└── README.md
```

## Current scope

This initial milestone intentionally does not pull in a graphics API or third-party library. First, keep the compiler, build, test, and debugger workflow reliable. Windowing and rendering dependencies can be evaluated when the first graphics milestone is ready.
