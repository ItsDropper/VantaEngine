#pragma once
#include <cmath>
#include <optional>

namespace vanta::math {
inline constexpr float kEpsilon = 1.0e-6F;
inline constexpr float kPi = 3.14159265358979323846F;
[[nodiscard]] constexpr float DegreesToRadians(float degrees) noexcept { return degrees * (kPi / 180.0F); }

struct Vec3 {
    float x=0, y=0, z=0;
    [[nodiscard]] constexpr Vec3 operator+(const Vec3& r) const noexcept { return {x+r.x,y+r.y,z+r.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& r) const noexcept { return {x-r.x,y-r.y,z-r.z}; }
    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return {-x,-y,-z}; }
    [[nodiscard]] constexpr Vec3 operator*(float s) const noexcept { return {x*s,y*s,z*s}; }
    [[nodiscard]] constexpr Vec3 operator/(float s) const noexcept { return {x/s,y/s,z/s}; }
    constexpr Vec3& operator+=(const Vec3& r) noexcept { x+=r.x; y+=r.y; z+=r.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& r) noexcept { x-=r.x; y-=r.y; z-=r.z; return *this; }
    [[nodiscard]] constexpr float LengthSquared() const noexcept { return x*x+y*y+z*z; }
    [[nodiscard]] float Length() const noexcept { return std::sqrt(LengthSquared()); }
    [[nodiscard]] constexpr float Dot(const Vec3& r) const noexcept { return x*r.x+y*r.y+z*r.z; }
    [[nodiscard]] constexpr Vec3 Cross(const Vec3& r) const noexcept { return {y*r.z-z*r.y,z*r.x-x*r.z,x*r.y-y*r.x}; }
    [[nodiscard]] std::optional<Vec3> Normalized(float epsilon=kEpsilon) const noexcept {
        const float ls=LengthSquared(); if (ls<=epsilon*epsilon) return std::nullopt; return *this/std::sqrt(ls);
    }
};
[[nodiscard]] constexpr Vec3 operator*(float s,const Vec3& v) noexcept { return v*s; }

struct Vec4 { float x=0,y=0,z=0,w=0; };

struct Quaternion {
    float x=0,y=0,z=0,w=1;
    [[nodiscard]] static Quaternion Identity() noexcept { return {}; }
    [[nodiscard]] static std::optional<Quaternion> FromAxisAngle(const Vec3& axis,float radians) noexcept {
        const auto n=axis.Normalized(); if(!n) return std::nullopt;
        const float h=radians*0.5F, s=std::sin(h); return Quaternion{n->x*s,n->y*s,n->z*s,std::cos(h)};
    }
    [[nodiscard]] float LengthSquared() const noexcept { return x*x+y*y+z*z+w*w; }
    [[nodiscard]] std::optional<Quaternion> Normalized(float epsilon=kEpsilon) const noexcept {
        const float ls=LengthSquared(); if(ls<=epsilon*epsilon) return std::nullopt;
        const float inv=1.0F/std::sqrt(ls); return Quaternion{x*inv,y*inv,z*inv,w*inv};
    }
    [[nodiscard]] constexpr Quaternion Conjugate() const noexcept { return {-x,-y,-z,w}; }
    [[nodiscard]] constexpr Quaternion operator*(const Quaternion& r) const noexcept {
        return {w*r.x+x*r.w+y*r.z-z*r.y,w*r.y-x*r.z+y*r.w+z*r.x,w*r.z+x*r.y-y*r.x+z*r.w,w*r.w-x*r.x-y*r.y-z*r.z};
    }
    [[nodiscard]] Vec3 Rotate(const Vec3& v) const noexcept {
        const Quaternion p{v.x,v.y,v.z,0}; const Quaternion r=(*this)*p*Conjugate(); return {r.x,r.y,r.z};
    }
};

// Column-major storage, column vectors (M * v), translation at column 3.
// Projection follows Direct3D's left-handed convention and [0,1] normalized depth.
struct Mat4 {
    float elements[16]{};
    [[nodiscard]] constexpr float& At(int row,int column) noexcept { return elements[column*4+row]; }
    [[nodiscard]] constexpr float At(int row,int column) const noexcept { return elements[column*4+row]; }
    [[nodiscard]] static constexpr Mat4 Identity() noexcept {
        Mat4 m{}; m.At(0,0)=m.At(1,1)=m.At(2,2)=m.At(3,3)=1; return m;
    }
    [[nodiscard]] static constexpr Mat4 Translation(const Vec3& p) noexcept {
        Mat4 m=Identity(); m.At(0,3)=p.x; m.At(1,3)=p.y; m.At(2,3)=p.z; return m;
    }
    [[nodiscard]] static constexpr Mat4 Scale(const Vec3& s) noexcept {
        Mat4 m{}; m.At(0,0)=s.x; m.At(1,1)=s.y; m.At(2,2)=s.z; m.At(3,3)=1; return m;
    }
    [[nodiscard]] static Mat4 Rotation(const Quaternion& q0) noexcept {
        const auto nq=q0.Normalized(); if(!nq) return Identity(); const auto q=*nq;
        const float xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z,xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z,wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
        Mat4 m=Identity();
        m.At(0,0)=1-2*(yy+zz); m.At(0,1)=2*(xy-wz); m.At(0,2)=2*(xz+wy);
        m.At(1,0)=2*(xy+wz); m.At(1,1)=1-2*(xx+zz); m.At(1,2)=2*(yz-wx);
        m.At(2,0)=2*(xz-wy); m.At(2,1)=2*(yz+wx); m.At(2,2)=1-2*(xx+yy); return m;
    }
    [[nodiscard]] static Mat4 Perspective(float fovY,float aspect,float nearZ,float farZ) noexcept {
        if(!(fovY>0&&fovY<kPi&&aspect>0&&nearZ>0&&farZ>nearZ)) return {};
        const float y=1/std::tan(fovY*0.5F); Mat4 m{};
        m.At(0,0)=y/aspect; m.At(1,1)=y; m.At(2,2)=farZ/(farZ-nearZ);
        m.At(2,3)=-(nearZ*farZ)/(farZ-nearZ); m.At(3,2)=1; return m;
    }
    [[nodiscard]] static std::optional<Mat4> LookAtLH(const Vec3& eye,const Vec3& target,const Vec3& up) noexcept {
        const auto f=(target-eye).Normalized(); if(!f) return std::nullopt;
        const auto r=up.Cross(*f).Normalized(); if(!r) return std::nullopt;
        const Vec3 u=f->Cross(*r); Mat4 m=Identity();
        m.At(0,0)=r->x;m.At(0,1)=r->y;m.At(0,2)=r->z;m.At(0,3)=-r->Dot(eye);
        m.At(1,0)=u.x;m.At(1,1)=u.y;m.At(1,2)=u.z;m.At(1,3)=-u.Dot(eye);
        m.At(2,0)=f->x;m.At(2,1)=f->y;m.At(2,2)=f->z;m.At(2,3)=-f->Dot(eye); return m;
    }
    [[nodiscard]] constexpr Mat4 operator*(const Mat4& r) const noexcept {
        Mat4 m{}; for(int row=0;row<4;++row) for(int col=0;col<4;++col) for(int k=0;k<4;++k) m.At(row,col)+=At(row,k)*r.At(k,col); return m;
    }
    [[nodiscard]] constexpr Vec4 Transform(const Vec4& v) const noexcept {
        return {At(0,0)*v.x+At(0,1)*v.y+At(0,2)*v.z+At(0,3)*v.w,
                At(1,0)*v.x+At(1,1)*v.y+At(1,2)*v.z+At(1,3)*v.w,
                At(2,0)*v.x+At(2,1)*v.y+At(2,2)*v.z+At(2,3)*v.w,
                At(3,0)*v.x+At(3,1)*v.y+At(3,2)*v.z+At(3,3)*v.w};
    }
};
} // namespace vanta::math
