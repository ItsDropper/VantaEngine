"""Starter asset: a small timber cabin built with the VantaEngine Python SDK."""
from vanta_sdk import Material, Model

def build() -> Model:
    model = Model("starter_cabin")
    model.add_material(Material("timber", (0.29,0.12,0.055), 0.86, 0.0))
    model.add_material(Material("roof_slate", (0.075,0.095,0.12), 0.72, 0.08))
    model.add_material(Material("stone", (0.31,0.33,0.34), 0.94, 0.0))
    model.add_material(Material("glass", (0.045,0.19,0.23), 0.18, 0.12))
    model.add_material(Material("paint", (0.72,0.68,0.56), 0.78, 0.0))
    model.add_material(Material("iron", (0.09,0.095,0.10), 0.38, 0.72))
    model.add_box("stone_foundation", (0,0.28,0), (4.9,0.56,4.1), "stone")
    model.add_box("timber_walls", (0,1.92,0), (4.55,3.0,3.8), "timber")
    model.add_box("front_door", (0,1.22,-1.93), (0.92,2.05,0.10), "paint")
    model.add_box("door_handle", (0.34,1.2,-2.0), (0.07,0.18,0.07), "iron")
    for side,x in (("left",-1.32),("right",1.32)):
        model.add_box(f"{side}_window_glass", (x,2.12,-1.94), (0.82,0.92,0.08), "glass")
        model.add_box(f"{side}_window_sill", (x,1.62,-2.01), (1.02,0.10,0.18), "paint")
        model.add_box(f"{side}_window_lintel", (x,2.62,-2.01), (1.02,0.10,0.18), "paint")
        model.add_box(f"{side}_window_mullion", (x,2.12,-2.01), (0.07,0.94,0.10), "paint")
    model.add_box("roof_left_slope", (-1.17,3.78,0), (3.28,0.22,4.35), "roof_slate", (0,0,0.62))
    model.add_box("roof_right_slope", (1.17,3.78,0), (3.28,0.22,4.35), "roof_slate", (0,0,-0.62))
    model.add_box("ridge_cap", (0,4.68,0), (0.26,0.20,4.48), "iron")
    model.add_cylinder("chimney", (1.25,4.75,0.72), 0.28, 1.8, "stone", 12)
    model.add_box("chimney_cap", (1.25,5.68,0.72), (0.72,0.14,0.72), "stone")
    model.add_box("porch_deck", (0,0.66,-2.42), (3.2,0.18,1.0), "timber")
    for index,x in enumerate((-1.35,1.35)):
        model.add_box(f"porch_post_{index}", (x,1.65,-2.78), (0.16,2.0,0.16), "paint")
    model.add_box("porch_beam", (0,2.68,-2.78), (3.0,0.18,0.18), "timber")
    for index,x in enumerate((-0.9,0.0,0.9)):
        model.add_box(f"porch_step_{index}", (x,0.25,-3.05), (0.82,0.22,0.42), "stone")
    model.validate()
    return model
