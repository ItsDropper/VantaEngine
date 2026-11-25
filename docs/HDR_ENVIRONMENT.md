# HDR environment lighting

The renderer accepts an immutable, decoded Radiance RGBE environment image on a submitted frame. The shared pointer keeps the image alive for the complete frame snapshot:

```cpp
std::ifstream file(path, std::ios::binary);
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

When the image identity changes, both Direct3D renderers project the HDR radiance into the existing nine-coefficient diffuse spherical-harmonic representation and five roughness-filtered specular SH levels. The result is cached by immutable image identity and uploaded through the renderer's existing scene constants. If no HDR image is supplied, the procedural environment generated from `EnvironmentSettings` remains the fallback.

The CPU preprocessor also exposes `BuildPrefilteredEnvironment`, which creates diffuse irradiance and GGX-prefiltered cubemap mip data. That cubemap data is not yet bound as a GPU texture by either backend; current material shading still evaluates the SH representation. This is an intentional intermediate integration stage, not a claim of full cubemap-based specular IBL.
