#include "vanta/physics/PhysicsWorld.hpp"

#include <algorithm>
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

bool raycastUsesDoublePrecisionForLargeFiniteBounds()
{
    PhysicsWorld world;
    const auto handle = world.AddCollider(Box(1.0e38F,-1.0F,-1.0F,1.5e38F,1.0F,1.0F));
    RaycastHit hit{};
    if (!handle.IsValid() || !world.Raycast({-1.0e38F,0,0},{1,0,0},2.1e38F,hit))
        return false;
    return hit.collider == handle && std::isfinite(hit.distance) &&
           std::abs(hit.distance - 2.0e38F) < 1.0e32F &&
           hit.normal.x == -1.0F && std::isfinite(hit.position.x);
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


bool clearInvalidatesAllCollidersAndAllowsReuse()
{
    PhysicsWorld world;
    const auto first=world.AddCollider(Box(0,0,0,1,1,1));
    const auto second=world.AddCollider(Box(2,2,2,3,3,3));
    if(!first.IsValid()||!second.IsValid())return false;
    world.Clear();
    if(world.ColliderCount()!=0||world.Contains(first)||world.Contains(second))return false;
    const auto replacement=world.AddCollider(Box(-1,-1,-1,0,0,0));
    return replacement.IsValid()&&replacement.index==first.index&&
           replacement.generation!=first.generation&&world.ColliderCount()==1;
}

bool broadphaseBuildsDeterministicSolidAndTriggerPairs()
{
    PhysicsWorld world;
    const auto first = world.AddCollider(Box(0,0,0,2,2,2));
    const auto second = world.AddCollider(Box(1,1,1,3,3,3,2,true));
    const auto third = world.AddCollider(Box(1.5F,0,0,2.5F,1,1,1,false));
    (void)world.AddCollider(Box(10,10,10,11,11,11));
    std::vector<vanta::ColliderPair> pairs;
    if (!world.ComputeOverlaps(pairs) || pairs.size() != 3)
        return false;
    const std::array expected{
        vanta::ColliderPair{first, second, true},
        vanta::ColliderPair{first, third, false},
        vanta::ColliderPair{second, third, true}
    };
    if (!std::equal(pairs.begin(), pairs.end(), expected.begin(), expected.end()))
        return false;

    if (!world.ComputeOverlaps(pairs, 1U) || pairs.size() != 1 ||
        pairs.front() != vanta::ColliderPair{first, third, false})
        return false;
    return !world.ComputeOverlaps(pairs, 0U) && pairs.empty();
}

bool broadphasePairsTrackGenerationAfterColliderReuse()
{
    PhysicsWorld world;
    const auto first = world.AddCollider(Box(0,0,0,1,1,1));
    const auto oldSecond = world.AddCollider(Box(0,0,0,1,1,1));
    if (!world.RemoveCollider(oldSecond))
        return false;
    const auto replacement = world.AddCollider(Box(0,0,0,1,1,1));
    std::vector<vanta::ColliderPair> pairs;
    return replacement.IsValid() && replacement.index == oldSecond.index &&
           replacement.generation != oldSecond.generation &&
           world.ComputeOverlaps(pairs) && pairs.size() == 1 &&
           pairs.front() == vanta::ColliderPair{first, replacement, false};
}

bool broadphaseMatchesBruteForceAcrossDenseAndSparseLayouts()
{
    PhysicsWorld world;
    std::vector<Aabb> bounds;
    std::vector<vanta::ColliderHandle> handles;
    std::uint32_t state = 0xC001D00DU;
    const auto next = [&state]() {
        state = state * 1664525U + 1013904223U;
        return state;
    };
    for (std::size_t index = 0; index < 96; ++index)
    {
        const float x = static_cast<float>(next() % 80U) * 0.25F;
        const float y = static_cast<float>(next() % 24U) * 0.25F;
        const float z = static_cast<float>(next() % 80U) * 0.25F;
        const float extent = 0.25F + static_cast<float>(next() % 8U) * 0.25F;
        const Aabb box{{x,y,z},{x+extent,y+extent,z+extent}};
        const auto handle = world.AddCollider({box, (index % 3U) == 0U ? 2U : 1U,
                                                (index % 7U) == 0U});
        if (!handle.IsValid())
            return false;
        bounds.push_back(box);
        handles.push_back(handle);
    }

    std::vector<vanta::ColliderPair> actual;
    if (!world.ComputeOverlaps(actual))
        return false;
    std::vector<vanta::ColliderPair> expected;
    for (std::size_t first = 0; first < bounds.size(); ++first)
    {
        for (std::size_t second = first + 1U; second < bounds.size(); ++second)
        {
            const Aabb& a = bounds[first];
            const Aabb& b = bounds[second];
            const bool intersects =
                a.minimum.x <= b.maximum.x && a.maximum.x >= b.minimum.x &&
                a.minimum.y <= b.maximum.y && a.maximum.y >= b.minimum.y &&
                a.minimum.z <= b.maximum.z && a.maximum.z >= b.minimum.z;
            if (intersects)
                expected.push_back({handles[first], handles[second],
                    (first % 7U) == 0U || (second % 7U) == 0U});
        }
    }
    const auto byHandle = [](const vanta::ColliderPair& left,
                             const vanta::ColliderPair& right) {
        if (left.first.index != right.first.index)
            return left.first.index < right.first.index;
        return left.second.index < right.second.index;
    };
    std::sort(actual.begin(), actual.end(), byHandle);
    std::sort(expected.begin(), expected.end(), byHandle);
    return actual == expected;
}

constexpr std::array kCases{
    TestCase{"collider lifecycle invalidates stale handles",addUpdateRemoveAndStaleHandles},
    TestCase{"broadphase emits deterministic solid and trigger pairs",broadphaseBuildsDeterministicSolidAndTriggerPairs},
    TestCase{"broadphase matches brute force across dense and sparse layouts",broadphaseMatchesBruteForceAcrossDenseAndSparseLayouts},
    TestCase{"broadphase pairs use current collider generations",broadphasePairsTrackGenerationAfterColliderReuse},
    TestCase{"invalid collider descriptions are rejected",rejectsInvalidColliderDescriptions},
    TestCase{"raycast returns nearest hit and surface normal",raycastFindsNearestAndCorrectNormal},
    TestCase{"raycast handles parallel axes and inside origins",raycastHandlesParallelAxesAndInsideOrigins},
    TestCase{"raycast respects distance and layers",raycastHonorsDistanceAndLayerMask},
    TestCase{"raycast uses double precision for large finite bounds",raycastUsesDoublePrecisionForLargeFiniteBounds},
    TestCase{"trigger filtering applies to queries",triggerFilteringIsConsistent},
    TestCase{"sphere overlap uses closest point",sphereOverlapUsesClosestPoint},
    TestCase{"invalid overlap queries clear output",invalidQueriesClearResults},
    TestCase{"vertical capsule overlap uses segment distance",verticalCapsuleOverlapUsesSegmentDistance},
    TestCase{"vertical capsule rejects invalid dimensions",capsuleRejectsInvalidDimensions},
    TestCase{"failed raycast clears stale hit data",failedRaycastClearsPreviousHit},
    TestCase{"clear invalidates colliders and supports reuse",clearInvalidatesAllCollidersAndAllowsReuse},
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
