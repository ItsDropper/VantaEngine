#pragma once

#include "vanta/renderer/ObjLoader.hpp"

#include <filesystem>
#include <string>

namespace vanta::renderer
{
// Imports static triangle meshes from glTF 2.0 (.gltf or .glb), including node transforms,
// indexed geometry, UVs, normals, per-primitive materials, and external PBR texture images.
// Images embedded inside GLB/data URIs are rejected with a specific error for now.
[[nodiscard]] bool LoadGltf(const std::filesystem::path& path, CpuModel& output, std::string& error);
} // namespace vanta::renderer
