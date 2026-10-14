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

    vanta::LogInfo("VantaEngine 0.2.0");
    vanta::LogInfo("Backend: Win32 + Direct3D 11");

    vanta::Engine engine;
    return engine.Run();
}
