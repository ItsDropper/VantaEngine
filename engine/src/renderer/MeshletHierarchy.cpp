#include "vanta/renderer/MeshletHierarchy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace vanta::renderer
{
namespace
{
bool IsFinite(const math::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

struct Sphere
{
    math::Vec3 center{};
    float radius = 0.0F;
};

Sphere MergeSpheres(const Sphere& a, const Sphere& b) noexcept
{
    const math::Vec3 delta = b.center - a.center;
    const float distance = delta.Length();
    if (a.radius >= distance + b.radius)
        return a;
    if (b.radius >= distance + a.radius)
        return b;
    const float radius = (distance + a.radius + b.radius) * 0.5F;
    return {a.center + delta * ((radius - a.radius) / distance), radius};
}

struct BuildNode
{
    Sphere bounds{};
    std::uint32_t index = 0U;
};

std::size_t LongestAxis(const std::vector<BuildNode>& nodes) noexcept
{
    math::Vec3 minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    math::Vec3 maximum{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const BuildNode& node : nodes)
    {
        minimum.x = std::min(minimum.x, node.bounds.center.x);
        minimum.y = std::min(minimum.y, node.bounds.center.y);
        minimum.z = std::min(minimum.z, node.bounds.center.z);
        maximum.x = std::max(maximum.x, node.bounds.center.x);
        maximum.y = std::max(maximum.y, node.bounds.center.y);
        maximum.z = std::max(maximum.z, node.bounds.center.z);
    }
    const math::Vec3 extent = maximum - minimum;
    if (extent.y > extent.x && extent.y >= extent.z)
        return 1U;
    if (extent.z > extent.x && extent.z > extent.y)
        return 2U;
    return 0U;
}

float AxisValue(const math::Vec3& value, const std::size_t axis) noexcept
{
    return axis == 0U ? value.x : (axis == 1U ? value.y : value.z);
}
} // namespace

bool BuildMeshletHierarchy(
    const MeshletData& meshlets,
    MeshletHierarchy& output,
    std::string& error,
    const MeshletHierarchyBuildOptions options)
{
    output = {};
    output.rootNode = MeshletHierarchy::InvalidIndex;

    if (options.maxLeafMeshlets == 0U || options.maxChildren < 2U)
    {
        error = "Meshlet hierarchy limits require at least one meshlet per leaf and two children per parent.";
        return false;
    }
    if (meshlets.meshlets.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Meshlet hierarchy exceeds supported 32-bit index limits.";
        return false;
    }

    if (meshlets.meshlets.empty())
    {
        if (!meshlets.vertexIndices.empty() || !meshlets.primitiveIndices.empty())
        {
            error = "Empty meshlet hierarchy input contains orphaned meshlet payload data.";
            return false;
        }
        error.clear();
        return true;
    }

    for (const MeshletDescriptor& descriptor : meshlets.meshlets)
    {
        if (descriptor.vertexCount == 0U || descriptor.primitiveCount == 0U ||
            !IsFinite(descriptor.boundsCenter) ||
            !std::isfinite(descriptor.boundsRadius) || descriptor.boundsRadius < 0.0F ||
            descriptor.vertexOffset > meshlets.vertexIndices.size() ||
            descriptor.vertexCount > meshlets.vertexIndices.size() - descriptor.vertexOffset ||
            descriptor.primitiveOffset > meshlets.primitiveIndices.size() ||
            descriptor.primitiveCount > (meshlets.primitiveIndices.size() - descriptor.primitiveOffset) / 3U)
        {
            error = "Meshlet hierarchy input contains an invalid descriptor or bounds.";
            return false;
        }
        const std::size_t primitiveByteCount =
            static_cast<std::size_t>(descriptor.primitiveCount) * 3U;
        for (std::size_t primitive = 0U; primitive < primitiveByteCount; ++primitive)
        {
            if (meshlets.primitiveIndices[descriptor.primitiveOffset + primitive] >= descriptor.vertexCount)
            {
                error = "Meshlet hierarchy input contains an out-of-range local primitive index.";
                return false;
            }
        }
    }

    output.meshletIndices.resize(meshlets.meshlets.size());
    std::iota(output.meshletIndices.begin(), output.meshletIndices.end(), 0U);

    std::vector<BuildNode> currentLevel;
    currentLevel.reserve((meshlets.meshlets.size() + options.maxLeafMeshlets - 1U) /
        options.maxLeafMeshlets);
    const std::size_t leafAxis = [&]()
    {
        std::vector<BuildNode> all;
        all.reserve(meshlets.meshlets.size());
        for (std::size_t index = 0U; index < meshlets.meshlets.size(); ++index)
        {
            const MeshletDescriptor& descriptor = meshlets.meshlets[index];
            all.push_back({{descriptor.boundsCenter, descriptor.boundsRadius},
                static_cast<std::uint32_t>(index)});
        }
        return LongestAxis(all);
    }();

    std::stable_sort(output.meshletIndices.begin(), output.meshletIndices.end(),
        [&](const std::uint32_t left, const std::uint32_t right)
        {
            const float a = AxisValue(meshlets.meshlets[left].boundsCenter, leafAxis);
            const float b = AxisValue(meshlets.meshlets[right].boundsCenter, leafAxis);
            return a == b ? left < right : a < b;
        });

    for (std::size_t first = 0U; first < output.meshletIndices.size();
         first += options.maxLeafMeshlets)
    {
        const std::size_t count = std::min<std::size_t>(
            options.maxLeafMeshlets, output.meshletIndices.size() - first);
        Sphere bounds{meshlets.meshlets[output.meshletIndices[first]].boundsCenter,
            meshlets.meshlets[output.meshletIndices[first]].boundsRadius};
        for (std::size_t offset = 1U; offset < count; ++offset)
        {
            const MeshletDescriptor& descriptor =
                meshlets.meshlets[output.meshletIndices[first + offset]];
            bounds = MergeSpheres(bounds, {descriptor.boundsCenter, descriptor.boundsRadius});
        }
        if (output.nodes.size() >= MeshletHierarchy::InvalidIndex)
        {
            error = "Meshlet hierarchy node count exceeds 32-bit indices.";
            output = {};
            return false;
        }
        const auto nodeIndex = static_cast<std::uint32_t>(output.nodes.size());
        output.nodes.push_back({bounds.center, bounds.radius, 0U, 0U,
            static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count)});
        currentLevel.push_back({bounds, nodeIndex});
    }

    while (currentLevel.size() > 1U)
    {
        const std::size_t axis = LongestAxis(currentLevel);
        std::stable_sort(currentLevel.begin(), currentLevel.end(),
            [axis](const BuildNode& left, const BuildNode& right)
            {
                const float a = AxisValue(left.bounds.center, axis);
                const float b = AxisValue(right.bounds.center, axis);
                return a == b ? left.index < right.index : a < b;
            });

        std::vector<BuildNode> nextLevel;
        const std::size_t groupCount =
            (currentLevel.size() + options.maxChildren - 1U) / options.maxChildren;
        const std::size_t baseGroupSize = currentLevel.size() / groupCount;
        const std::size_t largerGroupCount = currentLevel.size() % groupCount;
        nextLevel.reserve(groupCount);
        std::size_t first = 0U;
        for (std::size_t group = 0U; group < groupCount; ++group)
        {
            const std::size_t count = baseGroupSize + (group < largerGroupCount ? 1U : 0U);
            Sphere bounds = currentLevel[first].bounds;
            for (std::size_t offset = 1U; offset < count; ++offset)
                bounds = MergeSpheres(bounds, currentLevel[first + offset].bounds);

            if (output.nodes.size() >= MeshletHierarchy::InvalidIndex ||
                output.childIndices.size() > MeshletHierarchy::InvalidIndex - count)
            {
                error = "Meshlet hierarchy child or node count exceeds 32-bit indices.";
                output = {};
                return false;
            }
            const auto firstChild = static_cast<std::uint32_t>(output.childIndices.size());
            for (std::size_t offset = 0U; offset < count; ++offset)
                output.childIndices.push_back(currentLevel[first + offset].index);

            const auto nodeIndex = static_cast<std::uint32_t>(output.nodes.size());
            output.nodes.push_back({bounds.center, bounds.radius, firstChild,
                static_cast<std::uint32_t>(count), 0U, 0U});
            nextLevel.push_back({bounds, nodeIndex});
            first += count;
        }
        currentLevel = std::move(nextLevel);
    }

    output.rootNode = currentLevel.front().index;
    error.clear();
    return true;
}

bool BuildGpuMeshletHierarchy(
    const MeshletHierarchy& source,
    GpuMeshletHierarchy& output,
    std::string& error)
{
    output = {};
    output.rootNode = MeshletHierarchy::InvalidIndex;

    if (source.nodes.empty())
    {
        if (source.rootNode != MeshletHierarchy::InvalidIndex ||
            !source.childIndices.empty() || !source.meshletIndices.empty())
        {
            error = "Empty meshlet hierarchy has non-empty root or index data.";
            return false;
        }
        error.clear();
        return true;
    }

    if (source.rootNode >= source.nodes.size() ||
        source.nodes.size() > std::numeric_limits<std::uint32_t>::max() ||
        source.childIndices.size() > std::numeric_limits<std::uint32_t>::max() ||
        source.meshletIndices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Meshlet hierarchy cannot be represented by 32-bit GPU indices.";
        return false;
    }

    output.nodes.reserve(source.nodes.size());
    std::vector<std::uint32_t> parentCounts(source.nodes.size(), 0U);
    for (std::size_t nodeIndex = 0U; nodeIndex < source.nodes.size(); ++nodeIndex)
    {
        const MeshletHierarchyNode& node = source.nodes[nodeIndex];
        if (!IsFinite(node.boundsCenter) || !std::isfinite(node.boundsRadius) ||
            node.boundsRadius < 0.0F)
        {
            error = "Meshlet hierarchy contains non-finite or negative GPU bounds.";
            output = {};
            return false;
        }

        GpuMeshletHierarchyNode gpuNode{};
        gpuNode.bounds[0] = node.boundsCenter.x;
        gpuNode.bounds[1] = node.boundsCenter.y;
        gpuNode.bounds[2] = node.boundsCenter.z;
        gpuNode.bounds[3] = node.boundsRadius;

        if (node.IsLeaf())
        {
            if (node.meshletCount == 0U || node.firstChild != 0U ||
                node.firstMeshlet > source.meshletIndices.size() ||
                node.meshletCount > source.meshletIndices.size() - node.firstMeshlet)
            {
                error = "Meshlet hierarchy leaf has an invalid meshlet range.";
                output = {};
                return false;
            }
            gpuNode.meshletRange[0] = node.firstMeshlet;
            gpuNode.meshletRange[1] = node.meshletCount;
            for (std::size_t offset = 0U; offset < node.meshletCount; ++offset)
            {
                if (source.meshletIndices[node.firstMeshlet + offset] >= source.meshletIndices.size())
                {
                    error = "Meshlet hierarchy leaf references an invalid meshlet index.";
                    output = {};
                    return false;
                }
            }
        }
        else
        {
            if (node.meshletCount != 0U || node.firstMeshlet != 0U ||
                node.childCount < 2U || node.firstChild > source.childIndices.size() ||
                node.childCount > source.childIndices.size() - node.firstChild)
            {
                error = "Meshlet hierarchy internal node has an invalid child range.";
                output = {};
                return false;
            }
            gpuNode.childRange[0] = node.firstChild;
            gpuNode.childRange[1] = node.childCount;
            for (std::size_t offset = 0U; offset < node.childCount; ++offset)
            {
                const std::uint32_t child = source.childIndices[node.firstChild + offset];
                if (child >= source.nodes.size() || child == nodeIndex ||
                    ++parentCounts[child] > 1U)
                {
                    error = "Meshlet hierarchy internal node references an invalid or multiply-parented child.";
                    output = {};
                    return false;
                }
            }
        }
        output.nodes.push_back(gpuNode);
    }

    for (std::size_t nodeIndex = 0U; nodeIndex < parentCounts.size(); ++nodeIndex)
    {
        const std::uint32_t expectedParents = nodeIndex == source.rootNode ? 0U : 1U;
        if (parentCounts[nodeIndex] != expectedParents)
        {
            error = "Meshlet hierarchy is disconnected or has an invalid root relationship.";
            output = {};
            return false;
        }
    }

    std::vector<std::uint32_t> pendingNodes{source.rootNode};
    std::vector<bool> visited(source.nodes.size(), false);
    std::size_t visitedCount = 0U;
    while (!pendingNodes.empty())
    {
        const std::uint32_t nodeIndex = pendingNodes.back();
        pendingNodes.pop_back();
        if (visited[nodeIndex])
        {
            error = "Meshlet hierarchy contains a cycle or repeated child reference.";
            output = {};
            return false;
        }
        visited[nodeIndex] = true;
        ++visitedCount;
        const MeshletHierarchyNode& node = source.nodes[nodeIndex];
        for (std::size_t offset = 0U; offset < node.childCount; ++offset)
            pendingNodes.push_back(source.childIndices[node.firstChild + offset]);
    }
    if (visitedCount != source.nodes.size())
    {
        error = "Meshlet hierarchy contains nodes unreachable from its root.";
        output = {};
        return false;
    }

    output.childIndices = source.childIndices;
    output.meshletIndices = source.meshletIndices;
    output.rootNode = source.rootNode;
    error.clear();
    return true;
}

} // namespace vanta::renderer
