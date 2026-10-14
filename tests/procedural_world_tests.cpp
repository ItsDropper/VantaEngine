#include "vanta/renderer/ProceduralWorld.hpp"

#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    std::vector<vanta::renderer::WorldVertex> vertices;
    std::vector<unsigned int> indices;
    vanta::renderer::BuildDetailedWorld(vertices, indices);

    bool valid = !vertices.empty() && !indices.empty() && indices.size() % 3U == 0U;
    for (const auto index : indices) valid = valid && index < vertices.size();
    for (const auto& vertex : vertices) {
        for (float value : vertex.position) valid = valid && std::isfinite(value);
        for (float value : vertex.normal) valid = valid && std::isfinite(value);
        for (float value : vertex.color) valid = valid && std::isfinite(value);
    }

    if (!valid || vertices.size() < 20000U || indices.size() < 60000U) {
        std::cerr << "Procedural world is empty, unexpectedly small, or contains invalid mesh data. vertices="
                  << vertices.size() << " indices=" << indices.size() << '\n';
        return 1;
    }
    std::cout << "PASS: detailed world mesh has " << vertices.size() << " vertices and "
              << indices.size() / 3U << " triangles; indices and attributes are valid.\n";
    return 0;
}
