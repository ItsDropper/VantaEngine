#include "vanta/assets/ObjLoader.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vanta::assets
{
namespace
{
struct FaceIndex
{
    int position = 0;
    int texcoord = 0;
    int normal = 0;
};

struct PendingSubmesh
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::string materialName;
};

std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1U);
}

bool ParseIndex(const std::string& token, FaceIndex& result)
{
    const auto parseComponent = [](const std::string_view value, int& output)
    {
        if (value.empty())
            return false;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && output != 0;
    };

    const std::size_t firstSlash = token.find('/');
    const std::string_view position(token.data(),
        firstSlash == std::string::npos ? token.size() : firstSlash);
    if (!parseComponent(position, result.position))
        return false;
    if (firstSlash == std::string::npos)
        return true;

    const std::size_t secondSlash = token.find('/', firstSlash + 1U);
    if (secondSlash == std::string::npos)
        return parseComponent(std::string_view(token).substr(firstSlash + 1U), result.texcoord);
    if (token.find('/', secondSlash + 1U) != std::string::npos)
        return false;

    const std::string_view texcoord(token.data() + firstSlash + 1U,
                                    secondSlash - firstSlash - 1U);
    const std::string_view normal(token.data() + secondSlash + 1U,
                                  token.size() - secondSlash - 1U);
    if (!texcoord.empty() && !parseComponent(texcoord, result.texcoord))
        return false;
    return parseComponent(normal, result.normal);
}

template <typename T>
bool ResolveIndex(int index, const std::vector<T>& values, const T*& resolved)
{
    const long long actual = index > 0 ? static_cast<long long>(index) - 1
        : static_cast<long long>(values.size()) + index;
    if (actual < 0 || actual >= static_cast<long long>(values.size()))
        return false;
    resolved = &values[static_cast<std::size_t>(actual)];
    return true;
}

