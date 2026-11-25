"""VantaEngine Editor: project, asset, and build workspace.

The editor deliberately authors through the public Python SDK and project manifest.
It never reaches into renderer-private state. Run with: python editor/vanta_editor.py
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import traceback
from dataclasses import dataclass


@dataclass(frozen=True)
class Project:
    root: Path
    name: str
    entrypoint: Path
    output: Path

    @classmethod
    def open(cls, directory: Path) -> "Project":
        root = directory.expanduser().resolve()
        manifest_path = root / "vanta.project.json"
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise ValueError(f"Cannot open project manifest: {exc}") from exc
        if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
            raise ValueError("Unsupported project manifest; expected schema_version 1.")
        name = manifest.get("name")
        entry = manifest.get("entrypoint")
        if not isinstance(name, str) or not name.strip():
            raise ValueError("Project name must be a non-empty string.")
        if not isinstance(entry, str) or not entry.strip():
            raise ValueError("Project entrypoint must be a relative Python file.")
        entrypoint = (root / entry).resolve()
        if not entrypoint.is_relative_to(root) or entrypoint.suffix.lower() != ".py":
            raise ValueError("Project entrypoint must be a Python file inside the project.")
        if not entrypoint.is_file():
            raise ValueError(f"Project entrypoint does not exist: {entrypoint}")
        return cls(root, name, entrypoint, root / "build" / "editor-assets")


def create_project(directory: Path, name: str | None = None) -> Project:
    root = directory.expanduser().resolve()
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"Destination is not empty: {root}")
    root.mkdir(parents=True, exist_ok=True)
    project_name = name or root.name
    safe_name = "".join(ch if ch.isalnum() or ch in "_-" else "_" for ch in project_name)
    if not safe_name or not (safe_name[0].isalpha() or safe_name[0] == "_"):
        safe_name = "VantaProject"
    manifest = {"schema_version": 1, "name": safe_name, "entrypoint": "generate.py"}
    script = '''"""Project entrypoint. Edit build() to author your model with the Vanta SDK."""
from vanta_sdk import Material, Model


def build() -> Model:
    model = Model("my_asset")
    model.add_material(Material("stone", (0.42, 0.45, 0.48), 0.82, 0.0))
    model.add_box("foundation", (0.0, 0.5, 0.0), (3.0, 1.0, 3.0), "stone")
    model.add_box("upper_block", (0.0, 2.0, 0.0), (2.0, 2.0, 2.0), "stone")
    return model
'''
    (root / "vanta.project.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (root / "generate.py").write_text(script, encoding="utf-8")
    return Project.open(root)


def discover_assets(project: Project) -> list[dict]:
    assets = []
    if not project.output.is_dir():
        return assets
    for path in sorted(project.output.glob("*.vanta-model.json")):
        try:
            metadata = json.loads(path.read_text(encoding="utf-8"))
            geometry = metadata.get("geometry", {})
            assets.append({
                "name": metadata.get("model", path.stem),
                "metadata": path,
                "mesh_count": geometry.get("mesh_count", 0),
                "vertices": geometry.get("vertex_count", 0),
                "triangles": geometry.get("triangle_count", 0),
                "materials": geometry.get("material_count", 0),
            })
        except (OSError, json.JSONDecodeError, AttributeError):
            assets.append({"name": path.stem, "metadata": path, "error": "Invalid metadata"})
    return assets


def self_test() -> int:
    import tempfile
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "sample"
        project = create_project(root, "Editor_Smoke_Test")
        assert project.name == "Editor_Smoke_Test"
        assert project.entrypoint.is_file()
        reopened = Project.open(root)
        assert reopened.output == root / "build" / "editor-assets"
        (root / "vanta.project.json").write_text(
            json.dumps({"schema_version": 1, "name": "bad", "entrypoint": "../escape.py"}),
            encoding="utf-8")
        try:
            Project.open(root)
        except ValueError:
            pass
        else:
            raise AssertionError("project entrypoint path traversal was accepted")
    print("VantaEngine Editor project-contract self-test passed.")
    return 0


def launch_editor(initial_project: Path | None = None) -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError as exc:
        print(f"VantaEngine Editor requires Python Tk support: {exc}", file=sys.stderr)
        return 2

    class Editor(tk.Tk):
        def __init__(self) -> None:
            super().__init__()
            self.title("VantaEngine Editor")
            self.geometry("1180x760")
            self.minsize(900, 580)
            self.project: Project | None = None
            self._build_ui()
            if initial_project:
                self.open_project(initial_project)

        def _build_ui(self) -> None:
            self.configure(background="#171b22")
            style = ttk.Style(self)
            style.theme_use("clam")
            style.configure(".", background="#202630", foreground="#e7edf5", fieldbackground="#111820")
            style.configure("TFrame", background="#202630")
            style.configure("TLabel", background="#202630", foreground="#dce5f0")
            style.configure("Title.TLabel", font=("Segoe UI", 16, "bold"), foreground="#ffffff")
            style.configure("Muted.TLabel", foreground="#9eacbd")
            style.configure("TButton", padding=(12, 7), background="#303c4c", foreground="#f3f7fb")
            style.map("TButton", background=[("active", "#40536b")])
            style.configure("Treeview", background="#151b23", foreground="#e2e8f0",
                             fieldbackground="#151b23", rowheight=26, borderwidth=0)
            style.configure("Treeview.Heading", background="#283342", foreground="#dce5f0")
            header = ttk.Frame(self, padding=(18, 14))
            header.pack(fill="x")
            ttk.Label(header, text="VANTAENGINE", style="Muted.TLabel").pack(anchor="w")
            ttk.Label(header, text="Project Editor", style="Title.TLabel").pack(anchor="w", pady=(3, 10))
            actions = ttk.Frame(header)
            actions.pack(fill="x")
            ttk.Button(actions, text="New Project", command=self.new_project).pack(side="left", padx=(0, 8))
            ttk.Button(actions, text="Open Project", command=self.choose_project).pack(side="left", padx=8)
            ttk.Button(actions, text="Open Script", command=self.open_script).pack(side="left", padx=8)
            ttk.Button(actions, text="Build Assets", command=self.build_assets).pack(side="left", padx=8)
            ttk.Button(actions, text="Refresh", command=self.refresh_assets).pack(side="left", padx=8)
            self.project_label = ttk.Label(header, text="No project open", style="Muted.TLabel")
            self.project_label.pack(anchor="w", pady=(12, 0))

            body = ttk.Panedwindow(self, orient="horizontal")
            body.pack(fill="both", expand=True, padx=18, pady=(0, 12))
            left = ttk.Frame(body, padding=10)
            right = ttk.Frame(body, padding=10)
            body.add(left, weight=1)
            body.add(right, weight=3)
            ttk.Label(left, text="PROJECT FILES", style="Muted.TLabel").pack(anchor="w", pady=(0, 8))
            self.files = ttk.Treeview(left, show="tree", selectmode="browse")
            self.files.pack(fill="both", expand=True)
            self.files.bind("<Double-1>", self._open_selected_file)
            ttk.Label(right, text="ASSET LIBRARY", style="Muted.TLabel").pack(anchor="w", pady=(0, 8))
            columns = ("meshes", "vertices", "triangles", "materials")
            self.assets = ttk.Treeview(right, columns=columns, show="headings", selectmode="browse")
            self.assets.heading("meshes", text="Meshes")
            self.assets.heading("vertices", text="Vertices")
            self.assets.heading("triangles", text="Triangles")
            self.assets.heading("materials", text="Materials")
            self.assets.column("meshes", width=85, anchor="center")
            self.assets.column("vertices", width=100, anchor="e")
            self.assets.column("triangles", width=100, anchor="e")
            self.assets.column("materials", width=100, anchor="center")
            self.assets.pack(fill="both", expand=True)
            self.assets.bind("<<TreeviewSelect>>", self._show_asset)
            self.asset_detail = ttk.Label(right, text="Build a project to inspect generated model assets.",
                                           style="Muted.TLabel")
            self.asset_detail.pack(fill="x", anchor="w", pady=10)
            ttk.Label(right, text="BUILD OUTPUT", style="Muted.TLabel").pack(anchor="w", pady=(10, 6))
            self.log = tk.Text(right, height=10, background="#10151c", foreground="#cbd5e1",
                               insertbackground="white", relief="flat", wrap="word")
            self.log.pack(fill="both", expand=True)
            self.status = ttk.Label(self, text="Ready", anchor="w", padding=(18, 8), style="Muted.TLabel")
            self.status.pack(fill="x")

        def write_log(self, value: str) -> None:
            self.log.insert("end", value.rstrip() + "\n")
            self.log.see("end")

        def choose_project(self) -> None:
            chosen = filedialog.askdirectory(title="Open VantaEngine project")
            if chosen:
                self.open_project(Path(chosen))

        def open_project(self, path: Path) -> None:
            try:
                self.project = Project.open(path)
            except (OSError, ValueError) as exc:
                messagebox.showerror("Cannot open project", str(exc))
                return
            self.project_label.configure(text=f"{self.project.name}  ·  {self.project.root}")
            self.status.configure(text="Project opened")
            self.refresh_files()
            self.refresh_assets()

        def new_project(self) -> None:
            chosen = filedialog.askdirectory(title="Choose an empty project directory")
            if not chosen:
                return
            try:
                project = create_project(Path(chosen))
            except (OSError, ValueError) as exc:
                messagebox.showerror("Cannot create project", str(exc))
                return
            self.open_project(project.root)
            self.write_log(f"Created project at {project.root}")

        def refresh_files(self) -> None:
            self.files.delete(*self.files.get_children())
            if not self.project:
                return
            root_id = self.files.insert("", "end", text=self.project.root.name, open=True, values=(str(self.project.root),))
            for path in sorted(self.project.root.rglob("*")):
                if any(part in {".git", "__pycache__"} for part in path.parts) or "build" in path.relative_to(self.project.root).parts:
                    continue
                relative = path.relative_to(self.project.root)
                parent = root_id
                for part in relative.parts[:-1]:
                    found = next((item for item in self.files.get_children(parent)
                                  if self.files.item(item, "text") == part), None)
                    parent = found or self.files.insert(parent, "end", text=part, open=True)
                self.files.insert(parent, "end", text=relative.name, values=(str(path),))

        def refresh_assets(self) -> None:
            self.assets.delete(*self.assets.get_children())
            if not self.project:
                return
            for item in discover_assets(self.project):
                self.assets.insert("", "end", iid=str(item["metadata"]),
                                   values=(item.get("mesh_count", 0), item.get("vertices", 0),
                                           item.get("triangles", 0), item.get("materials", 0)),
                                   text=item["name"])
            self.status.configure(text=f"{len(discover_assets(self.project))} generated asset(s)")

        def open_script(self) -> None:
            if not self.project:
                messagebox.showinfo("No project", "Open or create a project first.")
                return
            self._open_path(self.project.entrypoint)

        def _open_selected_file(self, _event=None) -> None:
            selected = self.files.selection()
            if not selected:
                return
            values = self.files.item(selected[0], "values")
            if values and values[0]:
                path = Path(values[0])
                if path.is_file():
                    self._open_path(path)

        def _open_path(self, path: Path) -> None:
            try:
                if os.name == "nt":
                    os.startfile(path)  # type: ignore[attr-defined]
                elif sys.platform == "darwin":
                    subprocess.Popen(["open", str(path)])
                else:
                    subprocess.Popen(["xdg-open", str(path)])
            except OSError as exc:
                messagebox.showerror("Cannot open file", str(exc))

        def _show_asset(self, _event=None) -> None:
            selected = self.assets.selection()
            if not selected:
                return
            path = Path(selected[0])
            try:
                metadata = json.loads(path.read_text(encoding="utf-8"))
                files = ", ".join(metadata.get("files", {}).keys())
                self.asset_detail.configure(text=f"{metadata.get('model')}  ·  {files}")
            except (OSError, json.JSONDecodeError):
                self.asset_detail.configure(text="Could not read asset metadata.")

        def build_assets(self) -> None:
            if not self.project:
                messagebox.showinfo("No project", "Open or create a project first.")
                return
            self.status.configure(text="Building assets…")
            self.write_log(f"> Building {self.project.name}")
            self.update_idletasks()
            env = os.environ.copy()
            sdk_root = Path(__file__).resolve().parents[1] / "tools" / "vanta_sdk"
            env["PYTHONPATH"] = str(sdk_root) + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
            command = [sys.executable, "-m", "vanta_sdk", "build",
                       "--project", str(self.project.root), "--output", str(self.project.output)]
            try:
                result = subprocess.run(command, cwd=self.project.root, env=env,
                                        capture_output=True, text=True, timeout=180)
            except (OSError, subprocess.TimeoutExpired) as exc:
                self.write_log(f"Build could not run: {exc}")
                self.status.configure(text="Build failed")
                return
            self.write_log(result.stdout)
            if result.stderr:
                self.write_log(result.stderr)
            if result.returncode == 0:
                self.write_log("Build completed.")
                self.status.configure(text="Build succeeded")
                self.refresh_assets()
            else:
                self.write_log(f"Build failed with exit code {result.returncode}.")
                self.status.configure(text="Build failed")

    app = Editor()
    app.mainloop()
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="VantaEngine project and asset editor")
    parser.add_argument("--project", type=Path, help="open a project on startup")
    parser.add_argument("--new-project", type=Path, help="create a starter project and exit")
    parser.add_argument("--self-test", action="store_true", help="test project contracts without opening a window")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.new_project:
        try:
            project = create_project(args.new_project)
        except (OSError, ValueError) as exc:
            print(f"editor: {exc}", file=sys.stderr)
            return 1
        print(project.root)
        return 0
    return launch_editor(args.project)


if __name__ == "__main__":
    raise SystemExit(main())
