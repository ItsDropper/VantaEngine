#include "vanta/renderer/SceneSubmission.hpp"
#include "vanta/renderer/SceneGeometry.hpp"

#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

using namespace vanta;
using namespace vanta::renderer;

namespace
{

void Require(const bool condition)
{
    if (!condition)
        std::abort();
}

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
    assert(material.IsValid() && scene.Contains(material));

    const math::Mat4 world = math::Mat4::Translation({3.0F, 2.0F, 1.0F}) *
                             math::Mat4::Scale({2.0F, 2.0F, 2.0F});
    const InstanceHandle instance = scene.CreateInstance(mesh, material, world);
    assert(instance.IsValid() && scene.Contains(instance));
    assert(scene.MeshCount() == 1U && scene.MaterialCount() == 1U &&
           scene.InstanceCount() == 1U);

    FrameView view;
    view.cameraPosition = {0.0F, 1.0F, -5.0F};
    view.frameNumber = 42U;
    const SceneFrame frame = scene.BuildFrame(view);
    assert(frame.view.frameNumber == 42U);
    assert(frame.draws.size() == 1U);
    assert(frame.draws[0].mesh->indices.size() == 3U);
    assert(frame.draws[0].material->description.roughness == 0.8F);
    assert(frame.draws[0].world.At(0, 3) == 3.0F);

    SubmittedSceneGeometry submitted;
    std::string geometryError;
    assert(BuildSubmittedSceneGeometry(frame, submitted, geometryError));
    assert(geometryError.empty());
    assert(submitted.vertices.size() == 3U && submitted.indices.size() == 3U);
    assert(submitted.draws.size() == 1U);
    assert(submitted.vertices[0].position[0] == 1.0F);
    assert(submitted.vertices[0].position[1] == 2.0F);
    assert(submitted.vertices[0].position[2] == 1.0F);
    assert(submitted.vertices[0].normal[1] == 1.0F);
    assert(submitted.vertices[0].color[0] == 1.0F);
    assert(submitted.vertices[0].material[0] == 0.8F);

    assert(!scene.DestroyMesh(mesh));
    assert(!scene.DestroyMaterial(material));
    const MaterialHandle replacementMaterial = scene.CreateMaterial(
        MaterialDescription{{0.8F, 0.9F, 1.0F}, 0.35F, 0.0F});
    assert(replacementMaterial.IsValid());
    assert(scene.SetResources(instance, mesh, replacementMaterial));
    assert(!scene.DestroyMaterial(replacementMaterial));
    assert(scene.SetResources(instance, mesh, material));
    assert(scene.DestroyMaterial(replacementMaterial));

    assert(scene.SetVisible(instance, false));
    assert(scene.BuildFrame(view).draws.empty());
    assert(scene.SetVisible(instance, true));

    math::Mat4 invalid = math::Mat4::Identity();
    invalid.At(0, 0) = std::numeric_limits<float>::quiet_NaN();
    assert(!scene.SetTransform(instance, invalid));
    assert(!scene.CreateInstance(mesh, material, invalid).IsValid());
    assert(scene.SetTransform(instance, world));

    SceneFrame retained = scene.BuildFrame(view);
    assert(scene.DestroyInstance(instance));
    assert(!scene.Contains(instance));
    assert(!scene.DestroyInstance(instance));
    assert(scene.DestroyMesh(mesh));
    assert(scene.DestroyMaterial(material));
    assert(!scene.Contains(mesh) && !scene.Contains(material));
    // An in-flight frame retains immutable CPU resources after their handles are destroyed.
    assert(retained.draws.size() == 1U);
    assert(retained.draws[0].mesh->indices.size() == 3U);

    const MeshHandle replacement = scene.CreateMesh(Triangle());
    assert(replacement.IsValid());
    assert(replacement.index == mesh.index);
    assert(replacement.generation != mesh.generation);
    assert(!scene.Contains(mesh));

    SceneSubmission normalScene;
    MeshDescription normalMesh = Triangle();
    for (auto& vertex : normalMesh.vertices)
        vertex.normal = {0.70710678F, 0.70710678F, 0.0F};
    const MeshHandle normalMeshHandle = normalScene.CreateMesh(std::move(normalMesh));
    const MaterialHandle normalMaterialHandle = normalScene.CreateMaterial({});
    const math::Mat4 nonUniformScale = math::Mat4::Scale({2.0F, 1.0F, 1.0F});
    assert(normalScene.CreateInstance(normalMeshHandle, normalMaterialHandle, nonUniformScale).IsValid());
    const SceneFrame normalFrame = normalScene.BuildFrame(view);
    SubmittedSceneGeometry normalGeometry;
    assert(BuildSubmittedSceneGeometry(normalFrame, normalGeometry, geometryError));
    assert(std::abs(normalGeometry.vertices[0].normal[0] - 0.4472136F) < 0.0001F);
    assert(std::abs(normalGeometry.vertices[0].normal[1] - 0.8944272F) < 0.0001F);

    const MaterialHandle invalidTextureMaterial = scene.CreateMaterial(
        MaterialDescription{{1.0F, 1.0F, 1.0F}, 0.5F, 0.0F, "albedo.png"});
    assert(!invalidTextureMaterial.IsValid());

    const MaterialHandle invalidMaterial = scene.CreateMaterial(
        MaterialDescription{{1.0F, 1.0F, 1.0F}, 2.0F, 0.0F});
    assert(!invalidMaterial.IsValid());
    const MaterialHandle negativeColor = scene.CreateMaterial(
        MaterialDescription{{-0.1F, 0.5F, 0.5F}, 0.5F, 0.0F});
    assert(!negativeColor.IsValid());

    MeshDescription zeroNormalMesh = Triangle();
    zeroNormalMesh.vertices[0].normal = {};
    assert(!scene.CreateMesh(std::move(zeroNormalMesh)).IsValid());

    MeshDescription badMesh = Triangle();
    badMesh.indices[2] = 99U;
    assert(!scene.CreateMesh(std::move(badMesh)).IsValid());

    scene.Clear();
    assert(scene.MeshCount() == 0U && scene.MaterialCount() == 0U &&
           scene.InstanceCount() == 0U);
    assert(!scene.Contains(replacement));
    return 0;
}
