#include "vanta/physics/PhysicsEventTracker.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using namespace vanta;
struct TestCase { std::string_view name; bool (*run)(); };

constexpr ColliderHandle Handle(std::uint32_t index, std::uint32_t generation = 1)
{
    return {index, generation};
}

bool emitsEnterStayAndExitTransitions()
{
    PhysicsEventTracker tracker;
    const std::vector<ColliderPair> overlap{{Handle(1), Handle(2), false}};
    const auto entered = tracker.Update(overlap);
    if (entered.size() != 1 || entered[0].phase != PhysicsEventPhase::Enter ||
        entered[0].pair != overlap[0])
        return false;
    const auto stayed = tracker.Update(overlap);
    if (stayed.size() != 1 || stayed[0].phase != PhysicsEventPhase::Stay)
        return false;
    const auto exited = tracker.Update({});
    return exited.size() == 1 && exited[0].phase == PhysicsEventPhase::Exit &&
           exited[0].pair == overlap[0] && tracker.PreviousPairCount() == 0;
}

bool detectsTriggerClassificationChanges()
{
    PhysicsEventTracker tracker;
    const std::vector<ColliderPair> solid{{Handle(3), Handle(7), false}};
    (void)tracker.Update(solid);
    const std::vector<ColliderPair> trigger{{Handle(3), Handle(7), true}};
    const auto events = tracker.Update(trigger);
    return events.size() == 2 &&
           events[0].phase == PhysicsEventPhase::Exit && !events[0].pair.involvesTrigger &&
           events[1].phase == PhysicsEventPhase::Enter && events[1].pair.involvesTrigger;
}

bool generationChangesAreExitThenEnter()
{
    PhysicsEventTracker tracker;
    (void)tracker.Update({{Handle(4), Handle(8), false}});
    const auto events = tracker.Update({{Handle(4), Handle(8, 2), false}});
    return events.size() == 2 &&
           events[0].phase == PhysicsEventPhase::Exit &&
           events[0].pair.second == Handle(8) &&
           events[1].phase == PhysicsEventPhase::Enter &&
           events[1].pair.second == Handle(8, 2);
}

bool normalizesOrderingAndDeduplicatesPairs()
{
    PhysicsEventTracker tracker;
    const std::vector<ColliderPair> input{
        {Handle(9), Handle(2), false},
        {Handle(2), Handle(9), true},
        {{}, Handle(5), false},
        {Handle(3), Handle(3), false}
    };
    const auto events = tracker.Update(input);
    return events.size() == 1 && events[0].phase == PhysicsEventPhase::Enter &&
           events[0].pair.first == Handle(2) && events[0].pair.second == Handle(9) &&
           events[0].pair.involvesTrigger;
}

bool resetDiscardsHistoryWithoutEmittingEvents()
{
    PhysicsEventTracker tracker;
    (void)tracker.Update({{Handle(1), Handle(5), false}});
    tracker.Reset();
    return tracker.PreviousPairCount() == 0 &&
           tracker.Update({{Handle(1), Handle(5), false}}).front().phase ==
               PhysicsEventPhase::Enter;
}

constexpr std::array kCases{
    TestCase{"enter stay exit lifecycle", emitsEnterStayAndExitTransitions},
    TestCase{"trigger classification transition", detectsTriggerClassificationChanges},
    TestCase{"collider generation replacement", generationChangesAreExitThenEnter},
    TestCase{"pair ordering and duplicate normalization", normalizesOrderingAndDeduplicatesPairs},
    TestCase{"reset clears contact history", resetDiscardsHistoryWithoutEmittingEvents}
};
}

int main()
{
    int failures = 0;
    for (const TestCase& test : kCases)
    {
        if (!test.run())
        {
            std::cerr << "FAIL: " << test.name << '\n';
            ++failures;
        }
        else
            std::cout << "PASS: " << test.name << '\n';
    }
    std::cout << (kCases.size() - static_cast<std::size_t>(failures))
              << '/' << kCases.size() << " physics event tests passed.\n";
    return failures == 0 ? 0 : 1;
}
