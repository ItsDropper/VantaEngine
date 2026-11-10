#include "vanta/renderer/MeshletBuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace vanta::renderer
{
namespace
{
bool IsFinite(const math::Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

bool BuildMeshlets(
    const std::span<const assets::ModelVertex> vertices,
    const std::span<const std::uint32_t> indices,
    MeshletData& output,
    std::string& error,
    const MeshletBuildOptions options)
{
    output = {};

    if (options.maxVertices < 3U || options.maxVertices > 255U ||
        options.maxPrimitives == 0U || options.maxPrimitives > 255U)
    {
        error = "Meshlet limits must allow at least one triangle and fit byte-sized local indices.";
        return false;
    }
    if (indices.size() % 3U != 0U)
    {
        error = "Meshlet input indices must describe complete triangles.";
        return false;
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        indices.size() / 3U > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Meshlet input exceeds the supported 32-bit source geometry limits.";
        return false;
    }
    for (const assets::ModelVertex& vertex : vertices)
    {
        if (!IsFinite(vertex.position))
        {
            error = "Meshlet input contains a non-finite vertex position.";
            return false;
        }
    }
    for (const std::uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            error = "Meshlet input contains an out-of-range vertex index.";
            return false;
        }
    }

    if (indices.empty())
    {
        error.clear();
        return true;
    }

    std::unordered_map<std::uint32_t, std::uint8_t> localVertexByGlobal;
    localVertexByGlobal.reserve(options.maxVertices);
    std::size_t currentVertexOffset = 0U;
    std::size_t currentPrimitiveOffset = 0U;

    const auto finishMeshlet = [&]() -> bool
    {
        const std::size_t vertexCount = output.vertexIndices.size() - currentVertexOffset;
        const std::size_t primitiveByteCount = output.primitiveIndices.size() - currentPrimitiveOffset;
        const std::size_t primitiveCount = primitiveByteCount / 3U;
        if (primitiveCount == 0U)
            return true;

        if (currentVertexOffset > std::numeric_limits<std::uint32_t>::max() ||
            currentPrimitiveOffset > std::numeric_limits<std::uint32_t>::max() ||
            vertexCount > std::numeric_limits<std::uint32_t>::max() ||
            primitiveCount > std::numeric_limits<std::uint32_t>::max())
        {
            error = "Meshlet output exceeds 32-bit descriptor offsets.";
            return false;
        }

        math::Vec3 minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        math::Vec3 maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (std::size_t local = 0U; local < vertexCount; ++local)
        {
            const math::Vec3& position =
                vertices[output.vertexIndices[currentVertexOffset + local]].position;
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
        }

        MeshletDescriptor descriptor;
        descriptor.vertexOffset = static_cast<std::uint32_t>(currentVertexOffset);
        descriptor.vertexCount = static_cast<std::uint32_t>(vertexCount);
        descriptor.primitiveOffset = static_cast<std::uint32_t>(currentPrimitiveOffset);
        descriptor.primitiveCount = static_cast<std::uint32_t>(primitiveCount);
        descriptor.boundsCenter = (minimum + maximum) * 0.5F;
        float radiusSquared = 0.0F;
        for (std::size_t local = 0U; local < vertexCount; ++local)
        {
            const math::Vec3 delta =
                vertices[output.vertexIndices[currentVertexOffset + local]].position -
                descriptor.boundsCenter;
            radiusSquared = std::max(radiusSquared, delta.LengthSquared());
        }
        descriptor.boundsRadius = std::sqrt(radiusSquared);

        math::Vec3 normalSum{};
        std::vector<math::Vec3> faceNormals;
        faceNormals.reserve(primitiveCount);
        for (std::size_t primitive = 0U; primitive < primitiveCount; ++primitive)
        {
            const std::size_t byteOffset = currentPrimitiveOffset + primitive * 3U;
            const auto globalA = output.vertexIndices[currentVertexOffset +
                output.primitiveIndices[byteOffset]];
            const auto globalB = output.vertexIndices[currentVertexOffset +
                output.primitiveIndices[byteOffset + 1U]];
            const auto globalC = output.vertexIndices[currentVertexOffset +
                output.primitiveIndices[byteOffset + 2U]];
            const math::Vec3 edgeAB = vertices[globalB].position - vertices[globalA].position;
            const math::Vec3 edgeAC = vertices[globalC].position - vertices[globalA].position;
            const auto normal = edgeAB.Cross(edgeAC).Normalized();
            if (normal)
            {
                faceNormals.push_back(*normal);
                normalSum += *normal;
            }
        }

        const auto coneAxis = normalSum.Normalized();
        if (coneAxis && faceNormals.size() == primitiveCount)
        {
            descriptor.normalConeAxis = *coneAxis;
            descriptor.normalConeCutoff = 1.0F;
            for (const math::Vec3& normal : faceNormals)
                descriptor.normalConeCutoff =
                    std::min(descriptor.normalConeCutoff, coneAxis->Dot(normal));
        }
        output.meshlets.push_back(descriptor);

        currentVertexOffset = output.vertexIndices.size();
        currentPrimitiveOffset = output.primitiveIndices.size();
        localVertexByGlobal.clear();
        return true;
    };

    for (std::size_t triangleOffset = 0U; triangleOffset < indices.size(); triangleOffset += 3U)
    {
        const std::array<std::uint32_t, 3> triangle{
            indices[triangleOffset], indices[triangleOffset + 1U], indices[triangleOffset + 2U]};

        std::uint32_t missingVertices = 0U;
        for (std::size_t corner = 0U; corner < triangle.size(); ++corner)
        {
            if (localVertexByGlobal.contains(triangle[corner]))
                continue;
            bool alreadyCounted = false;
            for (std::size_t earlier = 0U; earlier < corner; ++earlier)
                alreadyCounted = alreadyCounted || triangle[earlier] == triangle[corner];
            if (!alreadyCounted)
                ++missingVertices;
        }

        const std::size_t currentPrimitiveCount =
            (output.primitiveIndices.size() - currentPrimitiveOffset) / 3U;
        if (currentPrimitiveCount != 0U &&
            (localVertexByGlobal.size() + missingVertices > options.maxVertices ||
             currentPrimitiveCount >= options.maxPrimitives))
        {
            if (!finishMeshlet())
            {
                output = {};
                return false;
            }
        }

        for (const std::uint32_t globalIndex : triangle)
        {
            auto found = localVertexByGlobal.find(globalIndex);
            if (found == localVertexByGlobal.end())
            {
                const std::size_t localIndex = output.vertexIndices.size() - currentVertexOffset;
                if (localIndex >= options.maxVertices ||
                    localIndex > std::numeric_limits<std::uint8_t>::max())
                {
                    error = "Meshlet builder exceeded its configured local vertex limit.";
                    output = {};
                    return false;
                }
                const auto localByte = static_cast<std::uint8_t>(localIndex);
                output.vertexIndices.push_back(globalIndex);
                found = localVertexByGlobal.emplace(globalIndex, localByte).first;
            }
            output.primitiveIndices.push_back(found->second);
        }
    }

    if (!finishMeshlet())
    {
        output = {};
        return false;
    }
    error.clear();
    return true;
}

bool BuildMeshletDrawData(
    const std::span<const assets::ModelVertex> vertices,
    const MeshletData& meshlets,
    MeshletDrawData& output,
    std::string& error)
{
    output = {};

    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.vertexIndices.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.primitiveIndices.size() > std::numeric_limits<std::uint32_t>::max() ||
        meshlets.meshlets.size() > std::numeric_limits<std::uint32_t>::max())
    {
        error = "Meshlet draw data exceeds the supported 32-bit indexed-draw limits.";
        return false;
    }

    output.draws.reserve(meshlets.meshlets.size());
    for (const MeshletDescriptor& meshlet : meshlets.meshlets)
    {
        if (meshlet.vertexCount == 0U || meshlet.primitiveCount == 0U ||
            meshlet.vertexOffset > meshlets.vertexIndices.size() ||
            meshlet.vertexCount > meshlets.vertexIndices.size() - meshlet.vertexOffset ||
            meshlet.primitiveOffset > meshlets.primitiveIndices.size() / 3U ||
            meshlet.primitiveCount > (meshlets.primitiveIndices.size() / 3U) - meshlet.primitiveOffset ||
            !IsFinite(meshlet.boundsCenter) || !std::isfinite(meshlet.boundsRadius) ||
            meshlet.boundsRadius < 0.0F || !IsFinite(meshlet.normalConeAxis) ||
            !std::isfinite(meshlet.normalConeCutoff))
        {
            error = "Meshlet draw descriptor contains an invalid range or bound.";
            output = {};
            return false;
        }

        const std::size_t indexCount = static_cast<std::size_t>(meshlet.primitiveCount) * 3U;
        if (output.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
            indexCount > std::numeric_limits<std::uint32_t>::max() - output.indices.size())
        {
            error = "Expanded meshlet indices exceed 32-bit indexed-draw offsets.";
            output = {};
            return false;
        }

        MeshletDrawDescriptor draw;
        draw.firstIndex = static_cast<std::uint32_t>(output.indices.size());
        draw.indexCount = static_cast<std::uint32_t>(indexCount);
        draw.boundsCenter = meshlet.boundsCenter;
        draw.boundsRadius = meshlet.boundsRadius;
        draw.normalConeAxis = meshlet.normalConeAxis;
        draw.normalConeCutoff = meshlet.normalConeCutoff;

        const std::size_t primitiveByteOffset =
            static_cast<std::size_t>(meshlet.primitiveOffset) * 3U;
        for (std::size_t indexOffset = 0U; indexOffset < indexCount; ++indexOffset)
        {
            const std::uint8_t localIndex =
                meshlets.primitiveIndices[primitiveByteOffset + indexOffset];
            if (localIndex >= meshlet.vertexCount)
            {
                error = "Meshlet primitive references a vertex outside its local vertex range.";
                output = {};
                return false;
            }
            const std::uint32_t globalIndex =
                meshlets.vertexIndices[static_cast<std::size_t>(meshlet.vertexOffset) + localIndex];
            if (globalIndex >= vertices.size())
            {
                error = "Meshlet references a vertex outside the source vertex buffer.";
                output = {};
                return false;
            }
            output.indices.push_back(globalIndex);
        }
        output.draws.push_back(draw);
    }

    error.clear();
    return true;
}

} // namespace vanta::renderer
