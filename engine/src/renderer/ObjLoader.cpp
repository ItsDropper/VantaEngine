#include "vanta/renderer/ObjLoader.hpp"

#include <fstream>
#include <charconv>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace vanta::renderer
{
namespace
{
struct FaceIndex
{
    int position = 0;
    int texcoord = 0;
    int normal = 0;
};

bool ParseIndex(const std::string& token, FaceIndex& result)
{
    const auto parseComponent = [](const std::string_view value, int& output) {
        if (value.empty())
            return false;
        const char* const begin = value.data();
        const char* const end = begin + value.size();
        const auto parsed = std::from_chars(begin, end, output);
        return parsed.ec == std::errc{} && parsed.ptr == end && output != 0;
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
    {
        const std::string_view texcoord(token.data() + firstSlash + 1U,
                                        token.size() - firstSlash - 1U);
        return parseComponent(texcoord, result.texcoord);
    }

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
    if (actual < 0 || actual >= static_cast<long long>(values.size())) return false;
    resolved = &values[static_cast<std::size_t>(actual)];
    return true;
}
} // namespace

bool LoadWavefrontObj(const std::filesystem::path& path, CpuModel& output, std::string& error)
{
    std::ifstream file(path);
    if (!file) { error = "Could not open model: " + path.string(); return false; }

    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    struct Texcoord { float u; float v; };
    std::vector<Texcoord> texcoords;
    CpuModel model;
    std::filesystem::path materialLibrary;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag.empty() || tag[0] == '#') continue;

        if (tag == "v") {
            math::Vec3 value;
            if (!(row >> value.x >> value.y >> value.z)) {
                error = "Invalid vertex at line " + std::to_string(lineNumber); return false;
            }
            positions.push_back(value);
        } else if (tag == "vn") {
            math::Vec3 value;
            if (!(row >> value.x >> value.y >> value.z)) {
                error = "Invalid normal at line " + std::to_string(lineNumber); return false;
            }
            normals.push_back(value);
        } else if (tag == "vt") {
            Texcoord value{};
            if (!(row >> value.u >> value.v)) {
                error = "Invalid UV coordinate at line " + std::to_string(lineNumber); return false;
            }
            texcoords.push_back(value);
        } else if (tag == "mtllib") {
            std::string name;
            if (row >> name) materialLibrary = path.parent_path() / std::filesystem::path(name);
        } else if (tag == "f") {
            std::vector<FaceIndex> face;
            std::string token;
            while (row >> token) {
                FaceIndex index;
                if (!ParseIndex(token, index)) {
                    error = "Invalid face index at line " + std::to_string(lineNumber); return false;
                }
                face.push_back(index);
            }
            if (face.size() < 3) {
                error = "Face has fewer than three vertices at line " + std::to_string(lineNumber); return false;
            }

            for (std::size_t triangle = 1; triangle + 1 < face.size(); ++triangle) {
                const FaceIndex corners[] = {face[0], face[triangle], face[triangle + 1]};
                const std::size_t base = model.vertices.size();
                math::Vec3 points[3]{};
                for (int corner = 0; corner < 3; ++corner) {
                    const math::Vec3* position = nullptr;
                    if (!ResolveIndex(corners[corner].position, positions, position)) {
                        error = "Position index out of range at line " + std::to_string(lineNumber); return false;
                    }
                    points[corner] = *position;
                }
                const auto generated = (points[1] - points[0]).Cross(points[2] - points[0]).Normalized()
                    .value_or(math::Vec3{0.0F, 1.0F, 0.0F});

                for (int corner = 0; corner < 3; ++corner) {
                    ModelVertex vertex{};
                    vertex.position = points[corner];
                    vertex.normal = generated;
                    if (corners[corner].normal != 0) {
                        const math::Vec3* normal = nullptr;
                        if (!ResolveIndex(corners[corner].normal, normals, normal)) {
                            error = "Normal index out of range at line " + std::to_string(lineNumber); return false;
                        }
                        vertex.normal = normal->Normalized().value_or(generated);
                    }
                    if (corners[corner].texcoord != 0) {
                        const Texcoord* uv = nullptr;
                        if (!ResolveIndex(corners[corner].texcoord, texcoords, uv)) {
                            error = "UV index out of range at line " + std::to_string(lineNumber); return false;
                        }
                        vertex.u = uv->u;
                        vertex.v = 1.0F - uv->v;
                    }
                    model.vertices.push_back(vertex);
                    model.indices.push_back(static_cast<unsigned int>(base + static_cast<std::size_t>(corner)));
                }
            }
        }
    }

    if (model.indices.empty()) { error = "Model contains no valid triangle faces."; return false; }

    if (!materialLibrary.empty()) {
        std::ifstream materialFile(materialLibrary);
        std::string materialLine;
        while (std::getline(materialFile, materialLine)) {
            std::istringstream row(materialLine);
            std::string tag;
            row >> tag;
            if (tag.empty() || tag[0] == '#') continue;

            if (tag == "Kd") {
                if (!(row >> model.baseColor.x >> model.baseColor.y >> model.baseColor.z)) {
                    error = "Invalid MTL base color in " + materialLibrary.string();
                    return false;
                }
            } else if (tag == "Pr") {
                if (!(row >> model.roughness)) {
                    error = "Invalid MTL roughness in " + materialLibrary.string();
                    return false;
                }
            } else if (tag == "Pm") {
                if (!(row >> model.metallic)) {
                    error = "Invalid MTL metallic value in " + materialLibrary.string();
                    return false;
                }
            } else if (tag == "Ns") {
                float shininess = 0.0F;
                if (row >> shininess) {
                    // Convert legacy Phong shininess into a roughness approximation.
                    model.roughness = std::sqrt(2.0F / (std::max(shininess, 0.0F) + 2.0F));
                }
            } else if (tag == "map_Kd" || tag == "map_Bump" || tag == "bump" || tag == "norm" ||
                       tag == "map_Pr" || tag == "map_Pm" || tag == "map_AO" || tag == "map_ao" || tag == "map_Ka") {
                // MTL map options can precede the filename. The final token is the most
                // useful portable interpretation for common exports (including Blender).
                std::string token;
                std::string filename;
                while (row >> token) filename = token;
                if (filename.empty()) continue;
                const auto texturePath = materialLibrary.parent_path() / filename;
                if (tag == "map_Kd") model.diffuseTexture = texturePath;
                else if (tag == "map_Bump" || tag == "bump" || tag == "norm") model.normalTexture = texturePath;
                else if (tag == "map_Pr") model.roughnessTexture = texturePath;
                else if (tag == "map_Pm") model.metallicTexture = texturePath;
                else if (tag == "map_AO" || tag == "map_ao" || tag == "map_Ka") model.ambientOcclusionTexture = texturePath;
            }
        }
        model.roughness = std::clamp(model.roughness, 0.04F, 1.0F);
        model.metallic = std::clamp(model.metallic, 0.0F, 1.0F);
    }

    output = std::move(model);
    error.clear();
    return true;
}
} // namespace vanta::renderer
