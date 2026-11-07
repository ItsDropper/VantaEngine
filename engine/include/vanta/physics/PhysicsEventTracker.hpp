#pragma once

#include "vanta/physics/PhysicsWorld.hpp"

#include <vector>

namespace vanta
{
enum class PhysicsEventPhase
{
    Enter,
    Stay,
    Exit
};

struct PhysicsEvent
{
    ColliderPair pair{};
    PhysicsEventPhase phase = PhysicsEventPhase::Enter;
};

class PhysicsEventTracker final
{
public:
    // Consumes the current broadphase result and returns deterministic lifecycle events.
    // Removed colliders produce Exit events carrying their previous generation-safe handles.
    [[nodiscard]] std::vector<PhysicsEvent> Update(
        const std::vector<ColliderPair>& currentPairs);
    void Reset() noexcept;
    [[nodiscard]] std::size_t PreviousPairCount() const noexcept { return m_previousPairs.size(); }

private:
    std::vector<ColliderPair> m_previousPairs;
};
} // namespace vanta
