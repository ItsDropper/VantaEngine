#include "vanta/renderer/ObjLoader.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void Check(bool condition, const char* name)
{
    if (condition) std::cout << "PASS: " << name << '\n';
    else { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "vanta_obj_import_test.obj";
    const auto materialPath = path.parent_path() / "vanta_obj_import_test.mtl";
    {
        std::ofstream material(materialPath);
        material << "newmtl test_material\nmap_Kd sample_diffuse.bmp\n";
    }
    {
        std::ofstream file(path);
        file << "mtllib vanta_obj_import_test.mtl\n"
                "# quad with UVs and no normals\n"
                "v 0 0 0\n"
                "v 1 0 0\n"
                "v 1 1 0\n"
                "v 0 1 0\n"
                "vt 0 0\n"
                "vt 1 0\n"
                "vt 1 1\n"
                "vt 0 1\n"
                "f 1/1 2/2 3/3 4/4\n";
    }

    vanta::renderer::CpuModel model;
    std::string error;
    Check(vanta::renderer::LoadWavefrontObj(path, model, error), "loads a Wavefront OBJ quad");
    Check(model.vertices.size() == 6 && model.indices.size() == 6, "triangulates a quad into two triangles");
    Check(model.vertices.size() >= 3 && std::abs(model.vertices[0].normal.z - 1.0F) < 0.001F,
        "generates a face normal when OBJ normals are absent");
    Check(model.vertices.size() >= 3 && std::abs(model.vertices[1].u - 1.0F) < 0.001F,
        "preserves texture coordinates");
    Check(model.diffuseTexture.filename() == "sample_diffuse.bmp",
        "resolves the MTL diffuse texture relative to the material file");
    std::filesystem::remove(path);
    std::filesystem::remove(materialPath);

    const auto invalidPath = std::filesystem::temp_directory_path() / "vanta_obj_import_invalid.obj";
    {
        std::ofstream file(invalidPath);
        file << "v 0 0 0\nf 1 2 3\n";
    }
    Check(!vanta::renderer::LoadWavefrontObj(invalidPath, model, error),
        "rejects face indices outside the vertex array");
    Check(!error.empty(), "reports a useful import error");
    std::filesystem::remove(invalidPath);

    std::cout << (5 - failures) << "/5 model import tests passed.\n";
    return failures == 0 ? 0 : 1;
}
