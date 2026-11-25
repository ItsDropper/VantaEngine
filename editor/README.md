# VantaEngine Editor

The editor is a native Rust desktop application built with Iced and its `wgpu`
renderer. It does not embed a browser, Chromium runtime, Python UI toolkit, or
web frontend. The Rust application owns the window, workspace state, scene
authoring, project navigation and asset inspection.

## Run from source

Install a stable Rust toolchain and Python 3.12+.

```powershell
cargo run --release --manifest-path editor/app/Cargo.toml
```

## Automated screenshot capture

The editor opens a project passed in the `VANTA_EDITOR_PROJECT` environment
variable on startup. The Windows CI workflow uses this to open a real starter
project, generate its assets, launch the compiled desktop application, and
capture the visible editor window. The resulting PNG is published as the
`vantaengine-editor-screenshot` workflow artifact.

## Portable Windows build

The Windows CI workflow creates
`VantaEngine-Editor-Windows-x64.zip`. Extract it and run `vanta-editor.exe`.
The package contains the native editor, its Python authoring SDK and this
README. Python 3.12+ must be installed on the machine. No repository checkout
or separate SDK installation is required.

For a local executable build:

```powershell
cargo build --release --manifest-path editor/app/Cargo.toml
```

The executable is written to `editor/app/target/release/vanta-editor.exe`.

## Implemented desktop workflows

- Native project create/open dialogs and validated `vanta.project.json`
  manifests.
- Recent-project history, accent selection and compact-layout preference,
  persisted locally in the operating system's application configuration
  directory.
- Bounded project file browser with system-editor file opening.
- Scene hierarchy with create, select, rename, duplicate and delete actions.
- Versioned `*.vanta-scene.json` scene documents with entity IDs and
  position/rotation/scale transforms. Scene edits are saved to disk and can
  be reopened independently of the editor.
- Asset library populated from generated `.vanta-model.json` metadata,
  including geometry counts and exported-file inventory.
- Background asset builds, build logs, error reporting, a five-minute build
  timeout and asset refresh.
- No telemetry or account connection.

The Python SDK is bundled in the portable package. Set `VANTA_PYTHON` to
choose a Python executable or `VANTA_SDK_PATH` to override the bundled SDK
location.

## Renderer integration status

The scene workspace currently provides a structured scene-data preview, not a
live 3D viewport. The runtime is composed around a Win32 D3D11/D3D12 window,
and the renderer does not yet expose a public offscreen viewport surface. A
production viewport must be built on an explicit renderer-owned surface/frame
contract; editor panels must never own D3D12 devices or include private backend
headers. Camera and light entity kinds are authorable in scene documents, but
their runtime component contracts are not yet connected.

## Visual system

The editor uses translucent, bordered surfaces over a dark workspace. On Windows 11 (build 22621 or newer), a small native Win32 integration asks Desktop Window Manager for the system Mica backdrop and rounded window corners; unsupported systems fall back to the same dark in-app surface styling. The native effect is compositor-owned rather than a simulated blur shader. Linux and macOS currently use their standard window compositor with the in-app translucent surfaces.

## UI technology

OneClient uses PolyCompose, a Kotlin/Jetpack Compose UI library designed for
Minecraft screens. That framework is not a Rust desktop dependency. Iced is
the Rust-native choice: declarative view composition, explicit state/message
updates, native windowing, and a wgpu renderer.
