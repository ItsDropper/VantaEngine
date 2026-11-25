# VantaEngine Editor

The editor is a native Rust desktop application built with Iced and its `wgpu`
renderer. It does not embed a browser, Chromium runtime, Python UI toolkit, or
web frontend. The Rust application owns the window, workspace state, scene
authoring, project navigation and asset inspection.

## Build and launch

Install a stable Rust toolchain and Python 3.12+.

From the repository root:

```powershell
cargo run --release --manifest-path editor/app/Cargo.toml
```

Build a Windows executable:

```powershell
cargo build --release --manifest-path editor/app/Cargo.toml
```

The executable is written to `editor/app/target/release/vanta-editor.exe`.
The Windows CI workflow publishes this executable as a downloadable build
artifact for each successful run on `main`.

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
- Background asset builds, build logs, error reporting and asset refresh.
- No telemetry or account connection.

The UI and project workspace are Rust. The existing Python SDK remains the
asset-authoring/build worker. Set `VANTA_PYTHON` to choose a Python executable
or `VANTA_SDK_PATH` to point to an installed SDK package directory.

## Renderer integration status

The scene workspace currently provides a structured scene-data preview, not a
live 3D viewport. The runtime is composed around a Win32 D3D11/D3D12 window,
and the renderer does not yet expose a public offscreen viewport surface. A
production viewport must be built on an explicit renderer-owned surface/frame
contract; editor panels must never own D3D12 devices or include private backend
headers. Camera and light entity kinds are authorable in scene documents, but
their runtime component contracts are not yet connected.

## UI technology

OneClient uses PolyCompose, a Kotlin/Jetpack Compose UI library designed for
Minecraft screens. That framework is not a Rust desktop dependency. Iced is
the Rust-native choice: declarative view composition, explicit state/message
updates, native windowing, and a wgpu renderer.
