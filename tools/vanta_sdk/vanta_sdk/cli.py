"""Command-line project builder."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import runpy
import sys
from .exporter import export_obj
from .model import Model

def _load_project(project_dir: Path):
    root = project_dir.resolve()
    manifest_path = root / "vanta.project.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read project manifest {manifest_path}: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("vanta.project.json must declare schema_version 1")
    if not isinstance(manifest.get("name"), str) or not manifest["name"].strip():
        raise ValueError("project manifest requires a non-empty name")
    entrypoint = manifest.get("entrypoint")
    if not isinstance(entrypoint, str) or not entrypoint:
        raise ValueError("project manifest requires a Python entrypoint")
    script = (root / entrypoint).resolve()
    if not script.is_relative_to(root):
        raise ValueError("project entrypoint must stay inside the project directory")
    if script.suffix.lower() != ".py" or not script.is_file():
        raise ValueError("project entrypoint must be an existing Python file")
    return manifest, script

def build_project(project_dir: Path, output_dir: Path) -> dict:
    manifest, script = _load_project(project_dir)
    namespace = runpy.run_path(str(script), run_name="__vanta_project__")
    build = namespace.get("build")
    if not callable(build):
        raise ValueError("project entrypoint must define build()")
    model = build()
    if not isinstance(model, Model):
        raise TypeError("project build() must return vanta_sdk.Model")
    model.name = manifest["name"]
    return export_obj(model, output_dir, script)

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="vanta", description="Build Python-authored VantaEngine model projects.")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="export OBJ/MTL assets from a project entrypoint")
    build.add_argument("--project", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = build_project(args.project, args.output)
    except (OSError, ValueError, TypeError, RuntimeError) as exc:
        print(f"vanta: error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0
