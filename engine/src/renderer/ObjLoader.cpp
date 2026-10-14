#include "vanta/renderer/ObjLoader.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    std::stringstream stream(token);
    std::string part;
    if (!std::getline(stream, part, '/') || part.empty()) return false;
    try { result.position = std::stoi(part); } catch (...) { return false; }
    if (std::getline(stream, part, '/')) {
        if (!part.empty()) { try { result.texcoord = std::stoi(part); } catch (...) { return false; } }
        if (std::getline(stream, part, '/')) {
            if (!part.empty()) { try { result.normal = std::stoi(part); } catch (...) { return false; } }
        }
    }
    return result.position != 0;
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
            if (tag == "map_Kd") {
                std::string texture;
                if (row >> texture) model.diffuseTexture = materialLibrary.parent_path() / texture;
                break;
            }
        }
    }

    output = std::move(model);
    error.clear();
    return true;
}
} // namespace vanta::renderer