bool ParseMaterialLibrary(const std::filesystem::path& path, CpuModel& model,
                          std::unordered_map<std::string, std::uint32_t>& materialIndices,
                          std::string& error)
{
    std::ifstream input(path);
    if (!input)
    {
        error = "Could not open referenced MTL library: " + path.string();
        return false;
    }

    std::optional<std::uint32_t> currentMaterial;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag.empty() || tag.front() == '#')
            continue;

        if (tag == "newmtl")
        {
            std::string name;
            std::getline(row, name);
            name = Trim(std::move(name));
            if (name.empty())
            {
                error = "Empty newmtl name in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
            if (materialIndices.contains(name))
            {
                error = "Duplicate MTL material name '" + name + "' in " + path.string();
                return false;
            }
            const auto index = static_cast<std::uint32_t>(model.materials.size());
            CpuMaterial material;
            material.name = name;
            model.materials.push_back(std::move(material));
            materialIndices.emplace(std::move(name), index);
            currentMaterial = index;
            continue;
        }

        if (!currentMaterial)
        {
            error = "MTL property appears before newmtl in " + path.string() +
                    " at line " + std::to_string(lineNumber);
            return false;
        }
        CpuMaterial& material = model.materials[*currentMaterial];
        if (tag == "Kd")
        {
            if (!(row >> material.baseColor.x >> material.baseColor.y >> material.baseColor.z) ||
                !std::isfinite(material.baseColor.x) || !std::isfinite(material.baseColor.y) ||
                !std::isfinite(material.baseColor.z))
            {
                error = "Invalid MTL base color in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
        }
        else if (tag == "Pr")
        {
            if (!(row >> material.roughness) || !std::isfinite(material.roughness))
            {
                error = "Invalid MTL roughness in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
            material.roughness = std::clamp(material.roughness, 0.04F, 1.0F);
        }
        else if (tag == "Pm")
        {
            if (!(row >> material.metallic) || !std::isfinite(material.metallic))
            {
                error = "Invalid MTL metallic value in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
            material.metallic = std::clamp(material.metallic, 0.0F, 1.0F);
        }
        else if (tag == "Ns")
        {
            float shininess = 0.0F;
            if (!(row >> shininess) || !std::isfinite(shininess) || shininess < 0.0F)
            {
                error = "Invalid MTL shininess in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
            material.roughness = std::clamp(std::sqrt(2.0F / (shininess + 2.0F)), 0.04F, 1.0F);
        }
        else if (tag == "map_Kd" || tag == "map_Bump" || tag == "bump" || tag == "norm" ||
                 tag == "map_Pr" || tag == "map_Pm" || tag == "map_AO" ||
                 tag == "map_ao" || tag == "map_Ka")
        {
            std::string token;
            std::string filename;
            while (row >> token)
            {
                if (!token.empty() && token.front() == '#')
                    break;
                filename = token;
            }
            if (filename.empty())
            {
                error = "Empty MTL texture map in " + path.string() + " at line " + std::to_string(lineNumber);
                return false;
            }
            const auto texturePath = path.parent_path() / std::filesystem::path(filename);
            if (tag == "map_Kd") material.diffuseTexture = texturePath;
            else if (tag == "map_Bump" || tag == "bump" || tag == "norm") material.normalTexture = texturePath;
            else if (tag == "map_Pr") material.roughnessTexture = texturePath;
            else if (tag == "map_Pm") material.metallicTexture = texturePath;
            else material.ambientOcclusionTexture = texturePath;
        }
    }
    if (!input.eof())
    {
        error = "I/O error while reading MTL library: " + path.string();
        return false;
    }
    return true;
}
} // namespace

bool LoadWavefrontObj(const std::filesystem::path& path, CpuModel& output, std::string& error)
{
    std::ifstream file(path);
    if (!file)
    {
        error = "Could not open model: " + path.string();
        return false;
    }

    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    struct Texcoord { float u; float v; };
    std::vector<Texcoord> texcoords;
    CpuModel model;
    std::vector<PendingSubmesh> pendingSubmeshes;
    std::string activeMaterial;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag.empty() || tag.front() == '#')
            continue;

        if (tag == "v")
        {
            math::Vec3 value;
            if (!(row >> value.x >> value.y >> value.z) ||
                !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            {
                error = "Invalid vertex at line " + std::to_string(lineNumber);
                return false;
            }
            positions.push_back(value);
        }
        else if (tag == "vn")
        {
            math::Vec3 value;
            if (!(row >> value.x >> value.y >> value.z) ||
                !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            {
                error = "Invalid normal at line " + std::to_string(lineNumber);
                return false;
            }
            normals.push_back(value);
        }
        else if (tag == "vt")
        {
            Texcoord value{};
            if (!(row >> value.u >> value.v) || !std::isfinite(value.u) || !std::isfinite(value.v))
            {
                error = "Invalid UV coordinate at line " + std::to_string(lineNumber);
                return false;
            }
            texcoords.push_back(value);
        }
        else if (tag == "mtllib")
        {
            std::string name;
            while (row >> name)
            {
                if (!name.empty() && name.front() == '#')
                    break;
                const auto library = path.parent_path() / std::filesystem::path(name);
                if (std::find(model.materialLibraries.begin(), model.materialLibraries.end(), library) ==
                    model.materialLibraries.end())
                    model.materialLibraries.push_back(library);
            }
            if (model.materialLibraries.empty())
            {
                error = "MTL library declaration is empty at line " + std::to_string(lineNumber);
                return false;
            }
        }
        else if (tag == "usemtl")
        {
            std::getline(row, activeMaterial);
            activeMaterial = Trim(std::move(activeMaterial));
            const std::size_t comment = activeMaterial.find('#');
            if (comment != std::string::npos)
                activeMaterial = Trim(activeMaterial.substr(0, comment));
            if (activeMaterial.empty())
            {
                error = "Empty usemtl name at line " + std::to_string(lineNumber);
                return false;
            }
        }
        else if (tag == "f")
        {
            std::vector<FaceIndex> face;
            std::string token;
            while (row >> token)
            {
                if (!token.empty() && token.front() == '#')
                    break;
                FaceIndex index;
                if (!ParseIndex(token, index))
                {
                    error = "Invalid face index at line " + std::to_string(lineNumber);
                    return false;
                }
                face.push_back(index);
            }
            if (face.size() < 3U)
            {
                error = "Face has fewer than three vertices at line " + std::to_string(lineNumber);
                return false;
            }
            if (model.vertices.size() + (face.size() - 2U) * 3U >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                error = "OBJ mesh exceeds the engine's 32-bit vertex/index limit.";
                return false;
            }

            const std::uint32_t firstIndex = static_cast<std::uint32_t>(model.indices.size());
            for (std::size_t triangle = 1; triangle + 1U < face.size(); ++triangle)
            {
                const FaceIndex corners[] = {face[0], face[triangle], face[triangle + 1U]};
                const std::size_t base = model.vertices.size();
                math::Vec3 points[3]{};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const math::Vec3* position = nullptr;
                    if (!ResolveIndex(corners[corner].position, positions, position))
                    {
                        error = "Position index out of range at line " + std::to_string(lineNumber);
                        return false;
                    }
                    points[corner] = *position;
                }
                const auto generated = (points[1] - points[0]).Cross(points[2] - points[0]).Normalized()
                    .value_or(math::Vec3{0.0F, 1.0F, 0.0F});

                for (int corner = 0; corner < 3; ++corner)
                {
                    ModelVertex vertex{};
                    vertex.position = points[corner];
                    vertex.normal = generated;
                    if (corners[corner].normal != 0)
                    {
                        const math::Vec3* normal = nullptr;
                        if (!ResolveIndex(corners[corner].normal, normals, normal))
                        {
                            error = "Normal index out of range at line " + std::to_string(lineNumber);
                            return false;
                        }
                        vertex.normal = normal->Normalized().value_or(generated);
                    }
                    if (corners[corner].texcoord != 0)
                    {
                        const Texcoord* uv = nullptr;
                        if (!ResolveIndex(corners[corner].texcoord, texcoords, uv))
                        {
                            error = "UV index out of range at line " + std::to_string(lineNumber);
                            return false;
                        }
                        vertex.u = uv->u;
                        vertex.v = 1.0F - uv->v;
                    }
                    model.vertices.push_back(vertex);
                    model.indices.push_back(static_cast<unsigned int>(base + static_cast<std::size_t>(corner)));
                }
            }
            const auto indexCount = static_cast<std::uint32_t>(model.indices.size()) - firstIndex;
            if (!pendingSubmeshes.empty() && pendingSubmeshes.back().materialName == activeMaterial &&
                pendingSubmeshes.back().firstIndex + pendingSubmeshes.back().indexCount == firstIndex)
                pendingSubmeshes.back().indexCount += indexCount;
            else
                pendingSubmeshes.push_back({firstIndex, indexCount, activeMaterial});
        }
    }
    if (!file.eof())
    {
        error = "I/O error while reading OBJ model: " + path.string();
        return false;
    }
    if (model.indices.empty())
    {
        error = "Model contains no valid triangle faces.";
        return false;
    }

    model.materials.push_back(CpuMaterial{});
    std::unordered_map<std::string, std::uint32_t> materialIndices;
    for (const auto& library : model.materialLibraries)
        if (!ParseMaterialLibrary(library, model, materialIndices, error))
            return false;

    model.submeshes.reserve(pendingSubmeshes.size());
    for (const PendingSubmesh& pending : pendingSubmeshes)
    {
        std::uint32_t materialIndex = 0U;
        if (!pending.materialName.empty())
        {
            const auto found = materialIndices.find(pending.materialName);
            if (found == materialIndices.end())
            {
                error = "OBJ usemtl references an unknown material: " + pending.materialName;
                return false;
            }
            materialIndex = found->second;
        }
        model.submeshes.push_back({pending.firstIndex, pending.indexCount, materialIndex});
    }

    if (model.materials.size() == 2U)
    {
        const CpuMaterial& material = model.materials[1];
        model.baseColor = material.baseColor;
        model.roughness = material.roughness;
        model.metallic = material.metallic;
        model.diffuseTexture = material.diffuseTexture;
        model.normalTexture = material.normalTexture;
        model.roughnessTexture = material.roughnessTexture;
        model.metallicTexture = material.metallicTexture;
        model.ambientOcclusionTexture = material.ambientOcclusionTexture;
    }

    output = std::move(model);
    error.clear();
    return true;
}
} // namespace vanta::assets
