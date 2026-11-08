#include "vanta/renderer/SceneSubmission.hpp"
#include "vanta/renderer/SceneGeometry.hpp"

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
    Require(submitted.draws.size() == 1U);
    Require(submitted.vertices[0].position[0] == -1.0F);
    Require(submitted.vertices[0].position[1] == 0.0F);
    Require(submitted.vertices[0].position[2] == 0.0F);
    Require(submitted.draws[0].world.At(0, 3) == 3.0F);
    Require(submitted.vertices[0].normal[1] == 1.0F);
    Require(submitted.vertices[0].color[0] == 1.0F);
    Require(submitted.vertices[0].material[0] == 0.8F);

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
