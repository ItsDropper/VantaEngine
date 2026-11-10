#pragma once

#include "vanta/renderer/MeshletBuilder.hpp"

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

struct MeshletHierarchy
{
    // Node zero is not special; rootNode identifies the root, or is invalid when empty.
    static constexpr std::uint32_t InvalidIndex = UINT32_MAX;
    std::vector<MeshletHierarchyNode> nodes;
    std::vector<std::uint32_t> childIndices;
    std::vector<std::uint32_t> meshletIndices;
    std::uint32_t rootNode = InvalidIndex;
};

// Builds a deterministic spatial hierarchy over existing meshlets. It preserves the
// meshlet payload and produces conservative enclosing spheres for hierarchical culling.
[[nodiscard]] bool BuildMeshletHierarchy(
    const MeshletData& meshlets,
    MeshletHierarchy& output,
    std::string& error,
    MeshletHierarchyBuildOptions options = {});
} // namespace vanta::renderer
