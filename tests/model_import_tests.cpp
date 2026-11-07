#include "vanta/assets/ObjLoader.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
int checks = 0;

void Check(bool condition, const char* name)
{
    ++checks;
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
        material << "newmtl test_material\n"
                    << "Kd 0.8 0.7 0.6\n"
                    << "Pr 0.3\n"
                    << "Pm 0.2\n"
                    << "map_Kd sample_diffuse.bmp\n"
                    << "map_Bump sample_normal.png\n"
                    << "map_Pr sample_roughness.png\n"
                    << "map_Pm sample_metallic.png\n"
                    << "map_AO sample_ao.png\n";
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
                "usemtl test_material\n"
                "f 1/1 2/2 3/3 4/4\n";
    }

    vanta::assets::CpuModel model;
    std::string error;
    Check(vanta::assets::LoadWavefrontObj(path, model, error), "loads a Wavefront OBJ quad");
    Check(model.vertices.size() == 6 && model.indices.size() == 6, "triangulates a quad into two triangles");
    Check(model.vertices.size() >= 3 && std::abs(model.vertices[0].normal.z - 1.0F) < 0.001F,
        "generates a face normal when OBJ normals are absent");
    Check(model.vertices.size() >= 3 && std::abs(model.vertices[1].u - 1.0F) < 0.001F,
        "preserves texture coordinates");
    Check(model.materialLibraries.size() == 1 && model.materialLibraries.front().filename() == "vanta_obj_import_test.mtl",
        "records the referenced MTL source dependency");
    Check(model.diffuseTexture.filename() == "sample_diffuse.bmp",
        "resolves the MTL diffuse texture relative to the material file");
    Check(std::abs(model.baseColor.x - 0.8F) < 0.001F && std::abs(model.baseColor.y - 0.7F) < 0.001F,
        "imports MTL base-color factors");
    Check(std::abs(model.roughness - 0.3F) < 0.001F && std::abs(model.metallic - 0.2F) < 0.001F,
        "imports PBR roughness and metallic factors");
    Check(model.normalTexture.filename() == "sample_normal.png" &&
          model.roughnessTexture.filename() == "sample_roughness.png" &&
          model.metallicTexture.filename() == "sample_metallic.png",
        "resolves normal, roughness, and metallic map paths");
    Check(model.ambientOcclusionTexture.filename() == "sample_ao.png",
        "resolves the ambient-occlusion map path");
    Check(model.materials.size() == 2U && model.materials[1].name == "test_material",
        "retains named MTL materials");
    Check(model.submeshes.size() == 1U && model.submeshes[0].materialIndex == 1U &&
          model.submeshes[0].indexCount == 6U,
        "assigns OBJ faces to their named material submesh");
    std::filesystem::remove(path);
    std::filesystem::remove(materialPath);

    const auto invalidPath = std::filesystem::temp_directory_path() / "vanta_obj_import_invalid.obj";
    {
        std::ofstream file(invalidPath);
        file << "v 0 0 0\nf 1 2 3\n";
    }
    Check(!vanta::assets::LoadWavefrontObj(invalidPath, model, error),
        "rejects face indices outside the vertex array");
    Check(!error.empty(), "reports a useful import error");
    std::filesystem::remove(invalidPath);

    const auto multiObj = std::filesystem::temp_directory_path() / "vanta_obj_multi_material.obj";
    const auto firstMtl = multiObj.parent_path() / "vanta_obj_snow.mtl";
    const auto secondMtl = multiObj.parent_path() / "vanta_obj_bark.mtl";
    {
        std::ofstream first(firstMtl);
        first << "newmtl snow\nKd 0.9 0.95 1.0\nPr 0.92\n";
        std::ofstream second(secondMtl);
        second << "newmtl bark\nKd 0.3 0.18 0.08\nPm 0.05\n";
        std::ofstream object(multiObj);
        object << "mtllib vanta_obj_snow.mtl vanta_obj_bark.mtl\n"
                  "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
                  "usemtl snow\nf 1 2 3\n"
                  "usemtl bark\nf 2 4 3\n";
    }
    Check(vanta::assets::LoadWavefrontObj(multiObj, model, error),
        "loads multiple OBJ material libraries");
    Check(model.materialLibraries.size() == 2U && model.materials.size() == 3U,
        "retains both MTL dependencies and all named materials");
    Check(model.submeshes.size() == 2U && model.submeshes[0].materialIndex == 1U &&
          model.submeshes[1].materialIndex == 2U,
        "splits geometry into submeshes with the correct material assignments");
    Check(model.materials[1].name == "snow" && model.materials[2].name == "bark" &&
          std::abs(model.materials[1].baseColor.x - 0.9F) < 0.001F &&
          std::abs(model.materials[2].baseColor.x - 0.3F) < 0.001F,
        "keeps material properties isolated across MTL libraries");
    {
        std::ofstream object(multiObj, std::ios::trunc);
        object << "mtllib vanta_obj_snow.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                  "usemtl missing\nf 1 2 3\n";
    }
    Check(!vanta::assets::LoadWavefrontObj(multiObj, model, error) &&
          error.find("unknown material") != std::string::npos,
        "rejects OBJ references to undefined materials");
    std::filesystem::remove(multiObj);
    std::filesystem::remove(firstMtl);
    std::filesystem::remove(secondMtl);

    const auto malformedIndexPath = std::filesystem::temp_directory_path() / "vanta_obj_import_malformed_index.obj";
    {
        std::ofstream file(malformedIndexPath);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1junk 2 3\n";
    }
    Check(!vanta::assets::LoadWavefrontObj(malformedIndexPath, model, error),
        "rejects partially numeric face indices");
    {
        std::ofstream file(malformedIndexPath, std::ios::trunc);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1/1/1 2 3\n";
    }
    Check(!vanta::assets::LoadWavefrontObj(malformedIndexPath, model, error),
        "rejects face indices with excess slash components");
    std::filesystem::remove(malformedIndexPath);

    const auto negativeIndexPath = std::filesystem::temp_directory_path() / "vanta_obj_import_negative_index.obj";
    {
        std::ofstream file(negativeIndexPath);
        file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf -3//1 -2//1 -1//1\n";
    }
    Check(vanta::assets::LoadWavefrontObj(negativeIndexPath, model, error) &&
          model.indices.size() == 3 && std::abs(model.vertices[0].normal.z - 1.0F) < 0.001F,
        "supports negative OBJ indices and omitted texture-coordinate fields");
    std::filesystem::remove(negativeIndexPath);
    const auto missingMtlPath = std::filesystem::temp_directory_path() / "vanta_obj_import_missing_mtl.obj";
    {
        std::ofstream file(missingMtlPath);
        file << "mtllib absent_materials.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    Check(!vanta::assets::LoadWavefrontObj(missingMtlPath, model, error) &&
          error.find("Could not open referenced MTL") != std::string::npos,
        "fails when a declared MTL sidecar is missing");
    std::filesystem::remove(missingMtlPath);
    std::cout << (16 - failures) << "/16 model import tests passed.\n";
    return failures == 0 ? 0 : 1;
}
