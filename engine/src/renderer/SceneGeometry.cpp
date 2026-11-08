#include "vanta/renderer/SceneGeometry.hpp"

#include <array>

#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace vanta::renderer
{
namespace
{
std::string MaterialTextureKey(const MaterialDescription& material)
{
    const std::array<const std::filesystem::path*, 5> paths = {
        &material.baseColorTexture, &material.normalTexture, &material.roughnessTexture,
        &material.metallicTexture, &material.ambientOcclusionTexture
    };
    std::string key;
    for (const std::filesystem::path* path : paths)
    {
        const std::string value = path->generic_string();
        key += std::to_string(value.size());
        key.push_back(':');
        key += value;
    }
    return key;
}

bool TransformNormal(const math::Mat4& world, const math::Vec3& normal, math::Vec3& output) noexcept
{
    const float a = world.At(0, 0), b = world.At(0, 1), c = world.At(0, 2);
    const float d = world.At(1, 0), e = world.At(1, 1), f = world.At(1, 2);
    const float g = world.At(2, 0), h = world.At(2, 1), i = world.At(2, 2);
    const float c00 = e * i - f * h;
    const float c01 = f * g - d * i;
    const float c02 = d * h - e * g;
    const float c10 = c * h - b * i;
    const float c11 = a * i - c * g;
    const float c12 = b * g - a * h;
    const float c20 = b * f - c * e;
    const float c21 = c * d - a * f;
    const float c22 = a * e - b * d;
    const float determinant = a * c00 + b * c01 + c * c02;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8F)
        return false;

    const float inverseDeterminant = 1.0F / determinant;
    const math::Vec3 transformed{
        (c00 * normal.x + c01 * normal.y + c02 * normal.z) * inverseDeterminant,
        (c10 * normal.x + c11 * normal.y + c12 * normal.z) * inverseDeterminant,
        (c20 * normal.x + c21 * normal.y + c22 * normal.z) * inverseDeterminant
    };
    const auto normalized = transformed.Normalized();
    if (!normalized)
        return false;
    output = *normalized;
    return true;
}
} // namespace

bool BuildSubmittedSceneGeometry(const SceneFrame& frame, SubmittedSceneGeometry& output, std::string& error)
{
    output = {};
    struct Key { const MeshResource* mesh; const MaterialResource* material; bool operator==(const Key&) const noexcept = default; };
    struct Hash { std::size_t operator()(const Key& k) const noexcept { auto a=std::hash<const void*>{}(k.mesh),b=std::hash<const void*>{}(k.material);return a^(b+0x9e3779b9U+(a<<6U)+(a>>2U)); } };
    std::unordered_map<Key,std::pair<std::uint32_t,std::uint32_t>,Hash> cachedGeometry;
    std::unordered_map<std::string,std::uint32_t> layers;
    cachedGeometry.reserve(frame.draws.size()); layers.reserve(frame.draws.size()); output.draws.reserve(frame.draws.size());
    for(const DrawItem& draw:frame.draws) {
        if(!draw.mesh||!draw.material){error="Scene frame contains a draw with a missing mesh or material.";output={};return false;}
        if(draw.mesh->vertices.empty()||draw.mesh->indices.empty()||draw.mesh->indices.size()%3U){error="Scene frame contains invalid indexed triangle geometry.";output={};return false;}
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(!std::isfinite(draw.world.At(r,c))){error="Submitted instance has a non-finite world transform.";output={};return false;}
        const auto& w=draw.world;
        const float det=w.At(0,0)*(w.At(1,1)*w.At(2,2)-w.At(1,2)*w.At(2,1))-w.At(0,1)*(w.At(1,0)*w.At(2,2)-w.At(1,2)*w.At(2,0))+w.At(0,2)*(w.At(1,0)*w.At(2,1)-w.At(1,1)*w.At(2,0));
        if(!std::isfinite(det)||std::abs(det)<=1.0e-8F||std::abs(w.At(3,0))>1.0e-6F||std::abs(w.At(3,1))>1.0e-6F||std::abs(w.At(3,2))>1.0e-6F||std::abs(w.At(3,3)-1.0F)>1.0e-6F){error="Submitted instance has a singular or non-affine world transform.";output={};return false;}
        SubmittedDrawRange range;range.world=w;range.material=draw.material->description;
        const auto [layer,inserted]=layers.emplace(MaterialTextureKey(range.material),static_cast<std::uint32_t>(output.materialLayers.size()));
        if(inserted)output.materialLayers.push_back(range.material);
        range.materialLayer=layer->second;
        const Key key{draw.mesh.get(),draw.material.get()};const auto cached=cachedGeometry.find(key);
        if(cached!=cachedGeometry.end()){range.firstVertex=cached->second.first;range.vertexCount=static_cast<std::uint32_t>(draw.mesh->vertices.size());range.firstIndex=cached->second.second;range.indexCount=static_cast<std::uint32_t>(draw.mesh->indices.size());output.draws.push_back(std::move(range));continue;}
        if(draw.mesh->vertices.size()>std::numeric_limits<std::uint32_t>::max()-output.vertices.size()||draw.mesh->indices.size()>std::numeric_limits<std::uint32_t>::max()-output.indices.size()){error="Submitted scene exceeds the 32-bit draw range limits.";output={};return false;}
        range.firstVertex=static_cast<std::uint32_t>(output.vertices.size());range.vertexCount=static_cast<std::uint32_t>(draw.mesh->vertices.size());range.firstIndex=static_cast<std::uint32_t>(output.indices.size());range.indexCount=static_cast<std::uint32_t>(draw.mesh->indices.size());
        std::uint32_t flags=0; if(!range.material.baseColorTexture.empty())flags|=1U;if(!range.material.normalTexture.empty())flags|=2U;if(!range.material.roughnessTexture.empty())flags|=4U;if(!range.material.metallicTexture.empty())flags|=8U;if(!range.material.ambientOcclusionTexture.empty())flags|=16U;if(range.material.packedMetallicRoughness)flags|=32U;if(flags)flags|=128U;
        for(const assets::ModelVertex& src:draw.mesh->vertices){const float v[]={src.position.x,src.position.y,src.position.z,src.normal.x,src.normal.y,src.normal.z,src.u,src.v};for(float x:v)if(!std::isfinite(x)){error="Submitted mesh contains a non-finite vertex attribute.";output={};return false;}
            SubmittedSceneVertex out;out.position[0]=src.position.x;out.position[1]=src.position.y;out.position[2]=src.position.z;out.normal[0]=src.normal.x;out.normal[1]=src.normal.y;out.normal[2]=src.normal.z;out.color[0]=range.material.baseColor.x;out.color[1]=range.material.baseColor.y;out.color[2]=range.material.baseColor.z;out.color[3]=static_cast<float>(range.materialLayer);out.uv[0]=src.u;out.uv[1]=src.v;out.material[0]=range.material.roughness;out.material[1]=range.material.metallic;out.material[2]=1.0F;out.material[3]=static_cast<float>(flags);output.vertices.push_back(out);}
        for(auto index:draw.mesh->indices){if(index>=range.vertexCount){error="Submitted mesh contains an out-of-range index.";output={};return false;}output.indices.push_back(index);}
        cachedGeometry.emplace(key,std::pair{range.firstVertex,range.firstIndex});output.draws.push_back(std::move(range));
    }
    error.clear();return true;
}
} // namespace vanta::renderer
