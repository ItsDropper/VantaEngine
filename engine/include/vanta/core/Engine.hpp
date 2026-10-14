#pragma once

namespace vanta
{
class Engine final
{
public:
    [[nodiscard]] int Run(bool useD3D12 = false);
};
} // namespace vanta
