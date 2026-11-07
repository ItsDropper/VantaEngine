#include "vanta/physics/PhysicsWorld.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using vanta::Aabb;
using vanta::ColliderDescription;
using vanta::PhysicsWorld;
using vanta::RaycastHit;
using vanta::math::Vec3;

struct TestCase { std::string_view name; bool (*run)(); };

ColliderDescription Box(float minX,float minY,float minZ,float maxX,float maxY,float maxZ,
                        std::uint32_t layer=1,bool trigger=false)
{
    return {{{minX,minY,minZ},{maxX,maxY,maxZ}},layer,trigger};
}

bool addUpdateRemoveAndStaleHandles()
{
    PhysicsWorld world;
    const auto first = world.AddCollider(Box(-1,-1,-1,1,1,1));
    if (!first.IsValid() || !world.Contains(first) || world.ColliderCount() != 1)
        return false;
    if (!world.UpdateCollider(first, Box(2,2,2,3,3,3)) || !world.RemoveCollider(first))
        return false;
    const auto second = world.AddCollider(Box(0,0,0,1,1,1));
    return second.IsValid() && second.index == first.index &&
           second.generation != first.generation && !world.Contains(first) &&
           world.Contains(second) && world.ColliderCount() == 1;
}

bool rejectsInvalidColliderDescriptions()
{
    PhysicsWorld world;
    const auto inverted = world.AddCollider(Box(2,0,0,1,1,1));
    const auto zeroLayer = world.AddCollider(Box(0,0,0,1,1,1,0));
    const auto nanBounds = world.AddCollider({{{std::numeric_limits<float>::quiet_NaN(),0,0},{1,1,1}},1,false});
    return !inverted.IsValid() && !zeroLayer.IsValid() && !nanBounds.IsValid() &&
           world.ColliderCount() == 0;
}

bool raycastFindsNearestAndCorrectNormal()
{
    PhysicsWorld world;
    (void)world.AddCollider(Box(5,-1,-1,6,1,1));
    const auto near = world.AddCollider(Box(2,-1,-1,3,1,1));
    RaycastHit hit{};
    if (!world.Raycast({0,0,0},{4,0,0},10,hit))
        return false;
    return hit.collider == near && std::abs(hit.distance-2.0F)<1.0e-5F &&
           std::abs(hit.position.x-2.0F)<1.0e-5F && hit.normal.x == -1.0F &&
           !hit.startedInside;
}

bool raycastHandlesParallelAxesAndInsideOrigins()
{
    PhysicsWorld world;
    const auto handle = world.AddCollider(Box(-1,-1,-1,1,1,1));
    RaycastHit hit{};
    const bool parallelMiss = !world.Raycast({2,0,-3},{0,0,1},10,hit);
    const bool insideHit = world.Raycast({0,0,0},{0,0,5},10,hit);
    return parallelMiss && insideHit && hit.collider == handle &&
           hit.distance == 0.0F && hit.startedInside && hit.normal.z == -1.0F;
}

bool raycastHonorsDistanceAndLayerMask()
{
    PhysicsWorld world;
    (void)world.AddCollider(Box(2,-1,-1,3,1,1,2));
    RaycastHit hit{};
    const bool masked = !world.Raycast({0,0,0},{1,0,0},10,hit,1);
    const bool tooShort = !world.Raycast({0,0,0},{1,0,0},1,hit,2);
    const bool found = world.Raycast({0,0,0},{1,0,0},2,hit,2);
    return masked && tooShort && found && std::abs(hit.distance-2.0F)<1.0e-5F;
}

bool triggerFilteringIsConsistent()
{
    PhysicsWorld world;
    const auto solid = world.AddCollider(Box(2,-1,-1,3,1,1,1,false));
    (void)world.AddCollider(Box(4,-1,-1,5,1,1,1,true));
    RaycastHit hit{};
    if (!world.Raycast({0,0,0},{1,0,0},10,hit,1,false) || hit.collider != solid)
        return false;
    std::vector<vanta::ColliderHandle> overlaps;
    return world.OverlapAabb(Box(-10,-10,-10,10,10,10).bounds,overlaps,1,false) &&
           overlaps.size() == 1 && overlaps[0] == solid;
}

