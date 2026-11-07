#pragma once

#include "vanta/assets/ObjLoader.hpp"

#include <filesystem>
#include <string>

namespace vanta::assets
{
// Imports static triangle meshes from glTF 2.0 (.gltf or .glb), including node transforms,
// indexed geometry, UVs, normals, per-primitive materials, external PBR texture images,
// data-URI images, and GLB buffer-view images. Embedded image bytes remain owned by the
// imported CPU material; no mutable sidecar files are extracted.
[[nodiscard]] bool LoadGltf(const std::filesystem::path& path, CpuModel& output, std::string& error);
} // namespace vanta::assets
