from __future__ import annotations
import json
from pathlib import Path
import tempfile
import unittest
from vanta_sdk import Material, Model
from vanta_sdk.cli import build_project
from vanta_sdk.exporter import export_obj

class ModelSdkTests(unittest.TestCase):
    def make_model(self):
        model = Model("sdk_test")
        model.add_material(Material("stone", (0.4, 0.45, 0.5), 0.9, 0.0))
        model.add_box("cube", (0, 0, 0), (2, 2, 2), "stone")
        model.add_cylinder("pillar", (0, 2, 0), 0.5, 3, "stone", 12)
        return model

    def test_export_is_deterministic_and_writes_metadata(self):
        model = self.make_model()
        with tempfile.TemporaryDirectory() as directory:
            first = export_obj(model, Path(directory))
            obj = (Path(directory)/"sdk_test.obj").read_bytes()
            second = export_obj(model, Path(directory))
            self.assertEqual(first, second)
            self.assertEqual(obj, (Path(directory)/"sdk_test.obj").read_bytes())
            self.assertEqual(first["geometry"]["mesh_count"], 2)
            self.assertEqual(first["geometry"]["triangle_count"], 60)
            self.assertTrue((Path(directory)/"sdk_test.mtl").is_file())
            self.assertTrue((Path(directory)/"sdk_test.vanta-model.json").is_file())

    def test_invalid_geometry_is_rejected(self):
        model = Model("bad_model")
        model.add_material(Material("default"))
        model.add_mesh("bad", [(0,0,0),(1,0,0),(0,1,0)], [(0,1,9)], "default")
        with self.assertRaisesRegex(ValueError, "out-of-range"):
            model.validate()

    def test_invalid_material_values_are_rejected(self):
        with self.assertRaises(ValueError):
            Material("bad", (2,0,0))

    def test_project_entrypoint_cannot_escape_project_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)/"project"
            root.mkdir()
            (root/"vanta.project.json").write_text(
                json.dumps({"schema_version":1,"name":"safe","entrypoint":"../outside.py"}),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "stay inside"):
                build_project(root, Path(directory)/"out")

if __name__ == "__main__":
    unittest.main()
