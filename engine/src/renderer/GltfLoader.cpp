#include "vanta/renderer/GltfLoader.hpp"

#define CGLTF_IMPLEMENTATION
#include "../../third_party/cgltf.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace vanta::renderer
{
namespace
{
const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == 0)
            return primitive.attributes[i].data;
    return nullptr;
}

bool ExternalTexturePath(const cgltf_texture_view& view, const std::filesystem::path& modelPath,
                         std::filesystem::path& output, std::string& error, const char* label)
{
    if (!view.texture || !view.texture->image) return true;
    const cgltf_image& image = *view.texture->image;
    if (image.uri && image.uri[0] != '\0')
    {
        const std::string uri = image.uri;
        if (uri.rfind("data:", 0) == 0)
        {
            error = std::string("Embedded data-URI image for ") + label +
                    " is not supported yet; export the texture as an external image.";
            return false;
        }
        output = modelPath.parent_path() / std::filesystem::u8path(uri);
        return true;
    }
    if (image.buffer_view)
    {
        error = std::string("Embedded GLB image for ") + label +
                " is not supported yet; export the texture as an external image.";
        return false;
    }
    error = std::string("Texture image for ") + label + " has no readable URI.";
    return false;
}

bool ConvertMaterial(const cgltf_material* source, const std::filesystem::path& modelPath,
                     CpuMaterial& target, std::string& error)
{
    target = CpuMaterial{};
    if (!source) return true;
    if (source->has_pbr_metallic_roughness)
    {
        const auto& pbr = source->pbr_metallic_roughness;
        target.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
        target.roughness = std::clamp(static_cast<float>(pbr.roughness_factor), 0.04F, 1.0F);
        target.metallic = std::clamp(static_cast<float>(pbr.metallic_factor), 0.0F, 1.0F);
        if (!ExternalTexturePath(pbr.base_color_texture, modelPath, target.diffuseTexture, error, "base color") ||
            !ExternalTexturePath(pbr.metallic_roughness_texture, modelPath, target.roughnessTexture, error, "metallic-roughness"))
            return false;
        target.metallicTexture = target.roughnessTexture;
        target.packedMetallicRoughness = !target.roughnessTexture.empty();
    }
    if (!ExternalTexturePath(source->normal_texture, modelPath, target.normalTexture, error, "normal") ||
        !ExternalTexturePath(source->occlusion_texture, modelPath, target.ambientOcclusionTexture, error, "occlusion"))
        return false;
    return true;
}

math::Vec3 TransformPoint(const float matrix[16], const math::Vec3& p)
{
    return {
        matrix[0] * p.x + matrix[4] * p.y + matrix[8] * p.z + matrix[12],
        matrix[1] * p.x + matrix[5] * p.y + matrix[9] * p.z + matrix[13],
        matrix[2] * p.x + matrix[6] * p.y + matrix[10] * p.z + matrix[14]
    };
}

math::Vec3 TransformNormal(const float matrix[16], const math::Vec3& n)
{
    return math::Vec3{
        matrix[0] * n.x + matrix[4] * n.y + matrix[8] * n.z,
        matrix[1] * n.x + matrix[5] * n.y + matrix[9] * n.z,
        matrix[2] * n.x + matrix[6] * n.y + matrix[10] * n.z
    }.Normalized().value_or(math::Vec3{0.0F, 1.0F, 0.0F});
}

struct ImportContext
{
    const cgltf_data& data;
    const std::filesystem::path& path;
    CpuModel& model;
    std::vector<bool>& generatedNormals;
    std::string& error;
};

