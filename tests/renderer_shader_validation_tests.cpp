#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/D3D12Renderer.hpp"

#include <iostream>
#include <string>

int main()
{
    std::string error;
    if (!vanta::ValidateD3D11SceneShader(error)) {
        std::cerr << "D3D11 scene shader failed to compile:\n" << error << '\n';
        return 1;
    }
    if (!vanta::ValidateD3D12SceneShader(error)) {
        std::cerr << "D3D12 scene shader failed to compile:\n" << error << '\n';
        return 1;
    }
    std::cout << "D3D11 and D3D12 scene shaders compile successfully.\n";
    return 0;
}
