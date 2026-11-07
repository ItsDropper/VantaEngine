#include "vanta/renderer/SceneSubmission.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

using namespace vanta;
using namespace vanta::renderer;

namespace
{
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
    assert(mesh.IsValid() && scene.Contains(mesh));
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

    assert(!scene.DestroyMesh(mesh));
    assert(!scene.DestroyMaterial(material));
    assert(scene.SetVisible(instance, false));
    assert(scene.BuildFrame(view).draws.empty());
    assert(scene.SetVisible(instance, true));

    math::Mat4 invalid = math::Mat4::Identity();
    invalid.At(0, 0) = std::numeric_limits<float>::quiet_NaN();
    assert(!scene.SetTransform(instance, invalid));
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

    const MaterialHandle invalidMaterial = scene.CreateMaterial(
        MaterialDescription{{1.0F, 1.0F, 1.0F}, 2.0F, 0.0F});
    assert(!invalidMaterial.IsValid());

    MeshDescription badMesh = Triangle();
    badMesh.indices[2] = 99U;
    assert(!scene.CreateMesh(std::move(badMesh)).IsValid());

    scene.Clear();
    assert(scene.MeshCount() == 0U && scene.MaterialCount() == 0U &&
           scene.InstanceCount() == 0U);
    assert(!scene.Contains(replacement));
    return 0;
}