bool AppendMesh(const cgltf_mesh& mesh, const float world[16], ImportContext& context)
{
    for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
    {
        const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles)
        {
            context.error = "Only triangle-list glTF primitives are supported.";
            return false;
        }
        const cgltf_accessor* positions = FindAttribute(primitive, cgltf_attribute_type_position);
        const cgltf_accessor* normals = FindAttribute(primitive, cgltf_attribute_type_normal);
        const cgltf_accessor* texcoords = FindAttribute(primitive, cgltf_attribute_type_texcoord);
        if (!positions || positions->type != cgltf_type_vec3 || positions->count == 0)
        {
            context.error = "glTF primitive is missing a valid VEC3 POSITION accessor.";
            return false;
        }
        if (positions->count > static_cast<cgltf_size>(std::numeric_limits<std::uint32_t>::max()) ||
            context.model.vertices.size() + positions->count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            context.error = "glTF mesh exceeds the engine's 32-bit vertex/index limit.";
            return false;
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(context.model.vertices.size());
        const std::uint32_t firstIndex = static_cast<std::uint32_t>(context.model.indices.size());
        for (cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex)
        {
            float positionData[3]{};
            if (!cgltf_accessor_read_float(positions, vertexIndex, positionData, 3))
            {
                context.error = "Could not read a glTF position accessor.";
                return false;
            }
            ModelVertex vertex{};
            vertex.position = TransformPoint(world, {positionData[0], positionData[1], positionData[2]});
            if (normals && normals->type == cgltf_type_vec3)
            {
                float normalData[3]{};
                if (!cgltf_accessor_read_float(normals, vertexIndex, normalData, 3))
                {
                    context.error = "Could not read a glTF normal accessor.";
                    return false;
                }
                vertex.normal = TransformNormal(world, {normalData[0], normalData[1], normalData[2]});
                context.generatedNormals.push_back(false);
            }
            else
            {
                vertex.normal = {0.0F, 0.0F, 0.0F};
                context.generatedNormals.push_back(true);
            }
            if (texcoords && texcoords->type == cgltf_type_vec2)
            {
                float uvData[2]{};
                if (!cgltf_accessor_read_float(texcoords, vertexIndex, uvData, 2))
                {
                    context.error = "Could not read a glTF TEXCOORD_0 accessor.";
                    return false;
                }
                vertex.u = uvData[0];
                vertex.v = 1.0F - uvData[1];
            }
            context.model.vertices.push_back(vertex);
        }

        const cgltf_size elementCount = primitive.indices ? primitive.indices->count : positions->count;
        if (elementCount == 0 || elementCount % 3 != 0 ||
            context.model.indices.size() + elementCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            context.error = "glTF triangle primitive has an invalid index count.";
            return false;
        }
        for (cgltf_size i = 0; i < elementCount; ++i)
        {
            const cgltf_size localIndex = primitive.indices ? cgltf_accessor_read_index(primitive.indices, i) : i;
            if (localIndex >= positions->count)
            {
                context.error = "glTF index references a vertex outside its POSITION accessor.";
                return false;
            }
            context.model.indices.push_back(baseVertex + static_cast<std::uint32_t>(localIndex));
        }

        std::uint32_t materialIndex = 0;
        if (primitive.material)
        {
            const auto found = std::find_if(context.data.materials, context.data.materials + context.data.materials_count,
                [&](const cgltf_material& material) { return &material == primitive.material; });
            if (found == context.data.materials + context.data.materials_count)
            {
                context.error = "glTF primitive references a material outside the document.";
                return false;
            }
            materialIndex = static_cast<std::uint32_t>(std::distance(context.data.materials, found)) + 1U;
        }
        context.model.submeshes.push_back({firstIndex,
            static_cast<std::uint32_t>(context.model.indices.size() - firstIndex), materialIndex});
    }
    return true;
}

bool AppendNode(cgltf_node* node, ImportContext& context)
{
    if (node->mesh)
    {
        float world[16]{};
        cgltf_node_transform_world(node, world);
        if (!AppendMesh(*node->mesh, world, context)) return false;
    }
    for (cgltf_size i = 0; i < node->children_count; ++i)
        if (!AppendNode(node->children[i], context)) return false;
    return true;
}

void GenerateMissingNormals(CpuModel& model, const std::vector<bool>& generated)
{
    for (std::size_t i = 0; i + 2 < model.indices.size(); i += 3)
    {
        const std::uint32_t ia = model.indices[i], ib = model.indices[i + 1], ic = model.indices[i + 2];
        const auto& a = model.vertices[ia].position;
        const auto& b = model.vertices[ib].position;
        const auto& c = model.vertices[ic].position;
        const math::Vec3 face = (b - a).Cross(c - a);
        if (generated[ia]) model.vertices[ia].normal = model.vertices[ia].normal + face;
        if (generated[ib]) model.vertices[ib].normal = model.vertices[ib].normal + face;
        if (generated[ic]) model.vertices[ic].normal = model.vertices[ic].normal + face;
    }
    for (std::size_t i = 0; i < model.vertices.size(); ++i)
        if (generated[i])
            model.vertices[i].normal = model.vertices[i].normal.Normalized().value_or(math::Vec3{0.0F, 1.0F, 0.0F});
}
} // namespace

bool LoadGltf(const std::filesystem::path& path, CpuModel& output, std::string& error)
{
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    const std::string nativePath = path.string();
    const cgltf_result parseResult = cgltf_parse_file(&options, nativePath.c_str(), &rawData);
    if (parseResult != cgltf_result_success || !rawData)
    {
        error = "Could not parse glTF/GLB asset: " + path.string();
        return false;
    }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), nativePath.c_str()) != cgltf_result_success)
    {
        error = "Could not load glTF buffer data for: " + path.string();
        return false;
    }
    if (cgltf_validate(data.get()) != cgltf_result_success)
    {
        error = "glTF validation failed for: " + path.string();
        return false;
    }

    CpuModel model;
    model.materials.reserve(data->materials_count + 1);
    model.materials.push_back(CpuMaterial{}); // glTF's implicit default material
    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        CpuMaterial material;
        if (!ConvertMaterial(&data->materials[i], path, material, error)) return false;
        model.materials.push_back(std::move(material));
    }

    std::vector<bool> generatedNormals;
    ImportContext context{*data, path, model, generatedNormals, error};
    cgltf_scene* scene = data->scene;
    if (!scene && data->scenes_count > 0) scene = &data->scenes[0];
    if (scene)
    {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            if (!AppendNode(scene->nodes[i], context)) return false;
    }
    else
    {
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (cgltf_size i = 0; i < data->meshes_count; ++i)
            if (!AppendMesh(data->meshes[i], identity, context)) return false;
    }

    if (model.vertices.empty() || model.indices.empty() || model.submeshes.empty())
    {
        error = "glTF asset contains no renderable triangle geometry.";
        return false;
    }
    GenerateMissingNormals(model, generatedNormals);
    model.baseColor = model.materials[0].baseColor;
    model.roughness = model.materials[0].roughness;
    model.metallic = model.materials[0].metallic;
    output = std::move(model);
    error.clear();
    return true;
}
} // namespace vanta::renderer
