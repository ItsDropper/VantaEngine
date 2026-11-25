# Python-authored models

VantaEngine projects can generate importable model assets with a dependency-free
Python SDK. A project contains \`vanta.project.json\` and a Python entrypoint
that defines \`build()\` and returns a \`vanta_sdk.Model\`.

## Build

From the repository root:

\`\`\`powershell
$env:PYTHONPATH = "tools/vanta_sdk"
python -m vanta_sdk build --project projects/starter --output build/generated
\`\`\`

The SDK validates geometry and materials, then writes deterministic outputs:

- \`starter_cabin.obj\`: geometry consumed by the native OBJ importer.
- \`starter_cabin.mtl\`: material factors using the engine's \`Kd\`, \`Pr\`, and \`Pm\` support.
- \`starter_cabin.vanta-model.json\`: geometry counts and SHA-256 hashes.

The API currently provides \`Material\`, \`Model.add_mesh()\`, \`Model.add_box()\`,
and \`Model.add_cylinder()\`. The renderer remains responsible for GPU upload.

## Editor

Launch the standalone editor workspace:

\`\`\`powershell
python editor/vanta_editor.py
\`\`\`

It creates and opens projects, validates project manifests, browses project
files, launches the script in the system editor, builds assets through the
same SDK command as CI, and displays generated model metadata. See
[Editor documentation](../editor/README.md). It does not yet provide a live
renderer viewport or visual scene gizmos.

## GPT-assisted authoring and CI

An assistant can create or edit a project's \`generate.py\` using the public
SDK, then run the same deterministic build command as the editor. CI tests the
SDK and editor project contracts, executes the starter project, configures and
builds the native engine, runs CTest (including importing the exact generated
OBJ through the native importer), and publishes OBJ/MTL/metadata as the
\`vantaengine-generated-models\` artifact.

The editor and automation do not require an AI-specific engine mode. A future
GPT bridge should be a separate, opt-in process that emits project scripts or
structured authoring commands through this SDK, then asks the editor/build
service to validate and import the result. Do not expose arbitrary shell
execution or renderer internals as an AI tool.

Project entrypoints are ordinary Python and execute with the current user's
permissions. Only build projects from sources you trust. GitHub Actions uses
read-only repository permissions and provides no deployment credentials to
project scripts; this is not an OS-level sandbox.
