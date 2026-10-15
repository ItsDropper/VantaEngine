#include "vanta/renderer/ProceduralWorld.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    std::vector<vanta::renderer::WorldVertex> vertices;
    std::vector<unsigned int> indices;
    vanta::renderer::BuildDetailedWorld(vertices, indices);

    bool valid = !vertices.empty() && !indices.empty() && indices.size() % 3U == 0U;
    // Both render backends consume this exact terrain field, so ensure it stays finite,
    // deterministic, non-flat, and suitable for placing geometry on the surface.
    const float h0 = vanta::renderer::TerrainHeightAt(0.0F, 0.0F);
    valid = valid && std::isfinite(h0) && h0 == vanta::renderer::TerrainHeightAt(0.0F, 0.0F);
    float minHeight = h0;
    float maxHeight = h0;
    for (int z = -64; z <= 64; z += 8) {
        for (int x = -64; x <= 64; x += 8) {
            const float height = vanta::renderer::TerrainHeightAt(static_cast<float>(x), static_cast<float>(z));
            valid = valid && std::isfinite(height);
            minHeight = std::min(minHeight, height);
            maxHeight = std::max(maxHeight, height);
        }
    }
    valid = valid && (maxHeight - minHeight) > 2.0F;
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
