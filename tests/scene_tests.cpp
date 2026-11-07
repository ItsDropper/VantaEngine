#include "vanta/core/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
using namespace vanta;
constexpr float kTolerance = 1.0e-4F;
bool Near(float left,float right){return std::abs(left-right)<=kTolerance;}
struct TestCase{std::string_view name;bool(*run)();};

bool transformComposesTranslationRotationAndScale()
{
    Transform transform;
    if(!transform.SetPosition({3,4,5}) ||
       !transform.SetRotation({0,0,0,2}) ||
       !transform.SetScale({2,3,4}))
        return false;
    const auto point=transform.Matrix().Transform({1,1,1,1});
    return Near(point.x,5)&&Near(point.y,7)&&Near(point.z,9)&&Near(point.w,1);
}

bool transformRejectsInvalidValues()
{
    Transform transform;
    const auto before=transform.Matrix();
    const bool position=!transform.SetPosition({std::numeric_limits<float>::infinity(),0,0});
    const bool rotation=!transform.SetRotation({0,0,0,0});
    const bool scale=!transform.SetScale({1,std::numeric_limits<float>::quiet_NaN(),1});
    const auto after=transform.Matrix();
    return position&&rotation&&scale&&Near(before.At(0,3),after.At(0,3))&&
           Near(after.At(0,0),1)&&Near(after.At(1,1),1);
}

bool entityHandlesAreGenerationSafe()
{
    Scene scene;
    const auto first=scene.CreateEntity("first");
    if(!scene.IsAlive(first)||scene.EntityCount()!=1||!scene.DestroyEntity(first))
        return false;
    const auto second=scene.CreateEntity("second");
    return second.IsValid()&&second.index==first.index&&
           second.generation!=first.generation&&!scene.IsAlive(first)&&
           scene.IsAlive(second)&&scene.EntityCount()==1;
}

bool hierarchyPropagatesWorldTransforms()
{
    Scene scene;
    const auto parent=scene.CreateEntity("parent");
    const auto child=scene.CreateEntity("child");
    scene.GetTransform(parent)->SetPosition({10,0,0});
    scene.GetTransform(child)->SetPosition({0,2,0});
    if(!scene.SetParent(child,parent))return false;
    const auto world=scene.GetWorldMatrix(child);
    if(!world)return false;
    const auto origin=world->Transform({0,0,0,1});
    return Near(origin.x,10)&&Near(origin.y,2)&&Near(origin.z,0);
}

bool hierarchyComposesRotationAndScale()
{
    Scene scene;
    const auto parent=scene.CreateEntity();
    const auto child=scene.CreateEntity();
    scene.GetTransform(parent)->SetScale({2,2,2});
    scene.GetTransform(child)->SetPosition({1,0,0});
    if(!scene.SetParent(child,parent))return false;
    const auto world=scene.GetWorldMatrix(child);
    if(!world)return false;
    const auto origin=world->Transform({0,0,0,1});
    return Near(origin.x,2)&&Near(origin.y,0)&&Near(origin.z,0);
}

bool reparentRejectsCyclesAndInvalidHandles()
{
    Scene scene;
    const auto a=scene.CreateEntity();
    const auto b=scene.CreateEntity();
    const auto c=scene.CreateEntity();
    if(!scene.SetParent(b,a)||!scene.SetParent(c,b))return false;
    return !scene.SetParent(a,c)&&!scene.SetParent(a,a)&&
           !scene.SetParent(a,{123,1})&&scene.GetParent(c)==b;
}

bool reparentingAndUnparentingUpdateWorldMatrix()
{
    Scene scene;
    const auto a=scene.CreateEntity();
    const auto b=scene.CreateEntity();
    const auto child=scene.CreateEntity();
    scene.GetTransform(a)->SetPosition({5,0,0});
    scene.GetTransform(b)->SetPosition({20,0,0});
    scene.GetTransform(child)->SetPosition({1,0,0});
    if(!scene.SetParent(child,a))return false;
    auto world=scene.GetWorldMatrix(child);
    if(!world||!Near(world->At(0,3),6)||!scene.SetParent(child,b))return false;
    world=scene.GetWorldMatrix(child);
    if(!world||!Near(world->At(0,3),21)||!scene.SetParent(child))return false;
    world=scene.GetWorldMatrix(child);
    return world&&Near(world->At(0,3),1);
}

bool destroyingParentDetachesChildren()
{
    Scene scene;
    const auto parent=scene.CreateEntity();
    const auto child=scene.CreateEntity();
    scene.SetParent(child,parent);
    if(!scene.DestroyEntity(parent))return false;
    return scene.IsAlive(child)&&!scene.GetParent(child).IsValid()&&
           scene.GetWorldMatrix(child).has_value();
}

bool activeStateInheritsThroughHierarchy()
{
    Scene scene;
    const auto parent=scene.CreateEntity();
    const auto child=scene.CreateEntity();
    scene.SetParent(child,parent);
    if(!scene.SetActive(parent,false)||scene.IsActiveInHierarchy(child))return false;
    if(!scene.SetActive(parent,true)||!scene.IsActiveInHierarchy(child))return false;
    return scene.SetActive(child,false)&&!scene.IsActiveInHierarchy(child);
}

bool snapshotsOwnNamesMatricesAndActiveState()
{
    Scene scene;
    const auto parent=scene.CreateEntity("root");
    const auto child=scene.CreateEntity("node");
    scene.GetTransform(parent)->SetPosition({7,0,0});
    scene.SetParent(child,parent);
    scene.SetActive(parent,false);
    auto snapshot=scene.CreateSnapshot();
    if(snapshot.size()!=2)return false;
    const auto found=std::find_if(snapshot.begin(),snapshot.end(),
        [&](const SceneEntitySnapshot& item){return item.entity==child;});
    if(found==snapshot.end()||found->active||found->name!="node"||
       !Near(found->worldMatrix.At(0,3),7))return false;
    scene.SetName(child,"renamed");
    scene.GetTransform(parent)->SetPosition({99,0,0});
    return found->name=="node"&&Near(found->worldMatrix.At(0,3),7);
}

constexpr std::array kCases{
    TestCase{"TRS transform composition",transformComposesTranslationRotationAndScale},
    TestCase{"transform rejects invalid values",transformRejectsInvalidValues},
    TestCase{"entity handles invalidate after destruction",entityHandlesAreGenerationSafe},
    TestCase{"hierarchy propagates parent transforms",hierarchyPropagatesWorldTransforms},
    TestCase{"hierarchy composes rotation and scale",hierarchyComposesRotationAndScale},
    TestCase{"reparent rejects cycles and invalid handles",reparentRejectsCyclesAndInvalidHandles},
    TestCase{"reparent and unparent update world matrix",reparentingAndUnparentingUpdateWorldMatrix},
    TestCase{"destroying parent detaches children",destroyingParentDetachesChildren},
    TestCase{"active state inherits through hierarchy",activeStateInheritsThroughHierarchy},
    TestCase{"snapshots own names matrices and active state",snapshotsOwnNamesMatricesAndActiveState}
};
}

int main()
{
    int failures=0;
    for(const auto& test:kCases)
    {
        if(!test.run()){std::cerr<<"FAIL: "<<test.name<<'\n';++failures;}
        else std::cout<<"PASS: "<<test.name<<'\n';
    }
    std::cout<<(kCases.size()-static_cast<std::size_t>(failures))
             <<'/'<<kCases.size()<<" scene tests passed.\n";
    return failures==0?0:1;
}
