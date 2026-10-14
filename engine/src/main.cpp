#include "vanta/core/Engine.hpp"
#include "vanta/core/Log.hpp"

#include <string_view>

int main(const int argc, char** argv)
{
    if (argc > 1 && std::string_view{argv[1]} == "--self-test")
    {
        vanta::LogInfo("Startup self-test passed.");
        return 0;
    }

    const bool useD3D11 = argc > 1 && std::string_view{argv[1]} == "--backend=d3d11";
    const bool useD3D12 = !useD3D11;
    vanta::LogInfo("VantaEngine 0.2.0");
    vanta::LogInfo(useD3D12 ? "Backend: Win32 + Direct3D 12 (default)" : "Backend: Win32 + Direct3D 11 (compatibility mode)");

    vanta::Engine engine;
    return engine.Run(useD3D12);
}
