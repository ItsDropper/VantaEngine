#include "vanta/assets/GltfLoader.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    const auto path = std::filesystem::temp_directory_path() / "vanta_gltf_import_test.gltf";
    {
        std::ofstream file(path, std::ios::binary);
        file << R"GLTF({
  "asset":{"version":"2.0","generator":"VantaEngine importer test"},
  "scene":0,
  "scenes":[{"nodes":[0]}],
  "nodes":[{"mesh":0,"translation":[2,0,0],"scale":[2,1,0.5]}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
  "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.8,0.7,0.6,1.0],"baseColorTexture":{"index":0},"metallicFactor":0.25,"roughnessFactor":0.45,"metallicRoughnessTexture":{"index":1}}}],
  "textures":[{"source":0},{"source":1}],
  "images":[{"uri":"albedo%20map.png"},{"uri":"packed_mr.png"}],
  "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAAAACAPwAAgD8AAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AAABAAIA","byteLength":102}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":6}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
  ]
})GLTF";
    }

    vanta::assets::CpuModel model;
    std::string error;
    const bool loaded = vanta::assets::LoadGltf(path, model, error);
    Check(loaded, "loads a glTF 2.0 scene with an embedded geometry buffer");
    if (!loaded) std::cerr << error << '\n';
    Check(model.vertices.size() == 3 && model.indices.size() == 3,
        "imports indexed triangle geometry");
    Check(model.submeshes.size() == 1 && model.submeshes[0].indexCount == 3 &&
          model.submeshes[0].materialIndex == 1,
        "retains primitive index ranges and material assignments");
    Check(model.materials.size() == 2 &&
          std::abs(model.materials[1].baseColor.x - 0.8F) < 0.001F &&
          std::abs(model.materials[1].roughness - 0.45F) < 0.001F &&
          std::abs(model.materials[1].metallic - 0.25F) < 0.001F,
        "imports glTF metallic-roughness factors and base color");
    Check(model.materials[1].diffuseTexture.filename() == "albedo map.png" &&
          model.materials[1].packedMetallicRoughness &&
          model.materials[1].roughnessTexture.filename() == "packed_mr.png" &&
          model.materials[1].metallicTexture.filename() == "packed_mr.png",
        "resolves external PBR textures and packed metallic-roughness channels");
    Check(std::abs(model.vertices[0].position.x - 2.0F) < 0.001F,
        "applies node translation transforms");
    Check(std::abs(model.vertices[0].normal.x - 0.4472136F) < 0.001F &&
          std::abs(model.vertices[0].normal.y - 0.8944272F) < 0.001F,
        "transforms normals with the inverse-transpose under non-uniform scale");


    const auto singularPath = std::filesystem::temp_directory_path() / "vanta_gltf_import_singular.gltf";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
                        const std::string scale = R"json("scale":[2,1,0.5])json";
        const auto scalePosition = document.find(scale);
        if (scalePosition != std::string::npos)
                                    document.replace(scalePosition, scale.size(), R"json("scale":[0,1,1])json");
        std::ofstream output(singularPath, std::ios::binary);
        output << document;
    }
    Check(!vanta::assets::LoadGltf(singularPath, model, error),
        "rejects singular node transforms when imported normals cannot be transformed");
    std::filesystem::remove(singularPath);

    const auto mirroredPath = std::filesystem::temp_directory_path() / "vanta_gltf_import_mirrored.gltf";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
                        const std::string scale = R"json("scale":[2,1,0.5])json";
        const auto scalePosition = document.find(scale);
        if (scalePosition != std::string::npos)
                                    document.replace(scalePosition, scale.size(), R"json("scale":[-2,1,0.5])json");
        std::ofstream output(mirroredPath, std::ios::binary);
        output << document;
    }
    Check(vanta::assets::LoadGltf(mirroredPath, model, error) &&
          model.indices.size() == 3 && model.indices[0] == 0 &&
          model.indices[1] == 2 && model.indices[2] == 1 &&
          std::abs(model.vertices[0].normal.x + 0.4472136F) < 0.001F &&
          std::abs(model.vertices[0].normal.y - 0.8944272F) < 0.001F,
        "preserves front-face winding and inverse-transpose normals for mirrored nodes");
    std::filesystem::remove(mirroredPath);

    const auto malformedUriPath = std::filesystem::temp_directory_path() / "vanta_gltf_import_bad_uri.gltf";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
        const std::string encoded = "albedo%20map.png";
        const auto uriPosition = document.find(encoded);
        if (uriPosition != std::string::npos)
            document.replace(uriPosition, encoded.size(), "albedo%2Gmap.png");
        std::ofstream output(malformedUriPath, std::ios::binary);
        output << document;
    }
    Check(!vanta::assets::LoadGltf(malformedUriPath, model, error),
        "rejects malformed percent-encoded external texture URIs");
    std::filesystem::remove(malformedUriPath);

    std::filesystem::remove(path);
    std::cout << (10 - failures) << "/10 glTF import tests passed.\\n";
    return failures == 0 ? 0 : 1;
}
