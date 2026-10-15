#include "vanta/renderer/ProceduralWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vanta::renderer
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;
struct P { float x, y, z; };
P operator+(P a, P b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
P operator-(P a, P b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
P operator*(P a, float s) { return {a.x*s,a.y*s,a.z*s}; }
float Dot(P a, P b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
P Cross(P a, P b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
P Normalize(P a) { const float l=std::sqrt(std::max(Dot(a,a),0.000001F)); return a*(1.0F/l); }
float Hash(int x, int z, int salt=0) {
    std::uint32_t n=static_cast<std::uint32_t>(x)*374761393U + static_cast<std::uint32_t>(z)*668265263U + static_cast<std::uint32_t>(salt)*2246822519U;
    n=(n^(n>>13U))*1274126177U; n^=n>>16U;
    return static_cast<float>(n & 0x00ffffffU)/16777215.0F;
}
float SmoothNoise(float x, float z, int salt) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx0 = x - static_cast<float>(ix);
    const float fz0 = z - static_cast<float>(iz);
    const float fx = fx0 * fx0 * (3.0F - 2.0F * fx0);
    const float fz = fz0 * fz0 * (3.0F - 2.0F * fz0);
    const float a = Hash(ix, iz, salt);
    const float b = Hash(ix + 1, iz, salt);
    const float c = Hash(ix, iz + 1, salt);
    const float d = Hash(ix + 1, iz + 1, salt);
    const float lower = a + (b - a) * fx;
    const float upper = c + (d - c) * fx;
    return lower + (upper - lower) * fz;
}
float FractalNoise(float x, float z, int salt) {
    float value = 0.0F;
    float amplitude = 0.55F;
    float total = 0.0F;
    for (int octave = 0; octave < 4; ++octave) {
        value += SmoothNoise(x, z, salt + octave * 37) * amplitude;
        total += amplitude;
        x *= 2.03F;
        z *= 2.03F;
        amplitude *= 0.5F;
    }
    return value / total;
}
float Height(float x, float z) {
    // Layered, seeded noise produces broad landforms plus smaller terrain breakup.
    const float broad = FractalNoise(x * 0.018F, z * 0.018F, 11) - 0.5F;
    const float medium = FractalNoise(x * 0.062F, z * 0.062F, 29) - 0.5F;
    const float fine = SmoothNoise(x * 0.22F, z * 0.22F, 71) - 0.5F;
    const float rolling = 0.32F * std::sin(x * 0.075F) * std::cos(z * 0.061F)
        + 0.16F * std::sin((x + z) * 0.17F);
    const float creekX = -23.0F + std::sin(z * 0.09F) * 2.8F;
    const float creekDistance = (x - creekX) / 3.8F;
    const float creekCut = 1.05F * std::exp(-creekDistance * creekDistance);
    return -1.0F + broad * 5.4F + medium * 1.7F + fine * 0.28F + rolling - creekCut;
}
class Builder
{
public:
    std::vector<WorldVertex>& v; std::vector<unsigned int>& ix;
    void tri(P a,P b,P c,float r,float g,float bl) {
        P n=Normalize(Cross(b-a,c-a));
        const unsigned int base=static_cast<unsigned int>(v.size());
        for(P p : {a,b,c}) v.push_back({{p.x,p.y,p.z},{n.x,n.y,n.z},{r,g,bl,1.0F},{p.x*0.35F,p.z*0.35F}});
        ix.insert(ix.end(),{base,base+1U,base+2U});
    }
    void quad(P a,P b,P c,P d,float r,float g,float bl) { tri(a,b,c,r,g,bl); tri(a,c,d,r,g,bl); }
    void box(P c,P h,float r,float g,float b) {
        P a{c.x-h.x,c.y-h.y,c.z-h.z}, z{c.x+h.x,c.y-h.y,c.z-h.z};
        P q{c.x+h.x,c.y-h.y,c.z+h.z}, d{c.x-h.x,c.y-h.y,c.z+h.z};
        P e{c.x-h.x,c.y+h.y,c.z-h.z}, f{c.x+h.x,c.y+h.y,c.z-h.z};
        P j{c.x+h.x,c.y+h.y,c.z+h.z}, k{c.x-h.x,c.y+h.y,c.z+h.z};
        quad(a,d,q,z,r*0.78F,g*0.78F,b*0.78F); quad(e,f,j,k,r,g,b);
        quad(a,z,f,e,r*0.92F,g*0.92F,b*0.92F); quad(z,q,j,f,r*0.84F,g*0.84F,b*0.84F);
        quad(q,d,k,j,r*0.72F,g*0.72F,b*0.72F); quad(d,a,e,k,r*0.9F,g*0.9F,b*0.9F);
    }
    void cylinder(P base,float radius,float height,int sides,float r,float g,float b,float topRadiusFactor=0.72F) {
        P top{base.x,base.y+height,base.z};
        for(int i=0;i<sides;++i) {
            const float a0=2*kPi*i/sides, a1=2*kPi*(i+1)/sides;
            P p0{base.x+std::cos(a0)*radius,base.y,base.z+std::sin(a0)*radius};
            P p1{base.x+std::cos(a1)*radius,base.y,base.z+std::sin(a1)*radius};
            P t0{base.x+std::cos(a0)*radius*topRadiusFactor,top.y,base.z+std::sin(a0)*radius*topRadiusFactor};
            P t1{base.x+std::cos(a1)*radius*topRadiusFactor,top.y,base.z+std::sin(a1)*radius*topRadiusFactor};
            const float shade=0.86F+0.14F*Hash(i,sides,static_cast<int>(base.x*17));
            quad(p0,p1,t1,t0,r*shade,g*shade,b*shade);
            tri(t0,t1,top,r*0.9F,g*0.9F,b*0.9F);
        }
    }
    void cone(P base,float radius,float height,int sides,float r,float g,float b) {
        P tip{base.x,base.y+height,base.z};
        for(int i=0;i<sides;++i) {
            float a0=2*kPi*i/sides, a1=2*kPi*(i+1)/sides;
            P p0{base.x+std::cos(a0)*radius,base.y,base.z+std::sin(a0)*radius};
            P p1{base.x+std::cos(a1)*radius,base.y,base.z+std::sin(a1)*radius};
            float s=0.78F+0.28F*Hash(i,static_cast<int>(base.x*9),static_cast<int>(base.z*7));
            tri(p0,p1,tip,r*s,g*s,b*s);
        }
    }
    void rock(P c,float rx,float ry,float rz,float shade) {
        constexpr int rings=4, sides=7;
        P bottom{c.x,c.y-ry*0.45F,c.z};
        for(int ring=0;ring<rings;++ring) {
            const float v0=static_cast<float>(ring)/rings, v1=static_cast<float>(ring+1)/rings;
            for(int side=0;side<sides;++side) {
                const float a0=2*kPi*side/sides, a1=2*kPi*(side+1)/sides;
                auto point=[&](float v,float a) {
                    const float radius=std::sin(v*kPi*0.5F);
                    const float wobble=0.78F+0.35F*Hash(side+ring*13,static_cast<int>(c.x*7),static_cast<int>(c.z*3));
                    return P{c.x+std::cos(a)*rx*radius*wobble,c.y-ry*0.45F+v*(ry*1.35F),c.z+std::sin(a)*rz*radius*wobble};
                };
                P p0=point(v0,a0),p1=point(v0,a1),p2=point(v1,a1),p3=point(v1,a0);
                float s=shade*(0.78F+0.3F*Hash(side,ring,static_cast<int>(c.x*5)));
                if(ring==0) { tri(bottom,p1,p0,s*0.8F,s*0.79F,s*0.75F); }
                else { quad(p0,p1,p2,p3,s*0.83F,s*0.82F,s*0.78F); }
            }
        }
    }
    void blob(P c,float rx,float ry,float rz,float r,float g,float bl) {
        constexpr int rings = 5, sides = 9;
        for (int ring = 0; ring < rings; ++ring) {
            const float latitude0 = -0.5F * kPi + kPi * static_cast<float>(ring) / rings;
            const float latitude1 = -0.5F * kPi + kPi * static_cast<float>(ring + 1) / rings;
            for (int side = 0; side < sides; ++side) {
                const float a0 = 2.0F * kPi * side / sides;
                const float a1 = 2.0F * kPi * (side + 1) / sides;
                auto point = [&](float latitude, float angle) {
                    const float wobble = 0.88F + 0.22F * Hash(side + ring * 17,
                        static_cast<int>(c.x * 5.0F), static_cast<int>(c.z * 7.0F));
                    const float horizontal = std::cos(latitude) * wobble;
                    return P{c.x + std::cos(angle) * rx * horizontal,
                             c.y + std::sin(latitude) * ry * wobble,
                             c.z + std::sin(angle) * rz * horizontal};
                };
                const P p0 = point(latitude0, a0);
                const P p1 = point(latitude0, a1);
                const P p2 = point(latitude1, a1);
                const P p3 = point(latitude1, a0);
                const float shade = 0.78F + 0.28F * Hash(side, ring, static_cast<int>(c.x * 11.0F));
                quad(p0, p1, p2, p3, r * shade, g * shade, bl * shade);
            }
        }
    }
    void beam(P a,P b,float radius,float r,float g,float bl) {
        P d=b-a; const float len=std::sqrt(Dot(d,d)); if(len<0.001F)return;
        const P along=d*(1.0F/len);
        P side=Normalize(Cross(along,{0.0F,1.0F,0.0F}));
        if(Dot(side,side)<0.1F) side={1.0F,0.0F,0.0F};
        const P up=Normalize(Cross(side,along));
        const P a0=a+side*radius+up*radius, a1=a-side*radius+up*radius;
        const P a2=a-side*radius-up*radius, a3=a+side*radius-up*radius;
        const P b0=b+side*radius+up*radius, b1=b-side*radius+up*radius;
        const P b2=b-side*radius-up*radius, b3=b+side*radius-up*radius;
        quad(a0,b0,b1,a1,r,g,bl); quad(a1,b1,b2,a2,r*0.9F,g*0.9F,bl*0.9F);
        quad(a2,b2,b3,a3,r*0.8F,g*0.8F,bl*0.8F); quad(a3,b3,b0,a0,r*0.95F,g*0.95F,bl*0.95F);
    }
    // Tapered radial branch geometry: unlike the old box beams, this produces rounded limbs
    // with changing radii and enough sides to avoid the obvious square-stick silhouette.
    void branchTube(P a, P b, float radiusA, float radiusB, int sides,
                    float r, float g, float bl) {
        const P delta = b - a;
        const float length = std::sqrt(std::max(Dot(delta, delta), 0.000001F));
        const P axis = delta * (1.0F / length);
        P reference = std::abs(axis.y) > 0.92F ? P{1.0F, 0.0F, 0.0F} : P{0.0F, 1.0F, 0.0F};
        const P side = Normalize(Cross(axis, reference));
        const P up = Normalize(Cross(side, axis));
        for (int i = 0; i < sides; ++i) {
            const float a0 = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(sides);
            const float a1 = 2.0F * kPi * static_cast<float>(i + 1) / static_cast<float>(sides);
            const P radial0 = side * std::cos(a0) + up * std::sin(a0);
            const P radial1 = side * std::cos(a1) + up * std::sin(a1);
            const P p0 = a + radial0 * radiusA;
            const P p1 = a + radial1 * radiusA;
            const P p2 = b + radial1 * radiusB;
            const P p3 = b + radial0 * radiusB;
            const float variation = 0.88F + 0.16F * Hash(i, sides, static_cast<int>((a.x + a.z) * 13.0F));
            quad(p0, p1, p2, p3, r * variation, g * variation, bl * variation);
        }
    }
    // A tapered, folded leaf blade with a pointed tip and visible central ridge.
    // The leaf is actual geometry rather than a spherical crown blob.
    void leaf(P base, P tip, float width, float r, float g, float bl, int seed) {
        const P direction = Normalize(tip - base);
        P side = Normalize(Cross(direction, {0.0F, 1.0F, 0.0F}));
        if (Dot(side, side) < 0.1F) side = Normalize(Cross(direction, {1.0F, 0.0F, 0.0F}));
        const P ridge = Normalize(Cross(side, direction));
        const P middle = base + (tip - base) * 0.48F;
        const P left = middle + side * width;
        const P right = middle - side * width;
        const P center = middle + ridge * (width * (0.10F + Hash(seed, 3) * 0.18F));
        const float shade = 0.82F + Hash(seed, 7) * 0.32F;
        tri(base, left, center, r * shade, g * shade, bl * shade);
        tri(left, tip, center, r * shade, g * shade, bl * shade);
        tri(tip, right, center, r * 0.92F * shade, g * 0.92F * shade, bl * 0.92F * shade);
        tri(right, base, center, r * 0.92F * shade, g * 0.92F * shade, bl * 0.92F * shade);
    }
    void cabin(float x,float z,float scale) {
        const float y=Height(x,z);
        const float w=2.3F*scale, d=2.0F*scale, h=1.8F*scale;
        // Stone footings, weathered timber walls, board seams and a proper pitched roof.
        for(int sx=-1;sx<=1;sx+=2) for(int sz=-1;sz<=1;sz+=2)
            box({x+sx*w*0.82F,y+0.18F*scale,z+sz*d*0.82F},{0.22F*scale,0.22F*scale,0.22F*scale},0.34F,0.33F,0.29F);
        box({x,y+h*0.5F,z},{w,h*0.5F,d},0.36F,0.24F,0.14F);
        for(int i=0;i<13;++i) {
            float xx=x-w+2*w*i/12.0F;
            box({xx,y+h*0.5F,z-d-0.025F*scale},{0.025F*scale,h*0.47F,0.035F*scale},0.2F+0.06F*Hash(i,2),0.13F,0.075F);
        }
        // Two sloping roof planes and ridge cap.
        P ridge{x,y+h+0.9F*scale,z};
        P lf{x-w-0.35F*scale,y+h,z-d-0.35F*scale}, lb{x-w-0.35F*scale,y+h,z+d+0.35F*scale};
        P rf{x+w+0.35F*scale,y+h,z-d-0.35F*scale}, rb{x+w+0.35F*scale,y+h,z+d+0.35F*scale};
        P ridgeFront{x,y+h+0.9F*scale,z-d-0.35F*scale};
        P ridgeBack{x,y+h+0.9F*scale,z+d+0.35F*scale};
        quad(lf,ridgeFront,ridgeBack,lb,0.16F,0.18F,0.18F);
        quad(ridgeFront,rf,rb,ridgeBack,0.13F,0.15F,0.16F);
        // Individual overlapping shingle tiles add real silhouette breaks and visible roof patterning.
        constexpr int shingleRows = 9, shingleColumns = 12;
        const float roofOuterX = w + 0.35F * scale;
        const float roofHalfLength = d + 0.35F * scale;
        for (int row = 0; row < shingleRows; ++row) {
            const float t0 = static_cast<float>(row) / shingleRows;
            const float t1 = static_cast<float>(row + 1) / shingleRows;
            for (int col = 0; col < shingleColumns; ++col) {
                const float stagger = (row % 2) * 0.5F;
                const float z0 = z - roofHalfLength + (2.0F * roofHalfLength) * (col + stagger + 0.035F) / shingleColumns;
                const float z1 = z - roofHalfLength + (2.0F * roofHalfLength) * (col + stagger + 0.91F) / shingleColumns;
                const float shade = 0.88F + 0.18F * Hash(row, col, static_cast<int>(x * 7.0F));
                const float lx0 = x - roofOuterX + roofOuterX * t0;
                const float lx1 = x - roofOuterX + roofOuterX * t1;
                const float ly0 = y + h + 0.9F * scale * t0 + 0.018F * scale;
                const float ly1 = y + h + 0.9F * scale * t1 + 0.018F * scale;
                quad({lx0,ly0,z0},{lx1,ly1,z0},{lx1,ly1,z1},{lx0,ly0,z1},
                    0.15F*shade,0.17F*shade,0.18F*shade);
                const float rx0 = x + roofOuterX * t0;
                const float rx1 = x + roofOuterX * t1;
                const float ry0 = y + h + 0.9F * scale * (1.0F - t0) + 0.018F * scale;
                const float ry1 = y + h + 0.9F * scale * (1.0F - t1) + 0.018F * scale;
                quad({rx0,ry0,z0},{rx1,ry1,z0},{rx1,ry1,z1},{rx0,ry0,z1},
                    0.13F*shade,0.15F*shade,0.16F*shade);
            }
        }
        // Exposed fascia boards give the roof a readable, finished silhouette.
        beam(lf,lb,0.07F*scale,0.28F,0.17F,0.09F);
        beam(rf,rb,0.07F*scale,0.25F,0.15F,0.08F);
        box({x,y+0.82F*scale,z-d-0.09F*scale},{0.42F*scale,0.78F*scale,0.08F*scale},0.16F,0.095F,0.055F);
        box({x-0.95F*scale,y+1.1F*scale,z-d-0.1F*scale},{0.43F*scale,0.42F*scale,0.09F*scale},0.07F,0.16F,0.18F);
        box({x-0.95F*scale,y+1.1F*scale,z-d-0.2F*scale},{0.51F*scale,0.05F*scale,0.035F*scale},0.24F,0.18F,0.12F);
        box({x-0.95F*scale,y+1.1F*scale,z-d-0.2F*scale},{0.04F*scale,0.45F*scale,0.035F*scale},0.24F,0.18F,0.12F);
        box({x+0.95F*scale,y+1.1F*scale,z-d-0.1F*scale},{0.43F*scale,0.42F*scale,0.09F*scale},0.07F,0.15F,0.17F);
        box({x+0.95F*scale,y+1.1F*scale,z-d-0.2F*scale},{0.035F*scale,0.45F*scale,0.035F*scale},0.25F,0.19F,0.13F);
        // Window mullions, recessed-looking dark panes, and a small metal door latch.
        for (float wx : {x-0.95F*scale, x+0.95F*scale}) {
            box({wx,y+1.1F*scale,z-d-0.245F*scale},{0.035F*scale,0.39F*scale,0.018F*scale},0.20F,0.17F,0.12F);
            box({wx,y+1.1F*scale,z-d-0.25F*scale},{0.36F*scale,0.025F*scale,0.018F*scale},0.22F,0.18F,0.13F);
        }
        box({x+0.23F*scale,y+0.83F*scale,z-d-0.19F*scale},{0.035F*scale,0.055F*scale,0.025F*scale},0.55F,0.43F,0.20F);
        // Individually spaced horizontal weatherboards on both side elevations.
        for (int plank = 0; plank < 9; ++plank) {
            const float py = y + (0.22F + plank * 0.19F) * scale;
            const float tone = 0.88F + 0.18F * Hash(plank, static_cast<int>(x), static_cast<int>(z));
            for (int side : {-1, 1}) {
                box({x+side*(w+0.018F*scale),py,z},{0.028F*scale,0.035F*scale,d*0.94F},
                    0.25F*tone,0.155F*tone,0.085F*tone);
            }
        }
        // Door boards and a threshold make the entrance read as constructed timber.
        for (int plank = 0; plank < 5; ++plank) {
            box({x-0.31F*scale+plank*0.155F*scale,y+0.82F*scale,z-d-0.175F*scale},
                {0.012F*scale,0.72F*scale,0.012F*scale},0.23F,0.135F,0.075F);
        }
        box({x,y+0.045F*scale,z-d-0.65F*scale},{1.05F*scale,0.07F*scale,0.65F*scale},0.32F,0.23F,0.13F);
        for(int i=0;i<5;++i) box({x-0.8F*scale+i*0.4F*scale,y+0.08F*scale,z-d-1.18F*scale},{0.17F*scale,0.08F*scale,0.25F*scale},0.29F,0.19F,0.11F);
        box({x+1.25F*scale,y+h+0.7F*scale,z+0.45F*scale},{0.24F*scale,0.9F*scale,0.24F*scale},0.3F,0.27F,0.23F);
        box({x+1.25F*scale,y+h+1.55F*scale,z+0.45F*scale},{0.3F*scale,0.06F*scale,0.3F*scale},0.12F,0.12F,0.12F);
        // Staggered mortar seams on the chimney's visible faces.
        for (int course = 0; course < 5; ++course) {
            const float cy = y+h+0.18F*scale+course*0.27F*scale;
            box({x+1.25F*scale,cy,z+0.202F*scale},{0.245F*scale,0.012F*scale,0.012F*scale},0.19F,0.18F,0.16F);
            const float offset = (course % 2 == 0) ? 0.0F : 0.12F*scale;
            box({x+1.25F*scale-0.12F*scale+offset,cy+0.12F*scale,z+0.202F*scale},
                {0.009F*scale,0.12F*scale,0.013F*scale},0.18F,0.17F,0.15F);
        }
        // Foundation courses wrap the cabin rather than leaving four isolated corner stones.
        for (int stone = 0; stone < 9; ++stone) {
            const float sx = x-w+2.0F*w*(stone+0.5F)/9.0F;
            box({sx,y+0.12F*scale,z-d-0.015F*scale},{w/9.0F*0.91F,0.13F*scale,0.12F*scale},
                0.32F+0.035F*Hash(stone,1),0.31F,0.28F);
            box({sx,y+0.12F*scale,z+d+0.015F*scale},{w/9.0F*0.91F,0.13F*scale,0.12F*scale},
                0.29F+0.035F*Hash(stone,2),0.29F,0.27F);
        }
    }
};

// Recursive broadleaf growth: primary limbs fork into smaller limbs, then twigs,
// with clusters of individual pointed leaves at the terminal growth. Seeded inputs
// keep every run reproducible while making each tree structurally different.
void GrowBroadleaf(Builder& b, P start, P direction, float length, float radius,
                   int depth, int seed, float seasonal) {
    direction = Normalize(direction);
    const P end = start + direction * length;
    const float bark = 0.88F + Hash(seed, depth, 5) * 0.22F;
    b.branchTube(start, end, radius, radius * 0.62F, 6,
                 0.22F * bark, 0.12F * bark, 0.062F * bark);
    if (depth <= 0) {
        for (int leafIndex = 0; leafIndex < 8; ++leafIndex) {
            const float angle = Hash(seed, leafIndex, 17) * 2.0F * kPi;
            const P leafDirection = Normalize({
                direction.x * 0.34F + std::cos(angle) * 0.74F,
                direction.y * 0.28F + 0.22F + Hash(seed, leafIndex, 19) * 0.28F,
                direction.z * 0.34F + std::sin(angle) * 0.74F});
            const float leafLength = 0.22F + Hash(seed, leafIndex, 23) * 0.24F;
            const P leafBase = end + leafDirection * (Hash(seed, leafIndex, 29) * 0.11F);
            const P leafTip = leafBase + leafDirection * leafLength;
            const float tint = 0.78F + Hash(seed, leafIndex, 31) * 0.42F;
            const float red = (0.035F + seasonal * 0.035F) * tint;
            const float green = (0.17F + seasonal * 0.10F) * tint;
            const float blue = (0.045F + seasonal * 0.025F) * tint;
            b.leaf(leafBase, leafTip, 0.045F + Hash(seed, leafIndex, 37) * 0.035F,
                   red, green, blue, seed * 11 + leafIndex);
        }
        return;
    }
    for (int child = 0; child < 2; ++child) {
        const float angle = Hash(seed, depth, child + 41) * 2.0F * kPi;
        const P childDirection = Normalize({
            direction.x * 0.48F + std::cos(angle) * 0.68F,
            std::max(0.18F, direction.y * 0.58F + 0.20F + Hash(seed, child, 43) * 0.18F),
            direction.z * 0.48F + std::sin(angle) * 0.68F});
        const float startAlong = 0.42F + Hash(seed, child, 47) * 0.28F;
        const P childStart = start + (end - start) * startAlong;
        GrowBroadleaf(b, childStart, childDirection,
                      length * (0.55F + Hash(seed, child, 53) * 0.13F),
                      radius * 0.61F, depth - 1, seed * 3 + child + 59, seasonal);
    }
}
}

float TerrainHeightAt(float x, float z)
{
    return Height(x, z);
}

void BuildDetailedWorld(std::vector<WorldVertex>& vertices, std::vector<unsigned int>& indices)
{
    Builder b{vertices,indices};
    // A worn access road with shoulders, broken center paint, drainage ditches and a creek crossing.
    for(int i=0;i<130;++i) {
        const float z0=-65.0F+i, z1=z0+1.0F;
        const float x0=8.0F+std::sin(z0*0.035F)*4.2F, x1=8.0F+std::sin(z1*0.035F)*4.2F;
        const float leftX0=x0-2.8F, leftX1=x1-2.8F, rightX0=x0+2.8F, rightX1=x1+2.8F;
        const float leftY0=Height(leftX0,z0)+0.035F, leftY1=Height(leftX1,z1)+0.035F;
        const float rightY0=Height(rightX0,z0)+0.035F, rightY1=Height(rightX1,z1)+0.035F;
        const float shoulderLeftY0=Height(x0-3.5F,z0), shoulderLeftY1=Height(x1-3.5F,z1);
        const float shoulderRightY0=Height(x0+3.5F,z0), shoulderRightY1=Height(x1+3.5F,z1);
        const float roadR=0.24F+0.035F*Hash(i,1), roadG=0.235F+0.03F*Hash(i,3), roadB=0.22F+0.025F*Hash(i,5);
        b.quad({leftX0,leftY0,z0},{leftX1,leftY1,z1},{rightX1,rightY1,z1},{rightX0,rightY0,z0},
            roadR,roadG,roadB);
        b.quad({x0-3.5F,shoulderLeftY0-0.025F,z0},{x1-3.5F,shoulderLeftY1-0.025F,z1},
            {leftX1,leftY1,z1},{leftX0,leftY0,z0},0.29F,0.27F,0.23F);
        b.quad({rightX0,rightY0,z0},{rightX1,rightY1,z1},
            {x1+3.5F,shoulderRightY1-0.025F,z1},{x0+3.5F,shoulderRightY0-0.025F,z0},0.29F,0.27F,0.23F);
        if(i%9<5) {
            const float centerY0=Height(x0,z0)+0.047F, centerY1=Height(x1,z1)+0.047F;
            b.quad({x0-0.055F,centerY0,z0},{x1-0.055F,centerY1,z1},
                {x1+0.055F,centerY1,z1},{x0+0.055F,centerY0,z0},0.62F,0.53F,0.34F);
        }
    }
    // A rocky stream and irregular banks cut through the left side of the valley.
    for(int i=0;i<95;++i) {
        float z=-47.0F+i, x=-23.0F+std::sin(z*0.09F)*2.8F;
        float y=Height(x,z)+0.035F;
        b.quad({x-1.1F,y,z},{x+1.1F,y,z},{x+1.0F,y+0.012F,z+1.0F},{x-1.0F,y+0.012F,z+1.0F},0.075F,0.22F,0.25F);
        if(i%2==0) b.rock({x-1.5F,y+0.05F,z+0.2F},0.45F,0.28F,0.6F,0.32F);
        if(i%3==0) b.rock({x+1.45F,y+0.04F,z+0.5F},0.55F,0.3F,0.42F,0.38F);
    }
    // A second structure gives the landscape scale and creates a small abandoned ranger compound.
    b.cabin(-12.0F,9.0F,0.82F);
    b.cabin(18.0F,25.0F,0.65F);
    // Hundreds of individually varied trees, trunks, branches and tiered crowns.
    for(int gz=-9;gz<=9;++gz) for(int gx=-9;gx<=9;++gx) {
        const float jx=(Hash(gx,gz,4)-0.5F)*4.8F, jz=(Hash(gz,gx,8)-0.5F)*4.8F;
        const float x=gx*6.8F+jx, z=gz*6.8F+jz;
        if(x*x+z*z<115.0F || std::abs(x)>59.0F || std::abs(z)>59.0F) continue;
        if(std::abs(x-(8.0F+std::sin(z*0.035F)*4.2F))<5.0F || (x>-27&&x<-19)) continue;
        const float y=Height(x,z), h=4.0F+Hash(gx,gz,2)*4.5F, radius=0.9F+Hash(gz,gx,6)*0.85F;
        const float trunkRadius = 0.18F + Hash(gx,gz,7)*0.1F;
        b.cylinder({x,y,z},trunkRadius,h*0.72F,11,0.22F,0.12F,0.065F,0.28F);
        // Exposed buttress roots flare into the forest floor; darker seams break up the smooth trunk.
        for (int root = 0; root < 4; ++root) {
            const float angle = root * (kPi * 0.5F) + Hash(gx,gz,root) * 0.55F;
            P end{x + std::cos(angle) * (0.48F + trunkRadius),
                  y + 0.035F,
                  z + std::sin(angle) * (0.48F + trunkRadius)};
            b.beam({x,y+0.12F,z},end,0.055F,0.25F,0.135F,0.07F);
        }
        for (int groove = 0; groove < 3; ++groove) {
            const float angle = groove * 2.0943951F + Hash(gx,gz,groove+30) * 0.4F;
            const float gx0 = x + std::cos(angle) * (trunkRadius * 0.88F);
            const float gz0 = z + std::sin(angle) * (trunkRadius * 0.88F);
            b.beam({gx0,y+0.2F,gz0},{gx0+0.01F,y+h*0.38F,gz0+0.01F},
                   0.012F,0.13F,0.075F,0.04F);
        }
        if (Hash(gx, gz, 19) < 0.34F) {
            // Broadleaf crowns are built from branching wood and individual leaf blades,
            // not low-poly ellipsoids. Six primary limbs each fork through three twig levels.
            const float seasonal = Hash(gx, gz, 79);
            for (int branch = 0; branch < 6; ++branch) {
                const float angle = Hash(gx, gz, branch + 41) * 2.0F * kPi;
                const float branchY = y + h * (0.40F + Hash(gx, gz, branch + 47) * 0.16F);
                const P start{x + std::cos(angle) * trunkRadius * 0.28F, branchY,
                              z + std::sin(angle) * trunkRadius * 0.28F};
                const P direction = Normalize({
                    std::cos(angle) * (0.55F + Hash(gx, gz, branch + 53) * 0.20F),
                    0.48F + Hash(gx, gz, branch + 57) * 0.38F,
                    std::sin(angle) * (0.55F + Hash(gx, gz, branch + 61) * 0.20F)});
                GrowBroadleaf(b, start, direction, radius * (0.82F + Hash(gx, gz, branch + 67) * 0.48F),
                              trunkRadius * 0.46F, 3, gx * 997 + gz * 313 + branch * 71, seasonal);
            }
            // Fine upper shoots make the crown less like a hollow ring of branches.
            for (int shoot = 0; shoot < 10; ++shoot) {
                const float angle = Hash(gx, gz, shoot + 101) * 2.0F * kPi;
                const P start{x + std::cos(angle) * radius * 0.18F, y + h * 0.63F,
                              z + std::sin(angle) * radius * 0.18F};
                const P direction = Normalize({std::cos(angle) * 0.30F, 0.92F,
                                                std::sin(angle) * 0.30F});
                GrowBroadleaf(b, start, direction, radius * 0.43F, trunkRadius * 0.18F,
                              1, gx * 127 + gz * 37 + shoot * 13, seasonal);
            }
        } else {
            // Conifers use a visible central leader, six irregular bough tiers, secondary
            // twigs and many narrow needle blades instead of stacked primitive cones.
            for (int tier = 0; tier < 6; ++tier) {
                const float t = static_cast<float>(tier) / 5.0F;
                const float ty = y + h * (0.20F + t * 0.66F);
                const float rr = radius * (1.0F - t * 0.79F);
                const int arms = 7 + (tier % 3);
                for (int arm = 0; arm < arms; ++arm) {
                    const float angle = 2.0F * kPi * static_cast<float>(arm) / static_cast<float>(arms)
                        + Hash(gx, gz, tier * 31 + arm) * 0.58F;
                    const P start{x + std::cos(angle) * trunkRadius * 0.45F, ty,
                                  z + std::sin(angle) * trunkRadius * 0.45F};
                    const P end{x + std::cos(angle) * rr * (0.88F + Hash(gx, gz, arm + tier * 17) * 0.18F),
                                ty - rr * (0.24F + Hash(gx, gz, arm + 71) * 0.20F),
                                z + std::sin(angle) * rr * (0.88F + Hash(gx, gz, arm + tier * 23) * 0.18F)};
                    const float branchRadius = std::max(0.012F, rr * 0.065F);
                    const float green = 0.17F + Hash(gx, gz, tier + arm * 7) * 0.10F;
                    b.branchTube(start, end, branchRadius, branchRadius * 0.10F, 5,
                                 0.22F, green, 0.105F);
                    for (int twig = 0; twig < 3; ++twig) {
                        const float along = 0.38F + twig * 0.21F;
                        const P twigBase = start + (end - start) * along;
                        const float twigAngle = angle + (twig % 2 == 0 ? 1.0F : -1.0F) * (0.58F + Hash(gx, gz, twig + arm * 13) * 0.36F);
                        const P twigTip = twigBase + P{std::cos(twigAngle) * rr * 0.24F,
                                                       -rr * (0.07F + Hash(gx, gz, twig + 97) * 0.08F),
                                                       std::sin(twigAngle) * rr * 0.24F};
                        b.branchTube(twigBase, twigTip, branchRadius * 0.44F, branchRadius * 0.05F, 4,
                                     0.20F, green * 0.92F, 0.095F);
                        for (int needle = 0; needle < 5; ++needle) {
                            const float nt = 0.16F + needle * 0.17F;
                            const P needleBase = twigBase + (twigTip - twigBase) * nt;
                            const float sideSign = needle % 2 == 0 ? 1.0F : -1.0F;
                            const P needleTip = needleBase + P{
                                std::cos(twigAngle + sideSign * 0.8F) * (0.20F + Hash(gx, gz, needle + twig * 19) * 0.15F),
                                0.045F + Hash(gx, gz, needle + arm * 11) * 0.10F,
                                std::sin(twigAngle + sideSign * 0.8F) * (0.20F + Hash(gx, gz, needle + tier * 13) * 0.15F)};
                            b.leaf(needleBase, needleTip, 0.018F + Hash(gx, gz, needle + 53) * 0.012F,
                                   0.055F, green * 1.12F, 0.075F, gx * 17 + gz * 31 + tier * 7 + arm * 3 + needle);
                        }
                    }
                }
            }
        }
    }
    // Broadleaf understory, low shrubs and angular boulders; deterministic placements keep builds reproducible.
    for(int i=0;i<420;++i) {
        const int gx=static_cast<int>(Hash(i,7)*110.0F)-55, gz=static_cast<int>(Hash(i,13)*110.0F)-55;
        float x=gx+Hash(i,17)*0.8F, z=gz+Hash(i,19)*0.8F;
        if(x*x+z*z<30.0F || std::abs(x-(8.0F+std::sin(z*0.035F)*4.2F))<4.5F) continue;
        float y=Height(x,z);
        if(i%3==0) {
            float s=0.35F+Hash(i,23)*0.55F;
            b.cone({x,y,z},s,0.55F+Hash(i,29)*0.55F,6,0.06F,0.20F,0.085F);
            b.cone({x+s*0.4F,y+0.18F,z+s*0.2F},s*0.72F,0.48F,6,0.08F,0.25F,0.1F);
        } else {
            float s=0.25F+Hash(i,31)*0.7F;
            b.rock({x,y+s*0.35F,z},s,s*0.6F,s*(0.6F+Hash(i,37)*0.7F),0.29F+Hash(i,41)*0.12F);
        }
    }
    // Thousands of crossed grass blades create foreground texture and break up the smooth terrain.
    for(int i=0;i<1850;++i) {
        const int gx=static_cast<int>(Hash(i,43)*124.0F)-62, gz=static_cast<int>(Hash(i,47)*124.0F)-62;
        const float x=gx+Hash(i,53), z=gz+Hash(i,59);
        const float roadX=8.0F+std::sin(z*0.035F)*4.2F;
        if(std::abs(x-roadX)<3.8F || (x>-25.0F&&x<-21.0F) || x*x+z*z<22.0F) continue;
        const float y=Height(x,z)+0.025F, h=0.12F+Hash(i,61)*0.28F, w=0.025F+Hash(i,67)*0.025F;
        const float r=0.055F+Hash(i,71)*0.055F, g=0.18F+Hash(i,73)*0.19F, bl=0.045F+Hash(i,79)*0.07F;
        const float a=Hash(i,83)*kPi, dx=std::cos(a)*w, dz=std::sin(a)*w;
        b.tri({x-dx,y,z-dz},{x+dx,y,z+dz},{x+std::sin(a)*0.05F,y+h,z+std::cos(a)*0.05F},r,g,bl);
        b.tri({x-dz,y,z+dx},{x+dz,y,z-dx},{x+std::cos(a)*0.05F,y+h*0.83F,z-std::sin(a)*0.05F},r*0.85F,g*0.92F,bl);
    }
    // Timber fencing, gates, stacked cut logs, a roadside sign and utility poles sell the human scale.
    for(int i=0;i<18;++i) {
        const float x=-5.5F+i*0.65F, z=-4.2F, y=Height(x,z);
        b.box({x,y+0.62F,z},{0.075F,0.62F,0.075F},0.25F,0.16F,0.075F);
        if(i<17) for(float yy : {0.35F,0.85F}) b.box({x+0.325F,y+yy,z},{0.36F,0.055F,0.055F},0.32F,0.2F,0.09F);
    }
    for(int i=0;i<7;++i) {
        const float x=-7.5F+i*0.65F, z=-6.2F, y=Height(x,z);
        b.cylinder({x,y+0.18F,z},0.12F,2.4F,7,0.34F,0.2F,0.11F,0.92F);
        b.cylinder({x+0.02F,y+0.18F,z+0.28F},0.12F,2.0F,7,0.28F,0.16F,0.08F,0.92F);
    }
    for(int i=0;i<5;++i) {
        float z=-19.0F+i*10.0F, x=8.0F+std::sin(z*0.035F)*4.2F+4.5F, y=Height(x,z);
        b.cylinder({x,y, z},0.12F,6.4F,8,0.32F,0.2F,0.11F,0.75F);
        b.box({x,y+5.9F,z},{0.12F,0.08F,1.2F},0.28F,0.22F,0.16F);
        b.box({x,y+5.7F,z-0.7F},{0.055F,0.055F,0.45F},0.23F,0.2F,0.15F);
    }
    // Fallen timber and splintered branches around the clearings.
    for(int i=0;i<34;++i) {
        float x=-18.0F+Hash(i,89)*36.0F, z=-18.0F+Hash(i,97)*36.0F;
        if(x*x+z*z<45.0F) continue;
        float y=Height(x,z)+0.18F, len=1.2F+Hash(i,101)*2.8F;
        b.cylinder({x-len*0.5F,y,z},0.13F+Hash(i,103)*0.09F,len,9,0.25F,0.14F,0.075F,0.98F);
        // Broken branch stubs and exposed pale cut ends make fallen trees less like plain cylinders.
        if(i%2==0) {
            b.beam({x-len*0.12F,y,z},{x-len*0.25F,y+0.35F,z+0.22F},0.045F,0.24F,0.14F,0.075F);
            b.cylinder({x+len*0.5F,y,z},0.105F,0.018F,9,0.48F,0.34F,0.20F,1.0F);
        }
    }

    // Loose gravel, scattered across the road surface with natural size and color variation.
    for (int i = 0; i < 620; ++i) {
        const float z = -64.0F + Hash(i, 107) * 128.0F;
        const float roadX = 8.0F + std::sin(z * 0.035F) * 4.2F;
        const float x = roadX + (Hash(i, 109) - 0.5F) * 5.1F;
        if (Hash(i, 113) < 0.08F) continue; // Leave irregular bare patches.
        const float y = Height(x,z) + 0.065F;
        const float size = 0.025F + Hash(i, 127) * 0.065F;
        const float shade = 0.72F + Hash(i, 131) * 0.45F;
        b.rock({x,y,z},size,size*0.45F,size*(0.55F+Hash(i,137)*0.6F),
               0.30F*shade);
    }

    // Ferns form irregular understory clusters, especially in shaded woodland and near the stream.
    for (int i = 0; i < 340; ++i) {
        const float x = -57.0F + Hash(i, 139) * 114.0F;
        const float z = -61.0F + Hash(i, 149) * 122.0F;
        const float roadX = 8.0F + std::sin(z * 0.035F) * 4.2F;
        if (x*x+z*z < 34.0F || std::abs(x-roadX) < 4.1F ||
            (x > -26.0F && x < -20.0F)) continue;
        const float y = Height(x,z) + 0.035F;
        const float scale = 0.35F + Hash(i, 151) * 0.55F;
        const float rotation = Hash(i, 157) * 2.0F * kPi;
        for (int leaf = 0; leaf < 7; ++leaf) {
            const float a = rotation + leaf * (2.0F*kPi/7.0F);
            const float length = scale * (0.65F + Hash(i, leaf+163)*0.5F);
            const float width = length * 0.19F;
            const P base{x,y,z};
            const P tip{x+std::cos(a)*length*0.8F,
                        y+length*(0.28F+Hash(i,leaf+173)*0.18F),
                        z+std::sin(a)*length*0.8F};
            const P side{x-std::sin(a)*width,y+length*0.35F,
                         z+std::cos(a)*width};
            const float green = 0.16F + Hash(i,leaf+179)*0.12F;
            b.tri(base,side,tip,0.045F,green,0.055F);
            b.tri(base,tip,{x+std::sin(a)*width,y+length*0.35F,
                            z-std::cos(a)*width},0.055F,green*1.05F,0.06F);
        }
    }

    // Leaf litter and small broken twigs break up the otherwise uniform ground plane.
    for (int i = 0; i < 1450; ++i) {
        const float x = -61.0F + Hash(i, 181) * 122.0F;
        const float z = -64.0F + Hash(i, 191) * 128.0F;
        const float roadX = 8.0F + std::sin(z * 0.035F) * 4.2F;
        if (x*x+z*z < 24.0F || std::abs(x-roadX) < 3.9F) continue;
        const float y = Height(x,z) + 0.032F;
        const float a = Hash(i, 193) * kPi;
        const float length = 0.06F + Hash(i, 197) * 0.19F;
        const float width = length * (0.18F + Hash(i, 199) * 0.18F);
        const P along{std::cos(a)*length,0.003F,std::sin(a)*length};
        const P across{-std::sin(a)*width,0.001F,std::cos(a)*width};
        const P center{x,y,z};
        const float tint = 0.75F + Hash(i, 211) * 0.55F;
        const float red = (i%4==0 ? 0.27F : 0.16F) * tint;
        const float green = (i%4==0 ? 0.19F : 0.13F) * tint;
        const float blue = (i%4==0 ? 0.075F : 0.055F) * tint;
        b.quad(center-along-across,center+along-across,center+along+across,center-along+across,
               red,green,blue);
        if (i%5==0) {
            const P twigEnd{x+std::cos(a)*length*1.8F,y+0.015F,z+std::sin(a)*length*1.8F};
            b.beam(center,twigEnd,0.009F,0.20F,0.12F,0.06F);
        }
    }

    // Dense, individually varied grass tufts create a more natural transition between
    // the cleared road, understory, and forest floor. Three crossed blades per tuft
    // keep the mesh static and draw-call friendly while breaking up distant repetition.
    for (int i = 0; i < 3600; ++i) {
        const float x = -63.0F + Hash(i, 241) * 126.0F;
        const float z = -65.0F + Hash(i, 251) * 130.0F;
        const float roadX = 8.0F + std::sin(z * 0.035F) * 4.2F;
        const float creekX = -23.0F + std::sin(z * 0.09F) * 2.8F;
        if (std::abs(x - roadX) < 3.9F || std::abs(x - creekX) < 2.0F ||
            x*x + z*z < 22.0F || (x > -26.0F && x < -20.0F)) continue;
        const float y = Height(x, z) + 0.028F;
        const float scale = 0.72F + Hash(i, 257) * 0.8F;
        const float hue = Hash(i, 263);
        const float red = 0.035F + hue * 0.055F;
        const float green = 0.15F + hue * 0.18F;
        const float blue = 0.035F + hue * 0.045F;
        const float rotation = Hash(i, 269) * 2.0F * kPi;
        for (int blade = 0; blade < 3; ++blade) {
            const float angle = rotation + blade * (kPi / 3.0F);
            const float width = (0.018F + Hash(i, blade + 271) * 0.025F) * scale;
            const float height = (0.16F + Hash(i, blade + 277) * 0.42F) * scale;
            const P side{-std::sin(angle) * width, 0.0F, std::cos(angle) * width};
            const P base{x + std::cos(angle) * width * 0.35F, y, z + std::sin(angle) * width * 0.35F};
            const P tip{x + std::cos(angle) * height * 0.18F, y + height,
                        z + std::sin(angle) * height * 0.18F};
            const float shade = 0.78F + Hash(i, blade + 283) * 0.38F;
            b.tri(base - side, base + side, tip, red * shade, green * shade, blue * shade);
        }
    }

    // Pebble clusters and exposed mineral fragments follow the stream banks and uneven ground.
    for (int i = 0; i < 720; ++i) {
        const float z = -61.0F + Hash(i, 293) * 122.0F;
        const float creekX = -23.0F + std::sin(z * 0.09F) * 2.8F;
        const float side = Hash(i, 307) < 0.5F ? -1.0F : 1.0F;
        const float x = creekX + side * (1.8F + Hash(i, 311) * 3.5F);
        if (x*x + z*z < 28.0F) continue;
        const float y = Height(x, z) + 0.035F;
        const float size = 0.045F + Hash(i, 313) * 0.22F;
        const float tint = 0.68F + Hash(i, 317) * 0.52F;
        b.rock({x, y + size * 0.18F, z}, size, size * (0.28F + Hash(i, 331) * 0.28F),
            size * (0.55F + Hash(i, 337) * 0.8F), 0.31F * tint);
    }

    // Tiny wildflowers add restrained color variation to grass verges and clearings.
    for (int i = 0; i < 150; ++i) {
        const float x = -56.0F + Hash(i, 223) * 112.0F;
        const float z = -60.0F + Hash(i, 227) * 120.0F;
        const float roadX = 8.0F + std::sin(z * 0.035F) * 4.2F;
        if (std::abs(x-roadX) < 4.0F || x*x+z*z < 26.0F) continue;
        const float y = Height(x,z) + 0.04F;
        const float h = 0.12F + Hash(i, 229) * 0.18F;
        b.beam({x,y,z},{x,y+h,z},0.009F,0.12F,0.24F,0.075F);
        const float petal = 0.025F + Hash(i, 233) * 0.018F;
        const float color = Hash(i, 239);
        const float r = color < 0.45F ? 0.85F : (color < 0.75F ? 0.9F : 0.96F);
        const float g = color < 0.45F ? 0.79F : (color < 0.75F ? 0.42F : 0.85F);
        const float bl = color < 0.45F ? 0.48F : (color < 0.75F ? 0.16F : 0.2F);
        b.rock({x,y+h,z},petal,petal*0.3F,petal,0.6F);
        b.tri({x-petal,y+h,z},{x,y+h+petal*0.2F,z+petal},{x+petal,y+h,z},r,g,bl);
    }
}
} // namespace vanta::renderer
