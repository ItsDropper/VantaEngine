"""Validated CPU-side model authoring primitives."""
from __future__ import annotations
from dataclasses import dataclass, field
import math
import re
from typing import Iterable

_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]{0,63}$")

def _name(value: str, kind: str) -> str:
    if not isinstance(value, str) or not _NAME.fullmatch(value):
        raise ValueError(f"{kind} names must match {_NAME.pattern}")
    return value

def _vector3(value: Iterable[float], label: str) -> tuple[float, float, float]:
    try:
        result = tuple(float(component) for component in value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{label} must contain three finite numbers") from exc
    if len(result) != 3 or not all(math.isfinite(component) for component in result):
        raise ValueError(f"{label} must contain three finite numbers")
    return result  # type: ignore[return-value]

@dataclass(frozen=True)
class Material:
    name: str
    base_color: tuple[float, float, float] = (0.8, 0.8, 0.8)
    roughness: float = 0.8
    metallic: float = 0.0

    def __post_init__(self) -> None:
        _name(self.name, "Material")
        color = _vector3(self.base_color, "base_color")
        if any(value < 0.0 or value > 1.0 for value in color):
            raise ValueError("base_color components must be between 0 and 1")
        if not math.isfinite(self.roughness) or not 0.0 <= self.roughness <= 1.0:
            raise ValueError("roughness must be finite and between 0 and 1")
        if not math.isfinite(self.metallic) or not 0.0 <= self.metallic <= 1.0:
            raise ValueError("metallic must be finite and between 0 and 1")

@dataclass
class Mesh:
    name: str
    material: str
    vertices: list[tuple[float, float, float]]
    faces: list[tuple[int, ...]]

@dataclass
class Model:
    name: str
    materials: dict[str, Material] = field(default_factory=dict)
    meshes: list[Mesh] = field(default_factory=list)

    def __post_init__(self) -> None:
        _name(self.name, "Model")

    def add_material(self, material: Material) -> Material:
        if not isinstance(material, Material):
            raise TypeError("material must be a Material")
        if material.name in self.materials:
            raise ValueError(f"duplicate material: {material.name}")
        self.materials[material.name] = material
        return material

    def add_mesh(self, name: str, vertices: Iterable[Iterable[float]],
                 faces: Iterable[Iterable[int]], material: str) -> Mesh:
        _name(name, "Mesh")
        if any(mesh.name == name for mesh in self.meshes):
            raise ValueError(f"duplicate mesh: {name}")
        if material not in self.materials:
            raise ValueError(f"unknown material: {material}")
        mesh = Mesh(name, material,
                    [_vector3(vertex, f"{name} vertex") for vertex in vertices],
                    [tuple(int(index) for index in face) for face in faces])
        self.meshes.append(mesh)
        return mesh

    def add_box(self, name: str, center: Iterable[float], size: Iterable[float],
                material: str, rotation: Iterable[float] = (0.0, 0.0, 0.0)) -> Mesh:
        cx, cy, cz = _vector3(center, "center")
        sx, sy, sz = _vector3(size, "size")
        rx, ry, rz = _vector3(rotation, "rotation")
        if min(sx, sy, sz) <= 0.0:
            raise ValueError("box dimensions must be positive")
        vertices = []
        for x, y, z in (
            (-sx/2, -sy/2, -sz/2), (sx/2, -sy/2, -sz/2),
            (sx/2, sy/2, -sz/2), (-sx/2, sy/2, -sz/2),
            (-sx/2, -sy/2, sz/2), (sx/2, -sy/2, sz/2),
            (sx/2, sy/2, sz/2), (-sx/2, sy/2, sz/2),
        ):
            x, y = x*math.cos(rx)-y*math.sin(rx), x*math.sin(rx)+y*math.cos(rx)
            y, z = y*math.cos(ry)-z*math.sin(ry), y*math.sin(ry)+z*math.cos(ry)
            x, z = x*math.cos(rz)-z*math.sin(rz), x*math.sin(rz)+z*math.cos(rz)
            vertices.append((cx+x, cy+y, cz+z))
        return self.add_mesh(name, vertices, (
            (0,3,2,1), (4,5,6,7), (0,1,5,4),
            (3,7,6,2), (0,4,7,3), (1,2,6,5),
        ), material)

    def add_cylinder(self, name: str, center: Iterable[float], radius: float,
                     height: float, material: str, segments: int = 16) -> Mesh:
        cx, cy, cz = _vector3(center, "center")
        if not math.isfinite(radius) or not math.isfinite(height) or radius <= 0 or height <= 0:
            raise ValueError("cylinder radius and height must be positive and finite")
        if not isinstance(segments, int) or not 3 <= segments <= 512:
            raise ValueError("cylinder segments must be an integer between 3 and 512")
        vertices = []
        for y in (cy-height/2, cy+height/2):
            for index in range(segments):
                angle = 2*math.pi*index/segments
                vertices.append((cx+math.cos(angle)*radius, y, cz+math.sin(angle)*radius))
        bottom, top = len(vertices), len(vertices)+1
        vertices.extend(((cx, cy-height/2, cz), (cx, cy+height/2, cz)))
        faces = []
        for index in range(segments):
            nxt = (index+1) % segments
            faces.extend(((index, nxt, segments+nxt, segments+index),
                          (bottom, nxt, index),
                          (top, segments+index, segments+nxt)))
        return self.add_mesh(name, vertices, faces, material)

    def validate(self) -> None:
        _name(self.name, "Model")
        if not self.materials or not self.meshes:
            raise ValueError("model must define materials and contain geometry")
        names = set()
        for mesh in self.meshes:
            _name(mesh.name, "Mesh")
            if mesh.name in names:
                raise ValueError(f"duplicate mesh: {mesh.name}")
            names.add(mesh.name)
            if mesh.material not in self.materials:
                raise ValueError(f"mesh {mesh.name} references unknown material {mesh.material}")
            if len(mesh.vertices) < 3 or not mesh.faces:
                raise ValueError(f"mesh {mesh.name} has no renderable geometry")
            for vertex in mesh.vertices:
                _vector3(vertex, f"{mesh.name} vertex")
            for face in mesh.faces:
                if len(face) < 3 or len(set(face)) != len(face):
                    raise ValueError(f"mesh {mesh.name} contains an invalid face")
                if any(index < 0 or index >= len(mesh.vertices) for index in face):
                    raise ValueError(f"mesh {mesh.name} contains an out-of-range face index")
