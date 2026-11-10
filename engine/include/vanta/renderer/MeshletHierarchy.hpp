#pragma once

#include "vanta/renderer/MeshletBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vanta::renderer
{
struct MeshletHierarchyBuildOptions
{
    std::uint32_t maxLeafMeshlets = 8U;
    std::uint32_t maxChildren = 4U;
};

struct MeshletHierarchyNode
{
    math::Vec3 boundsCenter{};
    float boundsRadius = 0.0F;
    // Internal nodes index MeshletHierarchy::childIndices. Leaves index meshletIndices.
    std::uint32_t firstChild = 0U;
    std::uint32_t childCount = 0U;
    std::uint32_t firstMeshlet = 0U;
    std::uint32_t meshletCount = 0U;

    [[nodiscard]] bool IsLeaf() const noexcept { return childCount == 0U; }
};

// Stable 48-byte GPU storage-buffer record. The three uint4-compatible groups map
// directly to HLSL StructuredBuffer fields and avoid relying on compiler-specific Vec3 layout.
struct alignas(16) GpuMeshletHierarchyNode
{
    float bounds[4]{};
    std::uint32_t childRange[4]{};
    std::uint32_t meshletRange[4]{};
};
static_assert(sizeof(GpuMeshletHierarchyNode) == 48U);
static_assert(alignof(GpuMeshletHierarchyNode) == 16U);

struct MeshletHierarchy
{
    // Node zero is not special; rootNode identifies the root, or is invalid when empty.
    static constexpr std::uint32_t InvalidIndex = UINT32_MAX;
    std::vector<MeshletHierarchyNode> nodes;
    std::vector<std::uint32_t> childIndices;
    std::vector<std::uint32_t> meshletIndices;
    std::uint32_t rootNode = InvalidIndex;
};

struct GpuMeshletHierarchy
{
    std::vector<GpuMeshletHierarchyNode> nodes;
    std::vector<std::uint32_t> childIndices;
    std::vector<std::uint32_t> meshletIndices;
    std::uint32_t rootNode = MeshletHierarchy::InvalidIndex;
};

// Converts the CPU hierarchy into a stable, upload-ready GPU buffer layout. This does not
// allocate GPU resources or alter renderer instance streams.
[[nodiscard]] bool BuildGpuMeshletHierarchy(
    const MeshletHierarchy& source,
    GpuMeshletHierarchy& output,
    std::string& error);

// Builds a deterministic spatial hierarchy over existing meshlets. It preserves the
// meshlet payload and produces conservative enclosing spheres for hierarchical culling.
[[nodiscard]] bool BuildMeshletHierarchy(
    const MeshletData& meshlets,
    MeshletHierarchy& output,
    std::string& error,
    MeshletHierarchyBuildOptions options = {});
} // namespace vanta::renderer
