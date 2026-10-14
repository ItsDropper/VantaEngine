#include "vanta/math/Math.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
namespace {
using namespace vanta::math;
constexpr float e=1e-4F;
bool near(float a,float b){return std::abs(a-b)<=e;}
struct Case{std::string_view name;bool(*run)();};
bool vectors(){auto a=Vec3{1,2,3}+Vec3{4,-2,1};return near(a.x,5)&&near(a.y,0)&&near(a.z,4)&&near(Vec3{1,2,3}.Dot({4,-2,1}),3);}
bool cross(){auto v=Vec3{1,0,0}.Cross({0,1,0});return near(v.x,0)&&near(v.y,0)&&near(v.z,1);}
bool zeroNorm(){return !Vec3{}.Normalized();}
bool norm(){auto v=Vec3{3,0,4}.Normalized();return v&&near(v->Length(),1);}
bool pointsDirections(){auto m=Mat4::Translation({5,-2,7});auto p=m.Transform({1,2,3,1});auto d=m.Transform({1,2,3,0});return near(p.x,6)&&near(p.y,0)&&near(p.z,10)&&near(d.x,1)&&near(d.y,2)&&near(d.z,3);}
bool composition(){auto m=Mat4::Translation({10,0,0})*Mat4::Scale({2,2,2});return near(m.Transform({1,0,0,1}).x,12);}
bool quaternion(){auto q=Quaternion::FromAxisAngle({0,1,0},DegreesToRadians(90));if(!q)return false;auto v=q->Rotate({0,0,1});return near(v.x,1)&&near(v.y,0)&&near(v.z,0);}
bool rotationMatrix(){auto q=Quaternion::FromAxisAngle({0,1,0},DegreesToRadians(90));if(!q)return false;auto v=Mat4::Rotation(*q).Transform({0,0,1,0});return near(v.x,1)&&near(v.z,0);}
bool perspectiveDepth(){auto m=Mat4::Perspective(DegreesToRadians(60),16.0F/9.0F,.1F,100);auto n=m.Transform({0,0,.1F,1});auto f=m.Transform({0,0,100,1});return near(n.z/n.w,0)&&near(f.z/f.w,1);}
bool invalidProjection(){auto m=Mat4::Perspective(DegreesToRadians(60),0,.1F,100);return near(m.At(0,0),0)&&near(m.At(1,1),0);}
bool lookAt(){auto m=Mat4::LookAtLH({0,0,-5},{0,0,0},{0,1,0});if(!m)return false;auto eye=m->Transform({0,0,-5,1});auto target=m->Transform({0,0,0,1});return near(eye.x,0)&&near(eye.y,0)&&near(eye.z,0)&&near(target.z,5);}
bool invalidLookAt(){return !Mat4::LookAtLH({0,0,0},{0,0,0},{0,1,0})&&!Mat4::LookAtLH({0,0,0},{0,0,1},{0,0,1});}
constexpr std::array cases{Case{"vector arithmetic and dot product",vectors},Case{"cross product",cross},Case{"zero vector normalization",zeroNorm},Case{"unit normalization",norm},Case{"point and direction transforms",pointsDirections},Case{"matrix composition",composition},Case{"quaternion axis-angle",quaternion},Case{"rotation matrix",rotationMatrix},Case{"Direct3D perspective depth",perspectiveDepth},Case{"invalid projection parameters",invalidProjection},Case{"left-handed look-at",lookAt},Case{"degenerate look-at",invalidLookAt}};
}
int main(){int failures=0;for(const auto& c:cases){if(!c.run()){std::cerr<<"FAIL: "<<c.name<<'\n';++failures;}else std::cout<<"PASS: "<<c.name<<'\n';}std::cout<<(cases.size()-static_cast<std::size_t>(failures))<<'/'<<cases.size()<<" math tests passed.\n";return failures?1:0;}
