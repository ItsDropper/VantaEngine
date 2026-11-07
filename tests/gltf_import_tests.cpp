#include "vanta/assets/GltfLoader.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

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

std::vector<std::uint8_t> DecodeBase64(std::string_view input)
{
    const auto decode = [](char value) -> int
    {
        if (value >= 'A' && value <= 'Z') return value - 'A';
        if (value >= 'a' && value <= 'z') return value - 'a' + 26;
        if (value >= '0' && value <= '9') return value - '0' + 52;
        if (value == '+') return 62;
        if (value == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> output;
    for (std::size_t offset = 0; offset + 4U <= input.size(); offset += 4U)
    {
        const int a = decode(input[offset]);
        const int b = decode(input[offset + 1U]);
        const bool padC = input[offset + 2U] == '=';
        const bool padD = input[offset + 3U] == '=';
        const int c = padC ? 0 : decode(input[offset + 2U]);
        const int d = padD ? 0 : decode(input[offset + 3U]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return {};
        const std::uint32_t packed = (static_cast<std::uint32_t>(a) << 18U) |
                                     (static_cast<std::uint32_t>(b) << 12U) |
                                     (static_cast<std::uint32_t>(c) << 6U) |
                                     static_cast<std::uint32_t>(d);
        output.push_back(static_cast<std::uint8_t>((packed >> 16U) & 0xffU));
        if (!padC) output.push_back(static_cast<std::uint8_t>((packed >> 8U) & 0xffU));
        if (!padD) output.push_back(static_cast<std::uint8_t>(packed & 0xffU));
    }
    return output;
}
void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
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


    const std::string pngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAHUlEQVR42gESAO3/AOHr+P/r8vr/ANfk9f/m7/r/hy0PF2JNGOQAAAAASUVORK5CYII=";
    const std::string embeddedUri = "data:image/png;base64," + pngBase64;
    const auto embeddedPath = std::filesystem::temp_directory_path() / "vanta_gltf_embedded_image.gltf";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
        const std::string externalUri = "albedo%20map.png";
        const auto position = document.find(externalUri);
        if (position != std::string::npos)
            document.replace(position, externalUri.size(), embeddedUri);
        std::ofstream output(embeddedPath, std::ios::binary);
        output << document;
    }
    Check(vanta::assets::LoadGltf(embeddedPath, model, error) &&
          model.materials[1].embeddedDiffuseTexture != nullptr &&
          model.materials[1].embeddedDiffuseTexture->mimeType == "image/png" &&
          model.materials[1].embeddedDiffuseTexture->encodedBytes == DecodeBase64(pngBase64) &&
          model.materials[1].diffuseTexture.empty(),
        "imports data-URI images as owned encoded payloads without sidecar extraction");
    std::filesystem::remove(embeddedPath);

    const auto percentEncodedPath = std::filesystem::temp_directory_path() / "vanta_gltf_percent_image.gltf";
    const auto pngBytes = DecodeBase64(pngBase64);
    std::string percentUri = "data:image/png,";
    constexpr char hexDigits[] = "0123456789ABCDEF";
    for (const std::uint8_t byte : pngBytes)
    {
        percentUri.push_back('%');
        percentUri.push_back(hexDigits[byte >> 4U]);
        percentUri.push_back(hexDigits[byte & 0x0fU]);
    }
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
        const std::string externalUri = "albedo%20map.png";
        const auto position = document.find(externalUri);
        if (position != std::string::npos)
            document.replace(position, externalUri.size(), percentUri);
        std::ofstream output(percentEncodedPath, std::ios::binary);
        output << document;
    }
    Check(vanta::assets::LoadGltf(percentEncodedPath, model, error) &&
          model.materials[1].embeddedDiffuseTexture != nullptr &&
          model.materials[1].embeddedDiffuseTexture->encodedBytes == pngBytes,
        "imports percent-encoded glTF image data URIs");
    std::filesystem::remove(percentEncodedPath);

    const auto malformedDataUriPath = std::filesystem::temp_directory_path() / "vanta_gltf_embedded_bad_image.gltf";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
        const std::string externalUri = "albedo%20map.png";
        const auto position = document.find(externalUri);
        if (position != std::string::npos)
            document.replace(position, externalUri.size(), "data:image/png;base64,!!!!");
        std::ofstream output(malformedDataUriPath, std::ios::binary);
        output << document;
    }
    Check(!vanta::assets::LoadGltf(malformedDataUriPath, model, error) &&
          error.find("base64") != std::string::npos,
        "rejects malformed embedded image base64 payloads");
    std::filesystem::remove(malformedDataUriPath);

    const auto glbPath = std::filesystem::temp_directory_path() / "vanta_gltf_embedded_image.glb";
    {
        std::ifstream input(path, std::ios::binary);
        std::string document((std::istreambuf_iterator<char>(input)), {});
        const std::string geometryPrefix = "data:application/octet-stream;base64,";
        const auto geometryStart = document.find(geometryPrefix);
        const auto geometryEnd = document.find('"', geometryStart);
        const std::string geometryBase64 = document.substr(
            geometryStart + geometryPrefix.size(), geometryEnd - geometryStart - geometryPrefix.size());
        std::vector<std::uint8_t> binary = DecodeBase64(geometryBase64);
        const auto png = DecodeBase64(pngBase64);
        binary.insert(binary.end(), png.begin(), png.end());

        const std::string oldImages = R"json("images":[{"uri":"albedo%20map.png"},{"uri":"packed_mr.png"}],)json";
        const std::string newImages = R"json("images":[{"bufferView":4,"mimeType":"image/png"},{"uri":"packed_mr.png"}],)json";
        const auto imagePosition = document.find(oldImages);
        if (imagePosition != std::string::npos)
            document.replace(imagePosition, oldImages.size(), newImages);
        const auto bufferStart = document.find("\"buffers\":[{");
        const auto bufferEnd = document.find("}],", bufferStart);
        if (bufferStart != std::string::npos && bufferEnd != std::string::npos)
            document.replace(bufferStart, bufferEnd + 3U - bufferStart,
                             "\"buffers\":[{\"byteLength\":188}],");
        const std::string viewsEnd = R"json({"buffer":0,"byteOffset":96,"byteLength":6}
  ],)json";
        const std::string viewsWithImage = R"json({"buffer":0,"byteOffset":96,"byteLength":6},
    {"buffer":0,"byteOffset":102,"byteLength":86}
  ],)json";
        const auto viewsPosition = document.find(viewsEnd);
        if (viewsPosition != std::string::npos)
            document.replace(viewsPosition, viewsEnd.size(), viewsWithImage);

        while (document.size() % 4U != 0U) document.push_back(' ');
        while (binary.size() % 4U != 0U) binary.push_back(0U);
        std::vector<std::uint8_t> glb;
        const std::uint32_t totalLength = static_cast<std::uint32_t>(12U + 8U + document.size() + 8U + binary.size());
        AppendU32(glb, 0x46546c67U);
        AppendU32(glb, 2U);
        AppendU32(glb, totalLength);
        AppendU32(glb, static_cast<std::uint32_t>(document.size()));
        AppendU32(glb, 0x4e4f534aU);
        glb.insert(glb.end(), document.begin(), document.end());
        AppendU32(glb, static_cast<std::uint32_t>(binary.size()));
        AppendU32(glb, 0x004e4942U);
        glb.insert(glb.end(), binary.begin(), binary.end());
        std::ofstream output(glbPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    }
    Check(vanta::assets::LoadGltf(glbPath, model, error) &&
          model.materials[1].embeddedDiffuseTexture != nullptr &&
          model.materials[1].embeddedDiffuseTexture->encodedBytes == DecodeBase64(pngBase64),
        "imports GLB buffer-view images directly from the binary chunk");
    std::filesystem::remove(glbPath);

    std::filesystem::remove(path);
    std::cout << (checks - failures) << "/" << checks << " glTF import tests passed.\\n";
    return failures == 0 ? 0 : 1;
}
