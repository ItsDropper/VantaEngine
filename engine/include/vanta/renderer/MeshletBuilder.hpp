#pragma once

#include "vanta/assets/ObjLoader.hpp"
#include "vanta/math/Math.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vanta::renderer
{
struct MeshletBuildOptions
{
    // Local primitive indices are bytes, so a meshlet can address at most 255 vertices.
    std::uint32_t maxVertices = 64U;
    std::uint32_t maxPrimitives = 126U;
};

struct MeshletDescriptor
{
    std::uint32_t vertexOffset = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t primitiveOffset = 0U;
    std::uint32_t primitiveCount = 0U;
    math::Vec3 boundsCenter{};
    float boundsRadius = 0.0F;
    math::Vec3 normalConeAxis{};
    // Minimum dot product between the cone axis and its valid triangle normals.
    // A negative value disables normal-cone rejection for degenerate or wide cones.
    float normalConeCutoff = -1.0F;
};

struct MeshletData
{
    // Global source-vertex indices, grouped by MeshletDescriptor::vertexOffset/count.
    std::vector<std::uint32_t> vertexIndices;
    // Three byte-sized local vertex indices per triangle, grouped by primitiveOffset/count.
    std::vector<std::uint8_t> primitiveIndices;
    std::vector<MeshletDescriptor> meshlets;
};

struct MeshletDrawDescriptor
{
    // Offsets into MeshletDrawData::indices; indices address the original vertex buffer.
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
    math::Vec3 boundsCenter{};
    float boundsRadius = 0.0F;
    math::Vec3 normalConeAxis{};
    float normalConeCutoff = -1.0F;
};

struct MeshletDrawData
{
    // Expanded global vertex indices, suitable for a conventional indexed draw with
    // BaseVertexLocation = 0 and the original mesh vertex buffer.
    std::vector<std::uint32_t> indices;
    std::vector<MeshletDrawDescriptor> draws;
};

// Expands meshlet-local byte indices into contiguous conventional indexed-draw ranges.
// This is a deterministic bridge to indexed indirect drawing; it does not submit GPU work.
[[nodiscard]] bool BuildMeshletDrawData(
    std::span<const assets::ModelVertex> vertices,
    const MeshletData& meshlets,
    MeshletDrawData& output,
    std::string& error);

// Deterministically partitions indexed triangle geometry into bounded meshlets.
// This is the CPU asset-build foundation for GPU cluster culling; it does not alter
// the current indexed renderer or its instance-buffer layout.
[[nodiscard]] bool BuildMeshlets(
    std::span<const assets::ModelVertex> vertices,
    std::span<const std::uint32_t> indices,
    MeshletData& output,
    std::string& error,
    MeshletBuildOptions options = {});
} // namespace vanta::renderer
