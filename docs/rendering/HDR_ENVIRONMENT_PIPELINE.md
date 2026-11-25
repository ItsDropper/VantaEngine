# HDR environment preprocessing

VantaEngine exposes a backend-independent Radiance RGBE environment pipeline through
`vanta/renderer/HdrEnvironment.hpp`.

## Supported input

- Radiance `.hdr` / RGBE data with `FORMAT=32-bit_rle_rgbe`.
- Standard `-Y height +X width` orientation.
- Both Radiance scanline RLE and flat RGBE scanlines.
- Linear floating-point radiance output. No sRGB transfer or exposure is applied.

Images are rejected when dimensions are invalid, the stream is truncated, the
orientation is unsupported, or the decoded pixel count exceeds 16,777,216.

## Preprocessing

1. Decode RGBE into `HdrEnvironmentImage`.
2. Call `BuildPrefilteredEnvironment` to produce six cube faces for diffuse
   cosine-weighted irradiance and a complete GGX importance-sampled specular mip chain.
3. Validate the result before passing it to a graphics backend.

The current implementation is deterministic CPU preprocessing. It is deliberately
independent of D3D11 and D3D12 so both backends can consume the same generated data.
GPU texture creation, persistent GPU caching, and shader sampling are the next
integration stage; this API alone does not claim that the live renderers use IBL yet.

## Quality and memory controls

`EnvironmentPrefilterOptions` controls cube resolution, diffuse resolution, and
sample counts. The implementation bounds these settings to avoid unbounded allocation
or accidental multi-billion-sample preprocessing. Use smaller settings for tests and
offline asset validation, and higher settings for final environment assets.
