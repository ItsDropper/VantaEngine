#include "vanta/renderer/EnvironmentLighting.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vanta::renderer
{
namespace
{
struct Float3 { float x, y, z; };
constexpr float kPi = 3.14159265358979323846F;
constexpr float kInvFourPi = 1.0F / (4.0F * kPi);

Float3 Add(Float3 a, Float3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Float3 Mul(Float3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
Float3 Normalize(Float3 v) noexcept
{
    const float length = std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return length > 1.0e-6F ? Mul(v, 1.0F/length) : Float3{0.0F,1.0F,0.0F};
}
std::array<float, 9> Basis(Float3 d) noexcept
{
    return {0.282095F, 0.488603F*d.y, 0.488603F*d.z, 0.488603F*d.x,
        1.092548F*d.x*d.y, 1.092548F*d.y*d.z,
        0.315392F*(3.0F*d.z*d.z-1.0F),
        1.092548F*d.x*d.z, 0.546274F*(d.x*d.x-d.y*d.y)};
}
Float3 Radiance(Float3 direction, const EnvironmentSettings& e) noexcept
{
    const Float3 sun = Normalize({e.sunDirection.x,e.sunDirection.y,e.sunDirection.z});
    const float up = std::clamp(direction.y*0.5F+0.5F,0.0F,1.0F);
    const float horizon = std::exp(-std::abs(direction.y)*5.0F);
    const Float3 sky{e.fogColor.x,e.fogColor.y,e.fogColor.z};
    const Float3 ambient{e.ambientColor.x,e.ambientColor.y,e.ambientColor.z};
    const Float3 ground{ambient.x*0.42F,ambient.y*0.43F,ambient.z*0.48F};
    Float3 color = Add(Mul(ground,1.0F-up),Mul(sky,up));
    color = Add(color,Mul(sky,horizon*0.22F));
    color = Add(color,Mul(ambient,std::max(0.0F,e.ambientIntensity)*0.22F));
    const float sunDot = std::max(0.0F,direction.x*sun.x+direction.y*sun.y+direction.z*sun.z);
    const float disc = std::pow(sunDot,1024.0F);
    const float halo = std::pow(sunDot,24.0F);
    const Float3 sunColor{e.sunColor.x,e.sunColor.y,e.sunColor.z};
    color = Add(color,Mul(sunColor,std::max(0.0F,e.sunIntensity)*(disc*8.0F+halo*0.12F)));
    return color;
}
void Store(std::array<float,4>& out, Float3 v, float weight) noexcept
{
    out[0]+=v.x*weight; out[1]+=v.y*weight; out[2]+=v.z*weight;
}
}

EnvironmentLighting BuildEnvironmentLighting(const EnvironmentSettings& settings) noexcept
{
    EnvironmentLighting result{};
    if (!settings.enabled) return result;
    constexpr unsigned int sampleCount = 2048;
    // Fibonacci sphere gives uniform solid-angle coverage without face seams.
    constexpr float goldenAngle = 2.39996322972865332F;
    std::array<std::array<float,3>, EnvironmentLighting::kCoefficientCount> radianceCoefficients{};
    for (unsigned int i=0;i<sampleCount;++i) {
        const float y=1.0F-2.0F*(static_cast<float>(i)+0.5F)/static_cast<float>(sampleCount);
        const float radius=std::sqrt(std::max(0.0F,1.0F-y*y));
        const float phi=goldenAngle*static_cast<float>(i);
        const Float3 direction{radius*std::cos(phi),y,radius*std::sin(phi)};
        const auto basis=Basis(direction);
        const Float3 light=Radiance(direction,settings);
        for(std::size_t c=0;c<basis.size();++c) {
            const float weight=basis[c]*4.0F*kPi*kInvFourPi;
            radianceCoefficients[c][0]+=light.x*weight;
            radianceCoefficients[c][1]+=light.y*weight;
            radianceCoefficients[c][2]+=light.z*weight;
        }
    }
    // Lambert convolution kernel for SH bands L0, L1 and L2.
    constexpr std::array<float,3> diffuseKernel{ kPi, 2.0F*kPi/3.0F, kPi/4.0F };
    for(std::size_t c=0;c<9;++c) {
        const float kernel=c==0?diffuseKernel[0]:(c<=3?diffuseKernel[1]:diffuseKernel[2]);
        result.diffuse[c]={radianceCoefficients[c][0]*kernel,
            radianceCoefficients[c][1]*kernel,radianceCoefficients[c][2]*kernel,0.0F};
    }
    // Approximate the GGX prefilter's angular broadening in SH space. The narrowest
    // level retains high-frequency sun/sky contrast; rougher levels suppress higher bands.
    constexpr std::array<float,5> roughness{0.04F,0.18F,0.38F,0.65F,1.0F};
    for(std::size_t level=0;level<roughness.size();++level) {
        const float alpha=roughness[level]*roughness[level];
        const float sigma=alpha*1.35F;
        for(std::size_t c=0;c<9;++c) {
            const int band=c==0?0:(c<=3?1:2);
            const float filter=std::exp(-0.5F*static_cast<float>(band*(band+1))*sigma*sigma);
            result.specular[level][c]={radianceCoefficients[c][0]*filter,
                radianceCoefficients[c][1]*filter,radianceCoefficients[c][2]*filter,0.0F};
        }
    }
    return result;
}
}
