# VantaEngine Editor

The first usable editor application is a standalone Python/Tk desktop workspace.
It is intentionally a client of the project manifest and Python authoring SDK;
it does not depend on private renderer implementation details.

## Launch

From the repository root:

```powershell
python editor/vanta_editor.py
```

Open an existing project or create a new one. The workspace provides:

- Project creation and manifest validation.
- Project file browsing; double-click a file to open it in the system editor.
- A one-click Python asset build using the repository SDK.
- Generated model inventory with mesh, vertex, triangle and material counts.
- Build output and per-asset metadata inspection.

The editor uses Python's standard-library Tk interface. No third-party Python
packages are required. Python must be installed with Tk support.

## Authoring model assets

Edit the project's `generate.py` entrypoint. Its `build()` function returns
a validated `vanta_sdk.Model`. The editor invokes the same command used in CI:

```powershell
$env:PYTHONPATH = "tools/vanta_sdk"
python -m vanta_sdk build --project projects/starter --output build/generated
```

Generated OBJ/MTL files are consumed by the engine's native importer. The
editor is a project/asset workbench, not yet a live in-engine viewport or
visual node editor; those require the renderer's public viewport integration.
