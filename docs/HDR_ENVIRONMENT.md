# HDR environment lighting

The renderer accepts an immutable, decoded Radiance RGBE environment image on a submitted frame. The shared pointer keeps the image alive for the complete frame snapshot:

```cpp
#include "vanta/renderer/HdrEnvironment.hpp"
#include "vanta/renderer/SceneSubmission.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

const std::filesystem::path path = "environment.hdr";
std::ifstream file(path, std::ios::binary);
if (!file)
    throw std::runtime_error("Could not open HDR environment.");
std::vector<std::uint8_t> encoded{
    std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()
};
auto environment = std::make_shared<vanta::renderer::HdrEnvironmentImage>();
std::string error;
if (!vanta::renderer::DecodeRadianceHdr(encoded, *environment, error))
    throw std::runtime_error(error);

vanta::renderer::FrameView view;
view.environment.enabled = true;
view.environmentMap = std::move(environment);
submission.BuildFrame(view, frame);
```

When the image identity changes, both Direct3D renderers generate a diffuse irradiance cubemap and a five-level GGX-prefiltered specular cubemap, then project those products into the existing nine-coefficient spherical-harmonic representation used by the shaders. The result is cached by immutable image identity and uploaded through the renderer's existing scene constants. If no HDR image is supplied, the procedural environment generated from `EnvironmentSettings` remains the fallback.

The CPU preprocessor exposes `BuildPrefilteredEnvironment` for callers that need the full cubemap mip data. The renderer currently projects its generated cubemap into SH rather than binding the cubemap as a GPU texture; current material shading still evaluates the SH representation. This is an intentional intermediate integration stage, not a claim of full cubemap-based specular IBL.
