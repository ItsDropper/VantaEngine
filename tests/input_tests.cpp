#include "vanta/platform/InputState.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
struct TestCase { std::string_view name; bool (*run)(); };

bool keyTransitionsAreEdgeTriggered()
{
    vanta::InputState input;
    input.SetKey('W', true);
    input.SetKey('W', true);
    const bool pressedOnce = input.IsKeyDown('W') && input.WasKeyPressed('W') &&
                             !input.WasKeyReleased('W');
    input.BeginFrame();
    const bool heldWithoutRepeatPress = input.IsKeyDown('W') && !input.WasKeyPressed('W');
    input.SetKey('W', false);
    return pressedOnce && heldWithoutRepeatPress && !input.IsKeyDown('W') &&
           input.WasKeyReleased('W');
}

bool mouseButtonsHaveIndependentTransitions()
{
    vanta::InputState input;
    input.SetMouseButton(1, true);
    input.SetMouseButton(2, true);
    input.SetMouseButton(1, false);
    return !input.IsMouseButtonDown(1) && input.WasMouseButtonPressed(1) &&
           input.WasMouseButtonReleased(1) && input.IsMouseButtonDown(2);
}

bool mouseMotionAccumulatesWithinFrame()
{
    vanta::InputState input;
    input.AddMouseDelta(12, -4);
    input.AddMouseDelta(-3, 9);
    return input.MouseDeltaX() == 9 && input.MouseDeltaY() == 5;
}

bool frameBoundaryClearsTransientStateOnly()
{
    vanta::InputState input;
    input.SetKey('A', true);
    input.SetMouseButton(0, true);
    input.AddMouseDelta(30, 10);
    input.BeginFrame();
    return input.IsKeyDown('A') && input.IsMouseButtonDown(0) &&
           !input.WasKeyPressed('A') && !input.WasMouseButtonPressed(0) &&
           input.MouseDeltaX() == 0 && input.MouseDeltaY() == 0;
}

bool focusLossReleasesAllHeldInputs()
{
    vanta::InputState input;
    input.SetKey('W', true);
    input.SetKey(0x10, true);
    input.SetMouseButton(1, true);
    input.AddMouseDelta(100, -20);
    input.ReleaseAll();
    return !input.IsKeyDown('W') && !input.IsKeyDown(0x10) &&
           input.WasKeyReleased('W') && input.WasKeyReleased(0x10) &&
           !input.IsMouseButtonDown(1) && input.WasMouseButtonReleased(1) &&
           input.MouseDeltaX() == 0 && input.MouseDeltaY() == 0;
}

bool invalidIndicesAreSafe()
{
    vanta::InputState input;
    input.SetKey(-1, true);
    input.SetKey(256, true);
    input.SetMouseButton(-1, true);
    input.SetMouseButton(5, true);
    return !input.IsKeyDown(-1) && !input.IsKeyDown(256) &&
           !input.IsMouseButtonDown(-1) && !input.IsMouseButtonDown(5);
}

bool mouseDeltaSaturatesWithoutOverflow()
{
    vanta::InputState input;
    input.AddMouseDelta(1'000'000, 1'000'000);
    input.AddMouseDelta(1, -2'000'000);
    return input.MouseDeltaX() == 1'000'000 && input.MouseDeltaY() == -1'000'000;
}

constexpr std::array kCases{
    TestCase{"keyboard transitions are edge-triggered", keyTransitionsAreEdgeTriggered},
    TestCase{"mouse buttons track independent transitions", mouseButtonsHaveIndependentTransitions},
    TestCase{"mouse motion accumulates per frame", mouseMotionAccumulatesWithinFrame},
    TestCase{"frame boundary preserves held state", frameBoundaryClearsTransientStateOnly},
    TestCase{"focus loss releases all held inputs", focusLossReleasesAllHeldInputs},
    TestCase{"invalid input indices are ignored", invalidIndicesAreSafe},
    TestCase{"mouse delta accumulation saturates", mouseDeltaSaturatesWithoutOverflow},
};
}

int main()
{
    int failures = 0;
    for (const TestCase& test : kCases)
    {
        if (!test.run()) { std::cerr << "FAIL: " << test.name << '\n'; ++failures; }
        else { std::cout << "PASS: " << test.name << '\n'; }
    }
    std::cout << (kCases.size() - static_cast<std::size_t>(failures))
              << '/' << kCases.size() << " input tests passed.\n";
    return failures == 0 ? 0 : 1;
}
