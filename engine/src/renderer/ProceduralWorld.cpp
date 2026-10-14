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
float Height(float x, float z) {
    return -1.0F + 0.68F*std::sin(x*0.075F)*std::cos(z*0.061F)
        + 0.28F*std::sin((x+z)*0.17F) + 0.15F*std::cos(z*0.29F)*std::sin(x*0.23F);
}
float Hash(int x, int z, int salt=0) {
    std::uint32_t n=static_cast<std::uint32_t>(x)*374761393U + static_cast<std::uint32_t>(z)*668265263U + static_cast<std::uint32_t>(salt)*2246822519U;
    n=(n^(n>>13U))*1274126177U; n^=n>>16U;
    return static_cast<float>(n & 0x00ffffffU)/16777215.0F;
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
                b.quad({lx0,ly0,z0},{lx1,ly1,z0},{lx1,ly1,z1},{lx0,ly0,z1},
                    0.15F*shade,0.17F*shade,0.18F*shade);
                const float rx0 = x + roofOuterX * t0;
                const float rx1 = x + roofOuterX * t1;
                const float ry0 = y + h + 0.9F * scale * (1.0F - t0) + 0.018F * scale;
                const float ry1 = y + h + 0.9F * scale * (1.0F - t1) + 0.018F * scale;
                b.quad({rx0,ry0,z0},{rx1,ry1,z0},{rx1,ry1,z1},{rx0,ry0,z1},
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
                b.box({x+side*(w+0.018F*scale),py,z},{0.028F*scale,0.035F*scale,d*0.94F},
                    0.25F*tone,0.155F*tone,0.085F*tone);
            }
        }
        // Door boards and a threshold make the entrance read as constructed timber.
        for (int plank = 0; plank < 5; ++plank) {
            b.box({x-0.31F*scale+plank*0.155F*scale,y+0.82F*scale,z-d-0.175F*scale},
                {0.012F*scale,0.72F*scale,0.012F*scale},0.23F,0.135F,0.075F);
        }
        box({x,y+0.045F*scale,z-d-0.65F*scale},{1.05F*scale,0.07F*scale,0.65F*scale},0.32F,0.23F,0.13F);
        for(int i=0;i<5;++i) box({x-0.8F*scale+i*0.4F*scale,y+0.08F*scale,z-d-1.18F*scale},{0.17F*scale,0.08F*scale,0.25F*scale},0.29F,0.19F,0.11F);
        box({x+1.25F*scale,y+h+0.7F*scale,z+0.45F*scale},{0.24F*scale,0.9F*scale,0.24F*scale},0.3F,0.27F,0.23F);
        box({x+1.25F*scale,y+h+1.55F*scale,z+0.45F*scale},{0.3F*scale,0.06F*scale,0.3F*scale},0.12F,0.12F,0.12F);
        // Staggered mortar seams on the chimney's visible faces.
        for (int course = 0; course < 5; ++course) {
            const float cy = y+h+0.18F*scale+course*0.27F*scale;
            b.box({x+1.25F*scale,cy,z+0.202F*scale},{0.245F*scale,0.012F*scale,0.012F*scale},0.19F,0.18F,0.16F);
            const float offset = (course % 2 == 0) ? 0.0F : 0.12F*scale;
            b.box({x+1.25F*scale-0.12F*scale+offset,cy+0.12F*scale,z+0.202F*scale},
                {0.009F*scale,0.12F*scale,0.013F*scale},0.18F,0.17F,0.15F);
        }
        // Foundation courses wrap the cabin rather than leaving four isolated corner stones.
        for (int stone = 0; stone < 9; ++stone) {
            const float sx = x-w+2.0F*w*(stone+0.5F)/9.0F;
            b.box({sx,y+0.12F*scale,z-d-0.015F*scale},{w/9.0F*0.91F,0.13F*scale,0.12F*scale},
                0.32F+0.035F*Hash(stone,1),0.31F,0.28F);
            b.box({sx,y+0.12F*scale,z+d+0.015F*scale},{w/9.0F*0.91F,0.13F*scale,0.12F*scale},
                0.29F+0.035F*Hash(stone,2),0.29F,0.27F);
        }
    }
};
}

void BuildDetailedWorld(std::vector<WorldVertex>& vertices, std::vector<unsigned int>& indices)
{
    Builder b{vertices,indices};
    // A worn access road with shoulders, broken center paint, drainage ditches and a creek crossing.
    for(int i=0;i<130;++i) {
        const float z0=-65.0F+i, z1=z0+1.0F;
        const float x0=8.0F+std::sin(z0*0.035F)*4.2F, x1=8.0F+std::sin(z1*0.035F)*4.2F;
        const float y0=Height(x0,z0)+0.035F, y1=Height(x1,z1)+0.035F;
        b.quad({x0-2.8F,y0,z0},{x1-2.8F,y1,z1},{x1+2.8F,y1,z1},{x0+2.8F,y0,z0},
            0.24F+0.035F*Hash(i,1),0.235F+0.03F*Hash(i,3),0.22F+0.025F*Hash(i,5));
        b.quad({x0-3.5F,y0-0.025F,z0},{x1-3.5F,y1-0.025F,z1},{x1-2.8F,y1,z1},{x0-2.8F,y0,z0},0.29F,0.27F,0.23F);
        b.quad({x0+2.8F,y0,z0},{x1+2.8F,y1,z1},{x1+3.5F,y1-0.025F,z1},{x0+3.5F,y0-0.025F,z0},0.29F,0.27F,0.23F);
        if(i%9<5) b.quad({x0-0.055F,y0+0.012F,z0},{x1-0.055F,y1+0.012F,z1},{x1+0.055F,y1+0.012F,z1},{x0+0.055F,y0+0.012F,z0},0.62F,0.53F,0.34F);
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
        b.cylinder({x,y,z},0.18F+Hash(gx,gz,7)*0.1F,h*0.42F,7,0.22F,0.12F,0.065F,0.72F);
        for(int tier=0;tier<4;++tier) {
            const float ty=y+h*(0.19F+tier*0.17F), th=h*(0.43F-tier*0.045F);
            const float rr=radius*(1.0F-tier*0.19F);
            b.cone({x,ty,z},rr,th,7,0.035F+Hash(gx,tier)*0.025F,0.18F+Hash(gz,tier)*0.08F,0.09F+Hash(gx,gz)*0.035F);
            // Downward boughs add a layered silhouette instead of a single primitive cone.
            if(tier<3) for(int arm=0;arm<5;++arm) {
                float a=arm*2*kPi/5.0F+Hash(gx,gz,arm);
                P end{x+std::cos(a)*rr*0.9F,ty+th*0.23F,z+std::sin(a)*rr*0.9F};
                b.cone({end.x, end.y-0.1F,end.z},rr*0.26F,th*0.28F,5,0.045F,0.22F,0.105F);
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
        b.cylinder({x-len*0.5F,y,z},0.13F+Hash(i,103)*0.09F,len,7,0.25F,0.14F,0.075F,0.98F);
        if(i%2==0) b.cone({x+len*0.15F,y+0.04F,z+0.15F},0.07F,0.65F,5,0.24F,0.15F,0.08F);
    }
}
} // namespace vanta::renderer