bool sphereOverlapUsesClosestPoint()
{
    PhysicsWorld world;
    const auto handle = world.AddCollider(Box(0,0,0,1,1,1));
    std::vector<vanta::ColliderHandle> overlaps;
    if (!world.OverlapSphere({1.5F,0.5F,0.5F},0.5F,overlaps) ||
        overlaps.size()!=1 || overlaps[0]!=handle)
        return false;
    if (!world.OverlapSphere({1.5F,1.5F,0.5F},0.5F,overlaps))
        return false;
    return overlaps.empty();
}

bool invalidQueriesClearResults()
{
    PhysicsWorld world;
    (void)world.AddCollider(Box(0,0,0,1,1,1));
    std::vector<vanta::ColliderHandle> overlaps(2);
    const bool invalidSphere = !world.OverlapSphere({0,0,0},-1,overlaps);
    const bool clearedSphere = overlaps.empty();
    overlaps.resize(2);
    const bool invalidBox = !world.OverlapAabb(Box(1,0,0,0,1,1).bounds,overlaps);
    const bool clearedBox = overlaps.empty();
    RaycastHit hit{};
    const bool invalidRay = !world.Raycast({0,0,0},{0,0,0},10,hit);
    return invalidSphere && clearedSphere && invalidBox && clearedBox && invalidRay;
}


bool verticalCapsuleOverlapUsesSegmentDistance()
{
    PhysicsWorld world;
    const auto floor = world.AddCollider(Box(-5,-1,-5,5,0,5));
    const auto wall = world.AddCollider(Box(1,0,-1,2,3,1));
    std::vector<vanta::ColliderHandle> overlaps;
    if (!world.OverlapVerticalCapsule({0,1.0F,0},0.5F,1.0F,overlaps))
        return false;
    if (overlaps.size()!=1 || overlaps[0]!=floor)
        return false;
    if (!world.OverlapVerticalCapsule({0.6F,1.0F,0},0.5F,1.0F,overlaps))
        return false;
    return overlaps.size()==2 && overlaps[0]==floor && overlaps[1]==wall;
}

bool capsuleRejectsInvalidDimensions()
{
    PhysicsWorld world;
    (void)world.AddCollider(Box(-1,-1,-1,1,1,1));
    std::vector<vanta::ColliderHandle> overlaps(1);
    const bool negativeRadius = !world.OverlapVerticalCapsule({0,0,0},-0.1F,1.0F,overlaps);
    const bool tooShort = !world.OverlapVerticalCapsule({0,0,0},0.5F,0.25F,overlaps);
    return negativeRadius && tooShort && overlaps.empty();
}

bool failedRaycastClearsPreviousHit()
{
    PhysicsWorld world;
    (void)world.AddCollider(Box(1,-1,-1,2,1,1));
    RaycastHit hit{};
    if (!world.Raycast({0,0,0},{1,0,0},5,hit))
        return false;
    const bool missed = !world.Raycast({0,0,0},{0,1,0},5,hit);
    return missed && !hit.collider.IsValid();
}

constexpr std::array kCases{
    TestCase{"collider lifecycle invalidates stale handles",addUpdateRemoveAndStaleHandles},
    TestCase{"invalid collider descriptions are rejected",rejectsInvalidColliderDescriptions},
    TestCase{"raycast returns nearest hit and surface normal",raycastFindsNearestAndCorrectNormal},
    TestCase{"raycast handles parallel axes and inside origins",raycastHandlesParallelAxesAndInsideOrigins},
    TestCase{"raycast respects distance and layers",raycastHonorsDistanceAndLayerMask},
    TestCase{"trigger filtering applies to queries",triggerFilteringIsConsistent},
    TestCase{"sphere overlap uses closest point",sphereOverlapUsesClosestPoint},
    TestCase{"invalid overlap queries clear output",invalidQueriesClearResults},
    TestCase{"vertical capsule overlap uses segment distance",verticalCapsuleOverlapUsesSegmentDistance},
    TestCase{"vertical capsule rejects invalid dimensions",capsuleRejectsInvalidDimensions},
    TestCase{"failed raycast clears stale hit data",failedRaycastClearsPreviousHit},
};
}

int main()
{
    int failures=0;
    for(const TestCase& test:kCases)
    {
        if(!test.run()){std::cerr<<"FAIL: "<<test.name<<'\n';++failures;}
        else{std::cout<<"PASS: "<<test.name<<'\n';}
    }
    std::cout<<(kCases.size()-static_cast<std::size_t>(failures))
             <<'/'<<kCases.size()<<" physics tests passed.\n";
    return failures==0?0:1;
}
