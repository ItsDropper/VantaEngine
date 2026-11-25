# VantaEngine Editor

The editor is a native Rust desktop application built with Iced and its `wgpu`
renderer. It does not embed a browser, Chromium runtime, Python UI toolkit, or
web frontend. Iced uses a declarative state/message/view architecture and
renders through the platform graphics backend.

## Build and launch

Install a stable Rust toolchain and Python 3.12+.

From the repository root:

```powershell
cargo run --release --manifest-path editor/app/Cargo.toml
```

Build a distributable executable:

```powershell
cargo build --release --manifest-path editor/app/Cargo.toml
```

The executable is written to `editor/app/target/release/vanta-editor.exe`
on Windows.

## Implemented functionality

- Native window and GPU-rendered UI with a dark Vanta theme.
- Native folder dialogs to create and open projects.
- Project manifest validation and canonicalized entrypoint containment.
- Bounded project file browser with system-editor file opening.
- Asset library populated from generated `.vanta-model.json` metadata.
- Geometry statistics and exported-file inventory.
- Background asset builds that do not block UI event processing.
- Build output, failure reporting, and asset refresh after successful builds.

The UI and project workspace are Rust. The existing Python SDK remains the
asset-authoring/build worker. Set `VANTA_PYTHON` to choose a Python executable
or `VANTA_SDK_PATH` to point to an installed SDK package directory.

## Renderer integration status

The current editor does not yet embed a live engine viewport. The runtime is
currently composed around a Win32 D3D11/D3D12 window, and the renderer does not
yet expose a public offscreen viewport surface. The viewport milestone must
introduce an explicit renderer-owned surface/frame contract; editor panels
must never own D3D12 devices or include private backend headers.

## UI technology choice

OneClient uses PolyCompose, a Kotlin/Jetpack Compose UI library designed for
Minecraft screens. That framework is not a Rust desktop dependency. Iced is
the Rust-native choice: declarative view composition, explicit state/message
updates, native windowing, and a wgpu renderer.
