# Python-authored models

VantaEngine projects can generate importable model assets with a dependency-free
Python SDK. A project contains `vanta.project.json` and a Python entrypoint
that defines `build()` and returns a `vanta_sdk.Model`.

## Build

From the repository root:

```powershell
$env:PYTHONPATH = "tools/vanta_sdk"
python -m vanta_sdk build --project projects/starter --output build/generated
```

The SDK validates geometry and materials, then writes deterministic outputs:

- `starter_cabin.obj`: geometry consumed by the existing native OBJ importer.
- `starter_cabin.mtl`: material factors using the engine's `Kd`, `Pr`, and `Pm` support.
- `starter_cabin.vanta-model.json`: geometry counts and SHA-256 hashes.

The API currently provides `Material`, `Model.add_mesh()`, `Model.add_box()`,
and `Model.add_cylinder()`. The renderer remains responsible for GPU upload.

## CI and AI-assisted authoring

Windows CI tests the SDK, executes the starter project, and imports that exact
generated OBJ through the C++ model importer before uploading the model files
as the `vantaengine-generated-models` artifact.

An AI coding assistant can write or edit `generate.py` using this SDK and submit
the change for CI. Review the script diff and generated artifact before merging.
The script is ordinary Python, not a sandbox: only execute projects you trust.
The workflow has read-only repository permissions and supplies no deployment
credentials or application secrets to project scripts.
