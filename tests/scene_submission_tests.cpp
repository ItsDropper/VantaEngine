#include "vanta/renderer/SceneSubmission.hpp"
#include "vanta/renderer/SceneGeometry.hpp"
#include "vanta/renderer/MeshletBuilder.hpp"
#include "vanta/renderer/MeshletHierarchy.hpp"
#include "vanta/renderer/EnvironmentLighting.hpp"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <iostream>
#include <utility>

using namespace vanta;
using namespace vanta::renderer;

namespace
{

void RequireImpl(const bool condition, const int line)
{
    if (!condition)
    {
        std::cerr << "SceneSubmissionTests assertion failed at line "
                  << line << std::endl;
        std::exit(EXIT_FAILURE);
    }
}
#define Require(condition) RequireImpl((condition), __LINE__)

MeshDescription Triangle()
{
    MeshDescription mesh;
    mesh.vertices = {
        {{-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.5F, 1.0F}
    };
    mesh.indices = {0U, 1U, 2U};
    return mesh;
}
}

int main()
{

    // Meshlet preprocessing is deterministic, bounded, and independent of the renderer's
    // existing instance-buffer layout.
    MeshDescription meshletQuad;
    meshletQuad.vertices = {
        {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 0.0F, 0.0F},
        {{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 1.0F, 0.0F},
        {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 1.0F, 1.0F},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 0.0F, 1.0F}
    };
    meshletQuad.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    MeshletData meshlets;
    std::string meshletError;
    Require(BuildMeshlets(meshletQuad.vertices, meshletQuad.indices, meshlets, meshletError));
    Require(meshlets.meshlets.size() == 1U);
    Require(meshlets.meshlets[0].vertexCount == 4U);
    Require(meshlets.meshlets[0].primitiveCount == 2U);
    Require(meshlets.vertexIndices.size() == 4U);
    Require(meshlets.primitiveIndices.size() == 6U);
    Require(meshlets.meshlets[0].boundsRadius > 0.0F);
    Require(meshlets.meshlets[0].normalConeCutoff > 0.99F);

    MeshletDrawData meshletDrawData;
    Require(BuildMeshletDrawData(meshletQuad.vertices, meshlets, meshletDrawData, meshletError));
    Require(meshletDrawData.draws.size() == 1U);
    Require(meshletDrawData.indices == meshletQuad.indices);
    Require(meshletDrawData.draws[0].firstIndex == 0U);
    Require(meshletDrawData.draws[0].indexCount == meshletQuad.indices.size());
    Require(meshletDrawData.draws[0].boundsRadius == meshlets.meshlets[0].boundsRadius);

    MeshletData invalidLocalMeshlet = meshlets;
    invalidLocalMeshlet.primitiveIndices[0] = 4U;
    Require(!BuildMeshletDrawData(meshletQuad.vertices, invalidLocalMeshlet,
        meshletDrawData, meshletError));
    Require(meshletDrawData.indices.empty() && meshletDrawData.draws.empty());

    MeshletData invalidGlobalMeshlet = meshlets;
    invalidGlobalMeshlet.vertexIndices[0] = 99U;
    Require(!BuildMeshletDrawData(meshletQuad.vertices, invalidGlobalMeshlet,
        meshletDrawData, meshletError));
    Require(meshletDrawData.indices.empty() && meshletDrawData.draws.empty());

    MeshletBuildOptions tinyMeshletOptions;
    tinyMeshletOptions.maxVertices = 3U;
    tinyMeshletOptions.maxPrimitives = 1U;
    Require(BuildMeshlets(meshletQuad.vertices, meshletQuad.indices,
        meshlets, meshletError, tinyMeshletOptions));
    Require(meshlets.meshlets.size() == 2U);
    for (const MeshletDescriptor& descriptor : meshlets.meshlets)
    {
        Require(descriptor.vertexCount == 3U);
        Require(descriptor.primitiveCount == 1U);
        Require(descriptor.vertexCount <= tinyMeshletOptions.maxVertices);
        Require(descriptor.primitiveCount <= tinyMeshletOptions.maxPrimitives);
    }
    Require(meshlets.primitiveIndices.size() == 6U);
    Require(BuildMeshletDrawData(meshletQuad.vertices, meshlets, meshletDrawData, meshletError));
    Require(meshletDrawData.draws.size() == 2U);
    Require(meshletDrawData.draws[0].firstIndex == 0U);
    Require(meshletDrawData.draws[0].indexCount == 3U);
    Require(meshletDrawData.draws[1].firstIndex == 3U);
    Require(meshletDrawData.draws[1].indexCount == 3U);
    Require(meshletDrawData.indices == meshletQuad.indices);

    MeshletData incompletePrimitiveData = meshlets;
    incompletePrimitiveData.primitiveIndices.push_back(0U);
    Require(!BuildMeshletDrawData(meshletQuad.vertices, incompletePrimitiveData,
        meshletDrawData, meshletError));
    Require(meshletDrawData.indices.empty() && meshletDrawData.draws.empty());

    MeshletData invalidDrawRange = meshlets;
    invalidDrawRange.meshlets[0].primitiveCount =
        static_cast<std::uint32_t>(invalidDrawRange.primitiveIndices.size());
    Require(!BuildMeshletDrawData(meshletQuad.vertices, invalidDrawRange,
        meshletDrawData, meshletError));
    Require(meshletDrawData.indices.empty() && meshletDrawData.draws.empty());

    MeshletBuildOptions invalidMeshletOptions;
    invalidMeshletOptions.maxVertices = 2U;
    Require(!BuildMeshlets(meshletQuad.vertices, meshletQuad.indices,
        meshlets, meshletError, invalidMeshletOptions));
    Require(meshlets.meshlets.empty() && meshlets.vertexIndices.empty() &&
        meshlets.primitiveIndices.empty());
    const std::vector<std::uint32_t> invalidMeshletIndices{0U, 1U, 99U};
    Require(!BuildMeshlets(meshletQuad.vertices, invalidMeshletIndices,
        meshlets, meshletError));
    Require(meshlets.meshlets.empty());


    // Spatial hierarchy groups meshlets into bounded, conservative parent spheres.
    MeshletData hierarchyInput;
    for (std::uint32_t index = 0U; index < 10U; ++index)
    {
        hierarchyInput.vertexIndices.insert(hierarchyInput.vertexIndices.end(),
            {index * 3U, index * 3U + 1U, index * 3U + 2U});
        hierarchyInput.primitiveIndices.insert(hierarchyInput.primitiveIndices.end(),
            {0U, 1U, 2U});
        MeshletDescriptor descriptor;
        descriptor.vertexOffset = index * 3U;
        descriptor.vertexCount = 3U;
        descriptor.primitiveOffset = index * 3U;
        descriptor.primitiveCount = 1U;
        descriptor.boundsCenter = {static_cast<float>(index), 0.0F, 0.0F};
        descriptor.boundsRadius = 0.5F;
        hierarchyInput.meshlets.push_back(descriptor);
    }
    MeshletHierarchy hierarchy;
    MeshletHierarchyBuildOptions hierarchyOptions;
    hierarchyOptions.maxLeafMeshlets = 1U;
    hierarchyOptions.maxChildren = 4U;
    Require(BuildMeshletHierarchy(hierarchyInput, hierarchy, meshletError, hierarchyOptions));
    Require(hierarchy.rootNode != MeshletHierarchy::InvalidIndex);
    Require(hierarchy.nodes[hierarchy.rootNode].childCount == 3U);
    Require(hierarchy.meshletIndices.size() == hierarchyInput.meshlets.size());
    std::vector<bool> hierarchyMeshletSeen(hierarchyInput.meshlets.size(), false);
    for (const MeshletHierarchyNode& node : hierarchy.nodes)
    {
        Require(node.boundsRadius >= 0.0F && std::isfinite(node.boundsRadius));
        if (node.IsLeaf())
        {
            Require(node.meshletCount == 1U);
            Require(node.firstMeshlet < hierarchy.meshletIndices.size());
            const std::uint32_t meshletIndex = hierarchy.meshletIndices[node.firstMeshlet];
            Require(meshletIndex < hierarchyMeshletSeen.size());
            Require(!hierarchyMeshletSeen[meshletIndex]);
            hierarchyMeshletSeen[meshletIndex] = true;
            const auto& source = hierarchyInput.meshlets[meshletIndex];
            Require((source.boundsCenter - node.boundsCenter).Length() + source.boundsRadius
                <= node.boundsRadius + 0.0001F);
        }
        else
        {
            Require(node.childCount >= 2U && node.childCount <= hierarchyOptions.maxChildren);
            Require(node.firstChild + node.childCount <= hierarchy.childIndices.size());
            for (std::uint32_t child = 0U; child < node.childCount; ++child)
            {
                const auto childIndex = hierarchy.childIndices[node.firstChild + child];
                Require(childIndex < hierarchy.nodes.size());
                const auto& childNode = hierarchy.nodes[childIndex];
                Require((childNode.boundsCenter - node.boundsCenter).Length() + childNode.boundsRadius
                    <= node.boundsRadius + 0.0001F);
            }
        }
    }
    for (const bool seen : hierarchyMeshletSeen)
        Require(seen);


    GpuMeshletHierarchy gpuHierarchy;
    Require(BuildGpuMeshletHierarchy(hierarchy, gpuHierarchy, meshletError));
    Require(gpuHierarchy.rootNode == hierarchy.rootNode);
    Require(gpuHierarchy.nodes.size() == hierarchy.nodes.size());
    Require(gpuHierarchy.childIndices == hierarchy.childIndices);
    Require(gpuHierarchy.meshletIndices == hierarchy.meshletIndices);
    Require(gpuHierarchy.nodes[gpuHierarchy.rootNode].childRange[1] == 3U);
    Require(gpuHierarchy.nodes[0].meshletRange[1] == 1U);

    MeshletHierarchy malformedGpuHierarchy = hierarchy;
    malformedGpuHierarchy.nodes[0].firstMeshlet =
        static_cast<std::uint32_t>(malformedGpuHierarchy.meshletIndices.size());
    Require(!BuildGpuMeshletHierarchy(malformedGpuHierarchy, gpuHierarchy, meshletError));
    Require(gpuHierarchy.nodes.empty() &&
        gpuHierarchy.rootNode == MeshletHierarchy::InvalidIndex);

    MeshletHierarchy cyclicGpuHierarchy = hierarchy;
    cyclicGpuHierarchy.childIndices[0] = cyclicGpuHierarchy.rootNode;
    Require(!BuildGpuMeshletHierarchy(cyclicGpuHierarchy, gpuHierarchy, meshletError));
    Require(gpuHierarchy.nodes.empty());

    MeshletData malformedHierarchy = hierarchyInput;
    malformedHierarchy.meshlets[0].primitiveOffset =
        static_cast<std::uint32_t>(malformedHierarchy.primitiveIndices.size() - 1U);
    Require(!BuildMeshletHierarchy(malformedHierarchy, hierarchy, meshletError, hierarchyOptions));
    Require(hierarchy.nodes.empty() && hierarchy.rootNode == MeshletHierarchy::InvalidIndex);

    // Reusing a SceneFrame retains its draw allocation across updates.
    SceneSubmission reusableScene;
    const MeshHandle reusableMesh = reusableScene.CreateMesh(Triangle());
    const MaterialHandle reusableMaterial = reusableScene.CreateMaterial({});
    const InstanceHandle reusableA = reusableScene.CreateInstance(reusableMesh, reusableMaterial);
    const InstanceHandle reusableB = reusableScene.CreateInstance(
        reusableMesh, reusableMaterial, math::Mat4::Translation({2.0F, 0.0F, 0.0F}));
    Require(reusableA.IsValid() && reusableB.IsValid());
    SceneFrame reusableFrame;
    FrameView reusableViewUpdated;
    reusableScene.BuildFrame(reusableViewUpdated, reusableFrame);
    Require(reusableFrame.draws.size() == 2U);
    const auto* reusableDrawStorage = reusableFrame.draws.data();
    const std::size_t reusableDrawCapacity = reusableFrame.draws.capacity();
    Require(reusableScene.SetTransform(reusableA,
        math::Mat4::Translation({1.0F, 0.0F, 0.0F})));
    FrameView reusableView;
    reusableScene.BuildFrame(reusableView, reusableFrame);
    Require(reusableFrame.draws.size() == 2U);
    Require(reusableFrame.draws.data() == reusableDrawStorage);
    Require(reusableFrame.draws.capacity() == reusableDrawCapacity);

    FrameView pointLightView;
    std::string pointLightError;
    Require(ValidateFrameView(pointLightView, pointLightError));
    pointLightView.pointLightCount = 1U;
    pointLightView.pointLights[0] = {{2.0F, 3.0F, 4.0F}, 18.0F, {1.0F, 0.45F, 0.2F}, 24.0F};
    Require(ValidateFrameView(pointLightView, pointLightError));
    pointLightView.pointLightCount = MaximumPointLightsPerFrame + 1U;
    Require(!ValidateFrameView(pointLightView, pointLightError));
    pointLightView.pointLightCount = 1U;
    pointLightView.pointLights[0].range = 0.0F;
    Require(!ValidateFrameView(pointLightView, pointLightError));
    pointLightView.pointLights[0].range = 18.0F;
    pointLightView.pointLights[0].intensity = std::numeric_limits<float>::quiet_NaN();
    Require(!ValidateFrameView(pointLightView, pointLightError));

    EnvironmentSettings lightingSettings{};
    lightingSettings.enabled = true;
    const EnvironmentLighting environmentLighting = BuildEnvironmentLighting(lightingSettings);
    Require(environmentLighting.diffuse[0][0] > 0.0F);
    Require(environmentLighting.diffuse[0][1] > 0.0F);
    Require(environmentLighting.diffuse[0][0] < 10.0F);
    Require(environmentLighting.specular[0][0][0] < 3.0F);
    Require(environmentLighting.specular[0][8][0] > environmentLighting.specular[4][8][0]);
    Require(environmentLighting.specular[4][8][0] <= environmentLighting.specular[0][8][0]);
    const EnvironmentLighting disabledLighting = BuildEnvironmentLighting(EnvironmentSettings{});
    Require(disabledLighting.diffuse[0][0] > 0.0F);
    EnvironmentSettings rotatedSun = lightingSettings;
    rotatedSun.sunDirection = {0.85F, 0.28F, -0.45F};
    const EnvironmentLighting rotatedLighting = BuildEnvironmentLighting(rotatedSun);
    Require(std::abs(rotatedLighting.specular[0][3][0] - environmentLighting.specular[0][3][0]) > 0.001F);
    for (const auto& coefficient : environmentLighting.diffuse)
        for (const float channel : coefficient)
            Require(std::isfinite(channel));

    SceneSubmission scene;
    const MeshHandle mesh = scene.CreateMesh(Triangle());
    const MaterialHandle material = scene.CreateMaterial({});
    Require(mesh.IsValid() && scene.Contains(mesh));
    Require(material.IsValid() && scene.Contains(material));

    const math::Mat4 world = math::Mat4::Translation({3.0F, 2.0F, 1.0F}) *
                             math::Mat4::Scale({2.0F, 2.0F, 2.0F});
    const InstanceHandle instance = scene.CreateInstance(mesh, material, world);
    Require(instance.IsValid() && scene.Contains(instance));
    Require(scene.MeshCount() == 1U && scene.MaterialCount() == 1U &&
           scene.InstanceCount() == 1U && scene.VisibleInstanceCount() == 1U);

    FrameView view;
    view.cameraPosition = {0.0F, 1.0F, -5.0F};
    view.frameNumber = 42U;
    view.environment.enabled = true;
    view.environment.sunDirection = {0.0F, 0.35F, -0.93675F};
    view.environment.sunIntensity = 2.4F;
    view.environment.fogDensity = 0.0035F;
    const SceneFrame frame = scene.BuildFrame(view);
    Require(frame.view.frameNumber == 42U);
    Require(frame.view.environment.enabled);
    Require(std::abs(frame.view.environment.sunIntensity - 2.4F) < 0.0001F);
    Require(std::abs(frame.view.environment.fogDensity - 0.0035F) < 0.00001F);
    Require(frame.draws.size() == 1U);
    Require(frame.draws[0].mesh->indices.size() == 3U);
    Require(frame.draws[0].material->description.roughness == 0.8F);
    Require(frame.draws[0].world.At(0, 3) == 3.0F);

    SubmittedSceneGeometry submitted;
    std::string geometryError;
    Require(BuildSubmittedSceneGeometry(frame, submitted, geometryError));
    Require(geometryError.empty());
    Require(submitted.vertices.size() == 3U && submitted.indices.size() == 3U);
    Require(submitted.meshletBatches.size() == 1U);
    Require(submitted.meshletDraws.size() == 1U);
    Require(submitted.meshletIndices == submitted.indices);
    Require(submitted.meshletBatches[0].firstDraw == 0U);
    Require(submitted.meshletBatches[0].drawCount == 1U);
    Require(submitted.meshletDraws[0].firstIndex == 0U);
    Require(submitted.meshletDraws[0].indexCount == 3U);
    Require(submitted.meshletHierarchyBatches.size() == 1U);
    Require(submitted.meshletHierarchyNodes.size() == 1U);
    Require(submitted.meshletHierarchyBatches[0].rootNode == 0U);
    Require(submitted.meshletHierarchyNodes[0].meshletRange[1] == 1U);
    Require(submitted.meshletHierarchyMeshlets[0] == 0U);
    Require(submitted.draws.size() == 1U);
    Require(submitted.vertices[0].position[0] == -1.0F);
    Require(submitted.vertices[0].position[1] == 0.0F);
    Require(submitted.vertices[0].position[2] == 0.0F);
    Require(submitted.draws[0].world.At(0, 3) == 3.0F);
    Require(submitted.vertices[0].normal[1] == 1.0F);
    Require(submitted.vertices[0].color[0] == 1.0F);
    Require(submitted.vertices[0].material[0] == 0.8F);

    // D3D12 stores every shadow transform before every color transform. Verify
    // multi-material batches use absolute offsets into that final concatenation.
    SceneSubmission batchedScene;
    const MeshHandle batchedMesh = batchedScene.CreateMesh(Triangle());
    MaterialDescription firstBatchMaterial;
    firstBatchMaterial.baseColor = {0.9F, 0.8F, 0.7F};
    MaterialDescription secondBatchMaterial;
    secondBatchMaterial.baseColor = {0.4F, 0.5F, 0.6F};
    const MaterialHandle firstBatchHandle = batchedScene.CreateMaterial(firstBatchMaterial);
    const MaterialHandle secondBatchHandle = batchedScene.CreateMaterial(secondBatchMaterial);
    Require(batchedMesh.IsValid() && firstBatchHandle.IsValid() && secondBatchHandle.IsValid());
    const math::Mat4 firstBatchTransformA = math::Mat4::Translation({-0.2F, 0.0F, 0.5F});
    const math::Mat4 firstBatchTransformB = math::Mat4::Translation({0.2F, 0.0F, 0.5F});
    const math::Mat4 secondBatchTransformA = math::Mat4::Translation({-0.1F, 0.0F, 0.6F});
    const math::Mat4 secondBatchTransformB = math::Mat4::Translation({0.1F, 0.0F, 0.6F});
    Require(batchedScene.CreateInstance(batchedMesh, firstBatchHandle, firstBatchTransformA).IsValid());
    Require(batchedScene.CreateInstance(batchedMesh, firstBatchHandle, firstBatchTransformB).IsValid());
    Require(batchedScene.CreateInstance(batchedMesh, secondBatchHandle, secondBatchTransformA).IsValid());
    Require(batchedScene.CreateInstance(batchedMesh, secondBatchHandle, secondBatchTransformB).IsValid());
    const SceneFrame batchedFrame = batchedScene.BuildFrame(view);
    SubmittedSceneGeometry batchedGeometry;
    Require(BuildSubmittedSceneGeometry(batchedFrame, batchedGeometry, geometryError));
    Require(batchedGeometry.batches.size() == 2U);
    Require(batchedGeometry.meshletBatches.size() == 2U);
    Require(batchedGeometry.meshletDraws.size() == 2U);
    Require(batchedGeometry.meshletIndices.size() == 6U);
    Require(batchedGeometry.meshletHierarchyBatches.size() == 2U);
    Require(batchedGeometry.meshletHierarchyNodes.size() == 2U);
    Require(batchedGeometry.meshletHierarchyBatches[0].rootNode == 0U);
    Require(batchedGeometry.meshletHierarchyBatches[1].rootNode == 1U);
    Require(batchedGeometry.meshletHierarchyMeshlets.size() == 2U);
    Require(batchedGeometry.meshletHierarchyMeshlets[0] == 0U);
    Require(batchedGeometry.meshletHierarchyMeshlets[1] == 1U);

    // A mesh with enough clusters to force multiple hierarchy levels verifies that
    // submitted batch rebasing adjusts both node child ranges and child-node indices.
    MeshDescription hierarchyMeshDescription;
    constexpr std::uint32_t hierarchyTriangleCount = 1100U;
    hierarchyMeshDescription.vertices.reserve(hierarchyTriangleCount * 3U);
    hierarchyMeshDescription.indices.reserve(hierarchyTriangleCount * 3U);
    for (std::uint32_t triangle = 0U; triangle < hierarchyTriangleCount; ++triangle)
    {
        const float x = static_cast<float>(triangle) * 2.0F;
        const std::uint32_t firstVertex = static_cast<std::uint32_t>(hierarchyMeshDescription.vertices.size());
        hierarchyMeshDescription.vertices.push_back({{x, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 0.0F, 0.0F});
        hierarchyMeshDescription.vertices.push_back({{x + 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 1.0F, 0.0F});
        hierarchyMeshDescription.vertices.push_back({{x, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, 0.0F, 1.0F});
        hierarchyMeshDescription.indices.insert(hierarchyMeshDescription.indices.end(),
            {firstVertex, firstVertex + 1U, firstVertex + 2U});
    }
    SceneSubmission hierarchyScene;
    const MeshHandle hierarchyMeshHandle = hierarchyScene.CreateMesh(std::move(hierarchyMeshDescription));
    const MeshHandle hierarchyTailMeshHandle = hierarchyScene.CreateMesh(Triangle());
    const MaterialHandle hierarchyMaterialA = hierarchyScene.CreateMaterial({});
    MaterialDescription hierarchyMaterialBDescription;
    hierarchyMaterialBDescription.baseColor = {0.5F, 0.7F, 0.9F};
    const MaterialHandle hierarchyMaterialB = hierarchyScene.CreateMaterial(hierarchyMaterialBDescription);
    Require(hierarchyMeshHandle.IsValid() && hierarchyTailMeshHandle.IsValid());
    Require(hierarchyMaterialA.IsValid() && hierarchyMaterialB.IsValid());
    Require(hierarchyScene.CreateInstance(hierarchyMeshHandle, hierarchyMaterialA).IsValid());
    Require(hierarchyScene.CreateInstance(hierarchyTailMeshHandle, hierarchyMaterialB).IsValid());
    SubmittedSceneGeometry hierarchySubmitted;
    Require(BuildSubmittedSceneGeometry(hierarchyScene.BuildFrame(view), hierarchySubmitted, geometryError));
    Require(hierarchySubmitted.meshletHierarchyBatches.size() == 2U);
    const SubmittedMeshletHierarchyBatch& firstHierarchyBatch = hierarchySubmitted.meshletHierarchyBatches[0];
    const SubmittedMeshletHierarchyBatch& secondHierarchyBatch = hierarchySubmitted.meshletHierarchyBatches[1];
    Require(firstHierarchyBatch.nodeCount > 1U);
    Require(firstHierarchyBatch.childCount > 0U);
    Require(secondHierarchyBatch.rootNode == firstHierarchyBatch.firstNode + firstHierarchyBatch.nodeCount);
    Require(secondHierarchyBatch.firstChild == firstHierarchyBatch.firstChild + firstHierarchyBatch.childCount);
    const GpuMeshletHierarchyNode& firstHierarchyRoot =
        hierarchySubmitted.meshletHierarchyNodes[firstHierarchyBatch.rootNode];
    Require(firstHierarchyRoot.childRange[0] >= firstHierarchyBatch.firstChild);
    Require(firstHierarchyRoot.childRange[0] + firstHierarchyRoot.childRange[1] <=
        firstHierarchyBatch.firstChild + firstHierarchyBatch.childCount);
    for (std::uint32_t child = 0U; child < firstHierarchyRoot.childRange[1]; ++child)
    {
        const std::uint32_t childNode =
            hierarchySubmitted.meshletHierarchyChildren[firstHierarchyRoot.childRange[0] + child];
        Require(childNode >= firstHierarchyBatch.firstNode);
        Require(childNode < firstHierarchyBatch.firstNode + firstHierarchyBatch.nodeCount);
    }
    Require(hierarchySubmitted.meshletHierarchyMeshlets.back() ==
        hierarchySubmitted.meshletBatches.back().firstDraw);
    Require(batchedGeometry.meshletBatches[0].firstIndex == 0U);
    Require(batchedGeometry.meshletBatches[1].firstIndex == 3U);
    Require(batchedGeometry.meshletIndices[3] >= batchedGeometry.batches[1].firstVertex);
    Require(batchedGeometry.meshletIndices[3] <
        batchedGeometry.batches[1].firstVertex + batchedGeometry.batches[1].vertexCount);
    SubmittedInstanceStreams instanceStreams;
    Require(BuildSubmittedInstanceStreams(batchedGeometry, math::Mat4::Identity(),
        math::Mat4::Identity(), instanceStreams, geometryError));
    Require(instanceStreams.shadowTransforms.size() == 5U);
    Require(instanceStreams.sceneTransforms.size() == 5U);
    Require(instanceStreams.shadowBatches.size() == 2U);
    Require(instanceStreams.sceneBatches.size() == 2U);
    Require(instanceStreams.shadowBatches[0].firstInstance == 1U);
    Require(instanceStreams.shadowBatches[0].instanceCount == 2U);
    Require(instanceStreams.shadowBatches[1].firstInstance == 3U);
    Require(instanceStreams.shadowBatches[1].instanceCount == 2U);
    Require(instanceStreams.sceneBatches[0].firstInstance == 6U);
    Require(instanceStreams.sceneBatches[0].instanceCount == 2U);
    Require(instanceStreams.sceneBatches[1].firstInstance == 8U);
    Require(instanceStreams.sceneBatches[1].instanceCount == 2U);
    Require(instanceStreams.sceneTransforms[1].At(0, 3) == -0.2F);
    Require(instanceStreams.sceneTransforms[2].At(0, 3) == 0.2F);
    Require(instanceStreams.sceneTransforms[3].At(0, 3) == -0.1F);
    Require(instanceStreams.sceneTransforms[4].At(0, 3) == 0.1F);
    const std::size_t retainedShadowCapacity = instanceStreams.shadowTransforms.capacity();
    const std::size_t retainedSceneCapacity = instanceStreams.sceneTransforms.capacity();
    const std::size_t retainedBatchCapacity = instanceStreams.shadowBatches.capacity();
    Require(BuildSubmittedInstanceStreams(batchedGeometry, math::Mat4::Identity(),
        math::Mat4::Identity(), instanceStreams, geometryError));
    Require(instanceStreams.shadowTransforms.capacity() >= retainedShadowCapacity);
    Require(instanceStreams.sceneTransforms.capacity() >= retainedSceneCapacity);
    Require(instanceStreams.shadowBatches.capacity() >= retainedBatchCapacity);
    Require(instanceStreams.shadowTransforms.size() == 5U);
    Require(instanceStreams.sceneTransforms.size() == 5U);
    SubmittedInstanceStreams shadowOnlyStreams;
    Require(BuildSubmittedInstanceStreams(batchedGeometry, math::Mat4::Identity(),
        math::Mat4::Identity(), shadowOnlyStreams, geometryError, false));
    Require(shadowOnlyStreams.shadowTransforms.size() == 5U);
    Require(shadowOnlyStreams.shadowBatches.size() == 2U);
    Require(shadowOnlyStreams.sceneTransforms.empty());
    Require(shadowOnlyStreams.sceneBatches.empty());
    Require(shadowOnlyStreams.shadowBatches[0].firstInstance == 1U);
    Require(shadowOnlyStreams.shadowBatches[0].instanceCount == 2U);
    Require(shadowOnlyStreams.shadowBatches[1].firstInstance == 3U);
    Require(shadowOnlyStreams.shadowBatches[1].instanceCount == 2U);
    auto rebasedSceneBatches = instanceStreams.sceneBatches;
    Require(RebaseSubmittedSceneBatchOffsets(rebasedSceneBatches,
        instanceStreams.shadowTransforms.size(), instanceStreams.shadowTransforms.size() - 1U,
        instanceStreams.sceneTransforms.size(), geometryError));
    Require(rebasedSceneBatches[0].firstInstance == 5U);
    Require(rebasedSceneBatches[1].firstInstance == 7U);
    auto invalidSceneBatches = instanceStreams.sceneBatches;
    invalidSceneBatches[0].firstInstance = 4U;
    Require(!RebaseSubmittedSceneBatchOffsets(invalidSceneBatches,
        instanceStreams.shadowTransforms.size(), instanceStreams.shadowTransforms.size() - 1U,
        instanceStreams.sceneTransforms.size(), geometryError));

    Require(!scene.DestroyMesh(mesh));
    Require(!scene.DestroyMaterial(material));
    const MaterialHandle replacementMaterial = scene.CreateMaterial(
        MaterialDescription{{0.8F, 0.9F, 1.0F}, 0.35F, 0.0F});
    Require(replacementMaterial.IsValid());
    Require(scene.SetResources(instance, mesh, replacementMaterial));
    Require(!scene.DestroyMaterial(replacementMaterial));
    Require(scene.SetResources(instance, mesh, material));
    Require(scene.DestroyMaterial(replacementMaterial));

    Require(scene.SetVisible(instance, false));
    Require(scene.VisibleInstanceCount() == 0U);
    Require(scene.BuildFrame(view).draws.empty());
    Require(scene.SetVisible(instance, true));
    Require(scene.VisibleInstanceCount() == 1U);

    // Visible indices stay dense when visibility changes and instances are destroyed.
    // BuildFrame should only need to visit this compact visible set, not every slot.
    SceneSubmission visibilityScene;
    const MeshHandle visibilityMesh = visibilityScene.CreateMesh(Triangle());
    const MaterialHandle visibilityMaterial = visibilityScene.CreateMaterial({});
    const InstanceHandle visibleA = visibilityScene.CreateInstance(
        visibilityMesh, visibilityMaterial, math::Mat4::Translation({1.0F, 0.0F, 0.0F}));
    const InstanceHandle visibleB = visibilityScene.CreateInstance(
        visibilityMesh, visibilityMaterial, math::Mat4::Translation({2.0F, 0.0F, 0.0F}));
    const InstanceHandle visibleC = visibilityScene.CreateInstance(
        visibilityMesh, visibilityMaterial, math::Mat4::Translation({3.0F, 0.0F, 0.0F}));
    Require(visibleA.IsValid() && visibleB.IsValid() && visibleC.IsValid());
    Require(visibilityScene.VisibleInstanceCount() == 3U);
    Require(visibilityScene.SetVisible(visibleB, false));
    Require(visibilityScene.VisibleInstanceCount() == 2U);
    Require(visibilityScene.BuildFrame(view).draws.size() == 2U);
    Require(visibilityScene.SetVisible(visibleA, false));
    Require(visibilityScene.VisibleInstanceCount() == 1U);
    Require(visibilityScene.BuildFrame(view).draws.size() == 1U);
    Require(visibilityScene.SetVisible(visibleB, true));
    Require(visibilityScene.VisibleInstanceCount() == 2U);
    Require(visibilityScene.BuildFrame(view).draws.size() == 2U);
    Require(visibilityScene.DestroyInstance(visibleC));
    Require(visibilityScene.VisibleInstanceCount() == 1U);
    Require(visibilityScene.BuildFrame(view).draws.size() == 1U);
    Require(visibilityScene.DestroyInstance(visibleB));
    Require(visibilityScene.VisibleInstanceCount() == 0U);
    Require(visibilityScene.BuildFrame(view).draws.empty());
    // Clear also removes hidden live slots from the dense visible index bookkeeping.
    Require(visibilityScene.InstanceCount() == 1U);
    visibilityScene.Clear();
    Require(visibilityScene.InstanceCount() == 0U);
    Require(visibilityScene.VisibleInstanceCount() == 0U);
    Require(!visibilityScene.Contains(visibleA));

    math::Mat4 invalid = math::Mat4::Identity();
    invalid.At(0, 0) = std::numeric_limits<float>::quiet_NaN();
    Require(!scene.SetTransform(instance, invalid));
    Require(!scene.CreateInstance(mesh, material, invalid).IsValid());
    Require(scene.SetTransform(instance, world));

    SceneFrame retained = scene.BuildFrame(view);
    Require(scene.DestroyInstance(instance));
    Require(scene.VisibleInstanceCount() == 0U);
    Require(!scene.Contains(instance));
    Require(!scene.DestroyInstance(instance));
    const InstanceHandle reusedInstance = scene.CreateInstance(mesh, material, world);
    Require(reusedInstance.IsValid() && reusedInstance.index == instance.index &&
            reusedInstance.generation != instance.generation);
    Require(scene.VisibleInstanceCount() == 1U);
    Require(scene.DestroyInstance(reusedInstance));
    Require(scene.VisibleInstanceCount() == 0U);
    Require(scene.DestroyMesh(mesh));
    Require(scene.DestroyMaterial(material));
    Require(!scene.Contains(mesh) && !scene.Contains(material));
    // An in-flight frame retains immutable CPU resources after their handles are destroyed.
    Require(retained.draws.size() == 1U);
    Require(retained.draws[0].mesh->indices.size() == 3U);

    const MeshHandle replacement = scene.CreateMesh(Triangle());
    Require(replacement.IsValid());
    Require(replacement.index == mesh.index);
    Require(replacement.generation != mesh.generation);
    Require(!scene.Contains(mesh));

    SceneSubmission normalScene;
    MeshDescription normalMesh = Triangle();
    for (auto& vertex : normalMesh.vertices)
        vertex.normal = {0.70710678F, 0.70710678F, 0.0F};
    const MeshHandle normalMeshHandle = normalScene.CreateMesh(std::move(normalMesh));
    const MaterialHandle normalMaterialHandle = normalScene.CreateMaterial({});
    const math::Mat4 nonUniformScale = math::Mat4::Scale({2.0F, 1.0F, 1.0F});
    Require(normalScene.CreateInstance(normalMeshHandle, normalMaterialHandle, nonUniformScale).IsValid());
    const SceneFrame normalFrame = normalScene.BuildFrame(view);
    SubmittedSceneGeometry normalGeometry;
    Require(BuildSubmittedSceneGeometry(normalFrame, normalGeometry, geometryError));
    Require(std::abs(normalGeometry.vertices[0].normal[0] - 0.70710678F) < 0.0001F);
    Require(std::abs(normalGeometry.vertices[0].normal[1] - 0.70710678F) < 0.0001F);
    Require(normalGeometry.draws[0].world.At(0, 0) == 2.0F);
    Require(normalGeometry.draws[0].localBoundsMin.x == -1.0F);
    Require(normalGeometry.draws[0].localBoundsMax.x == 1.0F);
    Require(IsSubmittedDrawVisible(normalGeometry.draws[0], math::Mat4::Identity()));
    SubmittedDrawRange outsideFrustum = normalGeometry.draws[0];
    outsideFrustum.world = math::Mat4::Translation({4.0F, 0.0F, 0.0F});
    Require(!IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));
    outsideFrustum.world = math::Mat4::Translation({1.5F, 0.0F, 0.0F});
    Require(IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));
    outsideFrustum.world = math::Mat4::Translation({2.01F, 0.0F, 0.0F});
    Require(!IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));
    outsideFrustum.world = math::Mat4::Translation({0.0F, 0.0F, -4.0F});
    Require(!IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));
    // D3D12 uses a zero-to-one clip-space depth range.
    outsideFrustum.world = math::Mat4::Translation({0.0F, 0.0F, 0.5F});
    Require(IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));
    outsideFrustum.world = math::Mat4::Translation({0.0F, 0.0F, 1.5F});
    Require(!IsSubmittedDrawVisible(outsideFrustum, math::Mat4::Identity()));

    SceneSubmission texturedScene;
    const MeshHandle texturedMesh = texturedScene.CreateMesh(Triangle());
    MaterialDescription texturedDescription;
    const std::filesystem::path textureRoot = std::filesystem::temp_directory_path();
    texturedDescription.baseColorTexture = textureRoot / "albedo.png";
    texturedDescription.normalTexture = textureRoot / "normal.png";
    texturedDescription.roughnessTexture = textureRoot / "roughness.png";
    texturedDescription.metallicTexture = textureRoot / "metallic.png";
    texturedDescription.ambientOcclusionTexture = textureRoot / "ao.png";
    const MaterialHandle texturedMaterial = texturedScene.CreateMaterial(texturedDescription);
    Require(texturedMaterial.IsValid());
    Require(texturedScene.CreateInstance(texturedMesh, texturedMaterial, math::Mat4::Identity()).IsValid());
    // More than the D3D12 2,048-layer array limit worth of draws may reuse one
    // texture set. Geometry must retain every draw while allocating only one layer.
    for (std::uint32_t draw = 0; draw < 2050U; ++draw)
        Require(texturedScene.CreateInstance(texturedMesh, texturedMaterial,
            math::Mat4::Translation({static_cast<float>(draw), 0.0F, 0.0F})).IsValid());
    const SceneFrame texturedFrame = texturedScene.BuildFrame(view);
    SubmittedSceneGeometry texturedGeometry;
    Require(BuildSubmittedSceneGeometry(texturedFrame, texturedGeometry, geometryError));
    Require(texturedGeometry.draws.size() == 2051U);
    Require(texturedGeometry.batches.size() == 1U);
    Require(texturedGeometry.batches[0].drawIndices.size() == 2051U);
    Require(texturedGeometry.batches[0].firstIndex == texturedGeometry.draws[0].firstIndex);
    Require(texturedGeometry.batches[0].indexCount == texturedGeometry.draws[0].indexCount);
    Require(texturedGeometry.vertices.size() == 3U);
    Require(texturedGeometry.indices.size() == 3U);
    Require(texturedGeometry.draws[0].firstVertex == texturedGeometry.draws[2050].firstVertex);
    Require(texturedGeometry.draws[0].firstIndex == texturedGeometry.draws[2050].firstIndex);
    Require(texturedGeometry.draws[2050].world.At(0, 3) == 2049.0F);
    Require(texturedGeometry.materialLayers.size() == 1U);
    Require(texturedGeometry.draws[0].materialLayer == 0U);
    Require(texturedGeometry.draws[2050].materialLayer == 0U);
    Require(texturedGeometry.draws[0].material.normalTexture == textureRoot / "normal.png");
    Require(texturedGeometry.vertices[0].color[3] == 0.0F);
    Require(texturedGeometry.vertices.back().color[3] == 0.0F);
    Require(texturedGeometry.vertices[0].material[3] == 159.0F);

    MaterialDescription packedOrmDescription;
    packedOrmDescription.roughnessTexture = textureRoot / "snow-orm.png";
    packedOrmDescription.ambientOcclusionTexture = textureRoot / "snow-orm.png";
    packedOrmDescription.packedMetallicRoughness = true;
    const MaterialHandle packedOrmMaterial = texturedScene.CreateMaterial(packedOrmDescription);
    Require(packedOrmMaterial.IsValid());
    Require(texturedScene.CreateInstance(texturedMesh, packedOrmMaterial,
        math::Mat4::Identity()).IsValid());
    const SceneFrame packedOrmFrame = texturedScene.BuildFrame(view);
    SubmittedSceneGeometry packedOrmGeometry;
    Require(BuildSubmittedSceneGeometry(packedOrmFrame, packedOrmGeometry, geometryError));
    Require(packedOrmGeometry.draws.size() == 2052U);
    Require(packedOrmGeometry.batches.size() == 2U);
    Require(packedOrmGeometry.batches[0].drawIndices.size() == 2051U);
    Require(packedOrmGeometry.batches[1].drawIndices.size() == 1U);
    Require(packedOrmGeometry.materialLayers.size() == 2U);
    Require(packedOrmGeometry.draws.back().materialLayer == 1U);
    Require(packedOrmGeometry.vertices.size() == 6U);
    Require(packedOrmGeometry.indices.size() == 6U);
    const std::size_t packedFirstVertex = 3U;
    Require(packedOrmGeometry.vertices[packedFirstVertex].material[3] == 180.0F);
    Require(packedOrmGeometry.draws.back().material.packedMetallicRoughness);

    MaterialDescription snowTerrainDescription;
    snowTerrainDescription.baseColorTexture = textureRoot / "snow-albedo.png";
    snowTerrainDescription.normalTexture = textureRoot / "snow-normal.png";
    snowTerrainDescription.terrain = true;
    snowTerrainDescription.snowSurface = true;
    const MaterialHandle snowTerrainMaterial = texturedScene.CreateMaterial(snowTerrainDescription);
    Require(snowTerrainMaterial.IsValid());
    Require(texturedScene.CreateInstance(texturedMesh, snowTerrainMaterial,
        math::Mat4::Identity()).IsValid());
    const SceneFrame snowTerrainFrame = texturedScene.BuildFrame(view);
    SubmittedSceneGeometry snowTerrainGeometry;
    Require(BuildSubmittedSceneGeometry(snowTerrainFrame, snowTerrainGeometry, geometryError));
    const auto snowTerrainFlags = static_cast<std::uint32_t>(
        snowTerrainGeometry.vertices.back().material[3]);
    Require((snowTerrainFlags & 64U) != 0U);
    Require((snowTerrainFlags & 128U) != 0U);
    Require((snowTerrainFlags & 256U) != 0U);

    SceneSubmission advancedMaterialScene;
    const MeshHandle advancedMesh = advancedMaterialScene.CreateMesh(Triangle());
    MaterialDescription advancedMaterialDescription;
    advancedMaterialDescription.emissive = {1.4F, 0.25F, 0.05F};
    advancedMaterialDescription.normalScale = 0.65F;
    advancedMaterialDescription.occlusionStrength = 0.35F;
    advancedMaterialDescription.alphaMode = MaterialAlphaMode::Mask;
    advancedMaterialDescription.alphaCutoff = 0.42F;
    advancedMaterialDescription.doubleSided = true;
    const MaterialHandle advancedMaterial = advancedMaterialScene.CreateMaterial(advancedMaterialDescription);
    Require(advancedMesh.IsValid() && advancedMaterial.IsValid());
    Require(advancedMaterialScene.CreateInstance(advancedMesh, advancedMaterial).IsValid());
    const SceneFrame advancedFrame = advancedMaterialScene.BuildFrame(view);
    SubmittedSceneGeometry advancedGeometry;
    Require(BuildSubmittedSceneGeometry(advancedFrame, advancedGeometry, geometryError));
    Require(advancedGeometry.vertices.size() == 3U);
    Require(advancedGeometry.vertices[0].materialExtra0[0] == 1.4F);
    Require(advancedGeometry.vertices[0].materialExtra0[1] == 0.25F);
    Require(advancedGeometry.vertices[0].materialExtra0[2] == 0.05F);
    Require(advancedGeometry.vertices[0].materialExtra0[3] == 0.65F);
    Require(advancedGeometry.vertices[0].materialExtra1[0] == 0.35F);
    Require(advancedGeometry.vertices[0].materialExtra1[1] == 0.42F);
    Require(advancedGeometry.vertices[0].materialExtra1[2] ==
        static_cast<float>(MaterialAlphaMode::Mask));
    Require(advancedGeometry.vertices[0].materialExtra1[3] == 1.0F);

    MaterialDescription blendedMaterialDescription;
    blendedMaterialDescription.alphaMode = MaterialAlphaMode::Blend;
    const MaterialHandle blendedMaterial = advancedMaterialScene.CreateMaterial(blendedMaterialDescription);
    Require(blendedMaterial.IsValid());
    Require(advancedMaterialScene.CreateInstance(advancedMesh, blendedMaterial).IsValid());
    const SceneFrame blendedFrame = advancedMaterialScene.BuildFrame(view);
    SubmittedSceneGeometry blendedGeometry;
    Require(BuildSubmittedSceneGeometry(blendedFrame, blendedGeometry, geometryError));
    Require(blendedGeometry.vertices.back().materialExtra1[2] ==
        static_cast<float>(MaterialAlphaMode::Blend));
    MaterialDescription emissiveTextureDescription;
    emissiveTextureDescription.emissiveTexture = textureRoot / "emissive.png";
    const MaterialHandle emissiveTextureMaterial = advancedMaterialScene.CreateMaterial(emissiveTextureDescription);
    Require(emissiveTextureMaterial.IsValid());
    Require(advancedMaterialScene.CreateInstance(advancedMesh, emissiveTextureMaterial).IsValid());
    const SceneFrame emissiveFrame = advancedMaterialScene.BuildFrame(view);
    SubmittedSceneGeometry emissiveGeometry;
    Require(BuildSubmittedSceneGeometry(emissiveFrame, emissiveGeometry, geometryError));
    Require((static_cast<std::uint32_t>(emissiveGeometry.vertices.back().material[3]) & 512U) != 0U);

    SceneSubmission cachedScene;
    const MeshHandle cachedMesh = cachedScene.CreateMesh(Triangle());
    const MaterialHandle cachedMaterial = cachedScene.CreateMaterial({});
    const InstanceHandle cachedInstance = cachedScene.CreateInstance(
        cachedMesh, cachedMaterial, math::Mat4::Identity());
    Require(cachedInstance.IsValid());
    SubmittedSceneGeometryCache geometryCache;
    const SceneFrame cachedFrameA = cachedScene.BuildFrame(view);
    Require(BuildSubmittedSceneGeometry(cachedFrameA, geometryCache, geometryError));
    const std::uint64_t cachedRevision = geometryCache.revision;
    const auto* cachedVertexStorage = geometryCache.geometry.vertices.data();
    const math::Mat4 movedTransform = math::Mat4::Translation({12.0F, 3.0F, -7.0F});
    Require(cachedScene.SetTransform(cachedInstance, movedTransform));
    const SceneFrame cachedFrameB = cachedScene.BuildFrame(view);
    Require(BuildSubmittedSceneGeometry(cachedFrameB, geometryCache, geometryError));
    Require(geometryCache.revision == cachedRevision);
    Require(geometryCache.geometry.vertices.data() == cachedVertexStorage);
    Require(geometryCache.geometry.draws[0].world.At(0, 3) == 12.0F);
    Require(geometryCache.geometry.draws[0].world.At(1, 3) == 3.0F);
    Require(geometryCache.geometry.draws[0].world.At(2, 3) == -7.0F);
    Require(geometryCache.geometry.batches.size() == 1U);
    Require(geometryCache.geometry.batches[0].drawIndices.size() == 1U);
    Require(geometryCache.geometry.meshletHierarchyBatches.size() == 1U);
    Require(geometryCache.geometry.meshletHierarchyNodes.size() == 1U);
    Require(geometryCache.geometry.meshletHierarchyBatches[0].rootNode == 0U);
    Require(geometryCache.geometry.meshletHierarchyMeshlets[0] == 0U);

    // Changing only the visible instance set must not rebuild immutable mesh data.
    const InstanceHandle cachedInstanceB = cachedScene.CreateInstance(
        cachedMesh, cachedMaterial, math::Mat4::Translation({24.0F, 0.0F, 0.0F}));
    Require(cachedInstanceB.IsValid());
    const SceneFrame bothCachedFrame = cachedScene.BuildFrame(view);
    Require(bothCachedFrame.resourceRevision == cachedFrameB.resourceRevision);
    Require(BuildSubmittedSceneGeometry(bothCachedFrame, geometryCache, geometryError));
    const std::uint64_t visibilityCacheRevision = geometryCache.revision;
    const auto* visibilityVertexStorage = geometryCache.geometry.vertices.data();
    const auto* visibilityIndexStorage = geometryCache.geometry.indices.data();
    Require(cachedScene.SetVisible(cachedInstance, false));
    const SceneFrame oneVisibleFrame = cachedScene.BuildFrame(view);
    Require(oneVisibleFrame.draws.size() == 1U);
    Require(BuildSubmittedSceneGeometry(oneVisibleFrame, geometryCache, geometryError));
    Require(geometryCache.revision == visibilityCacheRevision);
    Require(geometryCache.geometry.vertices.data() == visibilityVertexStorage);
    Require(geometryCache.geometry.indices.data() == visibilityIndexStorage);
    Require(geometryCache.geometry.draws.size() == 1U);
    Require(geometryCache.geometry.batches.size() == 1U);
    Require(geometryCache.geometry.batches[0].drawIndices.size() == 1U);
    Require(geometryCache.geometry.batches[0].drawIndices[0] == 0U);
    Require(geometryCache.geometry.draws[0].world.At(0, 3) == 24.0F);

    Require(cachedScene.SetVisible(cachedInstance, true));
    Require(cachedScene.SetVisible(cachedInstanceB, false));
    const SceneFrame swappedVisibleFrame = cachedScene.BuildFrame(view);
    Require(swappedVisibleFrame.draws.size() == 1U);
    Require(BuildSubmittedSceneGeometry(swappedVisibleFrame, geometryCache, geometryError));
    Require(geometryCache.revision == visibilityCacheRevision);
    Require(geometryCache.geometry.vertices.data() == visibilityVertexStorage);
    Require(geometryCache.geometry.indices.data() == visibilityIndexStorage);
    Require(geometryCache.geometry.draws[0].world.At(0, 3) == 12.0F);

    // Creating a mesh/material changes the resource revision and must invalidate old static data.
    const MeshHandle addedMesh = cachedScene.CreateMesh(Triangle());
    MaterialDescription addedMaterialDescription;
    addedMaterialDescription.baseColor = {0.7F, 0.8F, 0.9F};
    const MaterialHandle addedMaterial = cachedScene.CreateMaterial(addedMaterialDescription);
    const InstanceHandle addedInstance = cachedScene.CreateInstance(
        addedMesh, addedMaterial, math::Mat4::Translation({36.0F, 0.0F, 0.0F}));
    Require(addedMesh.IsValid() && addedMaterial.IsValid() && addedInstance.IsValid());
    const std::uint64_t previousResourceCacheRevision = geometryCache.revision;
    const SceneFrame expandedResourceFrame = cachedScene.BuildFrame(view);
    Require(BuildSubmittedSceneGeometry(expandedResourceFrame, geometryCache, geometryError));
    Require(geometryCache.revision != previousResourceCacheRevision);
    Require(geometryCache.geometry.vertices.size() == 6U);
    Require(geometryCache.geometry.indices.size() == 6U);
    Require(geometryCache.geometry.batches.size() == 2U);
    Require(geometryCache.geometry.draws.size() == 2U);
    Require(geometryCache.geometry.batches[0].drawIndices.size() == 1U);
    Require(geometryCache.geometry.batches[1].drawIndices.size() == 1U);
    Require(geometryCache.geometry.meshletHierarchyBatches.size() == 2U);
    Require(geometryCache.geometry.meshletHierarchyNodes.size() == 2U);
    Require(geometryCache.geometry.meshletHierarchyBatches[0].rootNode == 0U);
    Require(geometryCache.geometry.meshletHierarchyBatches[1].rootNode == 1U);
    Require(geometryCache.geometry.meshletHierarchyMeshlets.size() == 2U);
    Require(geometryCache.geometry.meshletHierarchyMeshlets[0] == 0U);
    Require(geometryCache.geometry.meshletHierarchyMeshlets[1] == 1U);

    Require(cachedScene.DestroyInstance(addedInstance));
    Require(cachedScene.DestroyMesh(addedMesh));
    Require(cachedScene.DestroyMaterial(addedMaterial));
    const std::uint64_t beforeDestroyCacheRevision = geometryCache.revision;
    const SceneFrame reducedResourceFrame = cachedScene.BuildFrame(view);
    Require(BuildSubmittedSceneGeometry(reducedResourceFrame, geometryCache, geometryError));
    Require(geometryCache.revision != beforeDestroyCacheRevision);
    Require(geometryCache.geometry.vertices.size() == 3U);
    Require(geometryCache.geometry.indices.size() == 3U);
    Require(geometryCache.geometry.batches.size() == 1U);
    Require(geometryCache.geometry.draws.size() == 1U);

    MaterialDescription unresolvedTextureDescription;
    unresolvedTextureDescription.normalTexture = "textures/normal.png";
    Require(!texturedScene.CreateMaterial(unresolvedTextureDescription).IsValid());

    const MaterialHandle invalidMaterial = scene.CreateMaterial(
        MaterialDescription{{1.0F, 1.0F, 1.0F}, 2.0F, 0.0F});
    Require(!invalidMaterial.IsValid());
    const MaterialHandle negativeColor = scene.CreateMaterial(
        MaterialDescription{{-0.1F, 0.5F, 0.5F}, 0.5F, 0.0F});
    Require(!negativeColor.IsValid());

    MeshDescription zeroNormalMesh = Triangle();
    zeroNormalMesh.vertices[0].normal = {};
    Require(!scene.CreateMesh(std::move(zeroNormalMesh)).IsValid());

    MeshDescription badMesh = Triangle();
    badMesh.indices[2] = 99U;
    Require(!scene.CreateMesh(std::move(badMesh)).IsValid());

    scene.Clear();
    Require(scene.MeshCount() == 0U && scene.MaterialCount() == 0U &&
           scene.InstanceCount() == 0U && scene.VisibleInstanceCount() == 0U);
    Require(!scene.Contains(replacement));
    return 0;
}
