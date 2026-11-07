#include "vanta/core/FixedStepClock.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
struct TestCase { std::string_view name; bool (*run)(); };

bool exactStepRunsOnce()
{
    vanta::FixedStepClock clock;
    int updates = 0;
    const auto result = clock.Advance(1.0 / 60.0, [&](double step) {
        if (std::abs(step - 1.0 / 60.0) < 1.0e-12) ++updates;
    });
    return updates == 1 && result.steps == 1 && result.interpolationAlpha < 1.0e-8;
}

bool subStepFramesAccumulateDeterministically()
{
    vanta::FixedStepClock clock;
    int updates = 0;
    for (int frame = 0; frame < 10; ++frame)
        (void)clock.Advance(1.0 / 120.0, [&](double) { ++updates; });
    return updates == 5;
}

bool fractionalTimeIsRetained()
{
    vanta::FixedStepClock clock;
    int updates = 0;
    const auto first = clock.Advance(0.005, [&](double) { ++updates; });
    const auto second = clock.Advance(0.012, [&](double) { ++updates; });
    return first.steps == 0 && first.interpolationAlpha > 0.29 &&
           second.steps == 1 && updates == 1;
}

bool frameSpikesAreBoundedAndMeasured()
{
    vanta::FixedStepClock clock;
    int updates = 0;
    const auto result = clock.Advance(2.0, [&](double) { ++updates; });
    return updates == 8 && result.steps == 8 && result.droppedSeconds > 0.11 &&
           result.interpolationAlpha < 1.0 &&
           std::abs(clock.TotalDroppedSeconds() - result.droppedSeconds) < 1.0e-9;
}

bool invalidFrameSamplesAreIgnored()
{
    vanta::FixedStepClock clock;
    int updates = 0;
    (void)clock.Advance(std::numeric_limits<double>::quiet_NaN(), [&](double) { ++updates; });
    (void)clock.Advance(-1.0, [&](double) { ++updates; });
    (void)clock.Advance(std::numeric_limits<double>::infinity(), [&](double) { ++updates; });
    const auto valid = clock.Advance(1.0 / 60.0, [&](double) { ++updates; });
    return updates == 1 && valid.steps == 1;
}

bool maximumStepCountIsConfigurable()
{
    vanta::FixedStepClock clock(0.01, 3, 0.2);
    int updates = 0;
    const auto result = clock.Advance(0.1, [&](double) { ++updates; });
    return updates == 3 && result.droppedSeconds > 0.06 &&
           result.interpolationAlpha < 1.0e-8;
}

bool resetClearsAccumulatorAndDiagnostics()
{
    vanta::FixedStepClock clock;
    (void)clock.Advance(0.2, [](double) {});
    clock.Reset();
    return clock.TotalDroppedSeconds() == 0.0 && clock.InterpolationAlpha() == 0.0;
}

bool invalidConfigurationUsesSafeDefaults()
{
    vanta::FixedStepClock clock(0.0, 0, -1.0);
    return std::abs(clock.StepSeconds() - 1.0 / 60.0) < 1.0e-12;
}

constexpr std::array kCases{
    TestCase{"one exact fixed step", exactStepRunsOnce},
    TestCase{"sub-step frames accumulate deterministically", subStepFramesAccumulateDeterministically},
    TestCase{"fractional time is retained", fractionalTimeIsRetained},
    TestCase{"frame spikes are bounded and measured", frameSpikesAreBoundedAndMeasured},
    TestCase{"invalid frame samples are ignored", invalidFrameSamplesAreIgnored},
    TestCase{"maximum step count is configurable", maximumStepCountIsConfigurable},
    TestCase{"reset clears timing state", resetClearsAccumulatorAndDiagnostics},
    TestCase{"invalid configuration uses safe defaults", invalidConfigurationUsesSafeDefaults},
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
              << '/' << kCases.size() << " fixed-step tests passed.\n";
    return failures == 0 ? 0 : 1;
}
