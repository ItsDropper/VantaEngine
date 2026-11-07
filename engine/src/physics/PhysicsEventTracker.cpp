#include "vanta/physics/PhysicsEventTracker.hpp"

#include <algorithm>
#include <tuple>

namespace vanta
{
namespace
{
[[nodiscard]] bool HandleLess(const ColliderHandle& left, const ColliderHandle& right) noexcept
{
    return std::tie(left.index, left.generation) <
           std::tie(right.index, right.generation);
}

[[nodiscard]] bool PairLess(const ColliderPair& left, const ColliderPair& right) noexcept
{
    if (left.first != right.first)
        return HandleLess(left.first, right.first);
    return HandleLess(left.second, right.second);
}

[[nodiscard]] bool SamePair(const ColliderPair& left, const ColliderPair& right) noexcept
{
    return left.first == right.first && left.second == right.second;
}

[[nodiscard]] std::vector<ColliderPair> NormalizePairs(
    const std::vector<ColliderPair>& input)
{
    std::vector<ColliderPair> pairs;
    pairs.reserve(input.size());
    for (ColliderPair pair : input)
    {
        if (!pair.first.IsValid() || !pair.second.IsValid() || pair.first == pair.second)
            continue;
        if (HandleLess(pair.second, pair.first))
            std::swap(pair.first, pair.second);
        pairs.push_back(pair);
    }

    std::sort(pairs.begin(), pairs.end(), PairLess);
    std::vector<ColliderPair> unique;
    unique.reserve(pairs.size());
    for (const ColliderPair& pair : pairs)
    {
        if (!unique.empty() && SamePair(unique.back(), pair))
            unique.back().involvesTrigger = unique.back().involvesTrigger || pair.involvesTrigger;
        else
            unique.push_back(pair);
    }
    return unique;
}
}

std::vector<PhysicsEvent> PhysicsEventTracker::Update(
    const std::vector<ColliderPair>& currentPairs)
{
    std::vector<ColliderPair> current = NormalizePairs(currentPairs);
    std::vector<PhysicsEvent> events;
    events.reserve(m_previousPairs.size() + current.size());

    std::size_t previousIndex = 0;
    std::size_t currentIndex = 0;
    while (previousIndex < m_previousPairs.size() || currentIndex < current.size())
    {
        if (previousIndex == m_previousPairs.size())
        {
            events.push_back({current[currentIndex++], PhysicsEventPhase::Enter});
            continue;
        }
        if (currentIndex == current.size())
        {
            events.push_back({m_previousPairs[previousIndex++], PhysicsEventPhase::Exit});
            continue;
        }

        const ColliderPair& previous = m_previousPairs[previousIndex];
        const ColliderPair& next = current[currentIndex];
        if (PairLess(previous, next))
        {
            events.push_back({previous, PhysicsEventPhase::Exit});
            ++previousIndex;
        }
        else if (PairLess(next, previous))
        {
            events.push_back({next, PhysicsEventPhase::Enter});
            ++currentIndex;
        }
        else
        {
            if (previous.involvesTrigger == next.involvesTrigger)
                events.push_back({next, PhysicsEventPhase::Stay});
            else
            {
                events.push_back({previous, PhysicsEventPhase::Exit});
                events.push_back({next, PhysicsEventPhase::Enter});
            }
            ++previousIndex;
            ++currentIndex;
        }
    }

    m_previousPairs.swap(current);
    return events;
}

void PhysicsEventTracker::Reset() noexcept
{
    m_previousPairs.clear();
}
} // namespace vanta
