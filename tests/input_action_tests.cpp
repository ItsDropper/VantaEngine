#include "vanta/platform/InputActionMap.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace
{
using namespace vanta;
struct TestCase { std::string_view name; bool (*run)(); };

bool combinesKeyboardAndMouseBindings()
{
    InputState input;
    InputActionMap actions;
    if (!actions.BindKey("fire", 'F') || !actions.BindMouseButton("fire", 0))
        return false;
    actions.Evaluate(input);
    input.SetKey('F', true);
    actions.Evaluate(input);
    if (!actions.State("fire").down || !actions.State("fire").pressed)
        return false;
    input.BeginFrame();
    input.SetMouseButton(0, true);
    input.SetKey('F', false);
    actions.Evaluate(input);
    const auto state = actions.State("fire");
    return state.down && !state.pressed && !state.released;
}

bool emitsAggregateReleaseOnlyAfterLastBinding()
{
    InputState input;
    InputActionMap actions;
    (void)actions.BindKey("jump", ' ');
    (void)actions.BindKey("jump", 'J');
    input.SetKey(' ', true);
    actions.Evaluate(input);
    input.BeginFrame();
    input.SetKey(' ', false);
    input.SetKey('J', true);
    actions.Evaluate(input);
    if (!actions.State("jump").down || actions.State("jump").released)
        return false;
    input.BeginFrame();
    input.SetKey('J', false);
    actions.Evaluate(input);
    const auto state = actions.State("jump");
    return !state.down && state.released && !state.pressed;
}

bool bindingChangesReleaseHeldActionOnNextEvaluation()
{
    InputState input;
    InputActionMap actions;
    (void)actions.BindKey("reload", 'R');
    input.SetKey('R', true);
    actions.Evaluate(input);
    if (!actions.Unbind("reload", {InputBindingType::Key, 'R'}))
        return false;
    actions.Evaluate(input);
    return actions.State("reload").released && !actions.State("reload").down;
}

bool rejectsInvalidBindingsAndDeduplicates()
{
    InputActionMap actions;
    return !actions.BindKey("", 'A') && !actions.BindKey("move", -1) &&
           !actions.BindKey("move", 256) && !actions.BindMouseButton("fire", 5) &&
           actions.BindKey("move", 'W') && actions.BindKey("move", 'W') &&
           actions.BindingCount("move") == 1;
}

bool clearAndUnknownActionsAreSafe()
{
    InputActionMap actions;
    InputState input;
    if (actions.State("missing").down || actions.BindingCount("missing") != 0)
        return false;
    (void)actions.BindKey("crouch", 'C');
    actions.Clear();
    actions.Evaluate(input);
    return actions.BindingCount("crouch") == 0 && !actions.State("crouch").down;
}

bool exposesBindingsAndStableActionNamesForSettingsUi()
{
    InputActionMap actions;
    (void)actions.BindKey("jump", ' ');
    (void)actions.BindKey("jump", 'J');
    (void)actions.BindMouseButton("aim", 1);
    const auto jump = actions.Bindings("jump");
    const auto names = actions.ActionNames();
    return jump.size() == 2 &&
           jump[0] == InputBinding{InputBindingType::Key, ' '} &&
           jump[1] == InputBinding{InputBindingType::Key, 'J'} &&
           names == std::vector<std::string>{"aim", "jump"} &&
           actions.Bindings("missing").empty();
}

constexpr std::array kCases{
    TestCase{"combines keyboard and mouse bindings", combinesKeyboardAndMouseBindings},
    TestCase{"aggregate release waits for final binding", emitsAggregateReleaseOnlyAfterLastBinding},
    TestCase{"binding removal releases held action", bindingChangesReleaseHeldActionOnNextEvaluation},
    TestCase{"invalid bindings are rejected and duplicates deduplicated", rejectsInvalidBindingsAndDeduplicates},
    TestCase{"clear and unknown action handling", clearAndUnknownActionsAreSafe},
    TestCase{"bindings and stable action names are queryable", exposesBindingsAndStableActionNamesForSettingsUi}
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
              << '/' << kCases.size() << " input action tests passed.\n";
    return failures == 0 ? 0 : 1;
}
