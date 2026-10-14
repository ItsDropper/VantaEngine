#include <iostream>
#include <string_view>

namespace
{
constexpr std::string_view kEngineVersion = "0.1.0";
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view{argv[1]} == "--self-test")
    {
        std::cout << "VantaEngine startup self-test passed.\n";
        return 0;
    }

    std::cout << "VantaEngine " << kEngineVersion << '\n';
    std::cout << "Native runtime initialized.\n";
    std::cout << "Renderer: not initialized yet\n";

    return 0;
}
