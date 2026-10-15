#include "vanta/renderer/AdaptiveWorldBudget.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
using vanta::renderer::AdaptiveWorldBudget;

struct TestCase
{
    std::string_view name;
    bool (*run)();
};

bool StartsAtFullDetail()
{
    const AdaptiveWorldBudget budget;
    return std::abs(budget.DetailScale() - 1.0F) < 1.0e-6F;
}

bool SustainedSlowFramesReduceDetail()
{
    AdaptiveWorldBudget budget;
    for (int i = 0; i < 240; ++i)
        (void)budget.UpdateForFrameSeconds(1.0F / 30.0F);
    return budget.DetailScale() < 0.9F && budget.DetailScale() >= 0.68F;
}

bool SustainedFastFramesRecoverDetail()
{
    AdaptiveWorldBudget budget;
    for (int i = 0; i < 240; ++i)
        (void)budget.UpdateForFrameSeconds(1.0F / 30.0F);
    const float reduced = budget.DetailScale();
    for (int i = 0; i < 600; ++i)
        (void)budget.UpdateForFrameSeconds(1.0F / 240.0F);
    return reduced < 1.0F && budget.DetailScale() > reduced &&
           budget.DetailScale() <= 1.0F;
}

bool IgnoresInvalidAndLongPauseSamples()
{
    AdaptiveWorldBudget budget;
    for (int i = 0; i < 80; ++i)
        (void)budget.UpdateForFrameSeconds(1.0F / 30.0F);
    const float before = budget.DetailScale();
    constexpr std::array invalid{
        0.0F, -0.01F, 0.251F, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()
    };
    for (const float sample : invalid)
        if (budget.UpdateForFrameSeconds(sample) != before)
            return false;
    return true;
}

bool ScaleRemainsBounded()
{
    AdaptiveWorldBudget slow;
    for (int i = 0; i < 1000; ++i)
        (void)slow.UpdateForFrameSeconds(0.1F);
    if (slow.DetailScale() < 0.68F || slow.DetailScale() > 1.0F)
        return false;

    AdaptiveWorldBudget fast;
    for (int i = 0; i < 2000; ++i)
        (void)fast.UpdateForFrameSeconds(0.001F);
    return fast.DetailScale() >= 0.68F && fast.DetailScale() <= 1.0F;
}

bool NormalCadenceDoesNotTriggerQualityDrop()
{
    AdaptiveWorldBudget budget;
    for (int i = 0; i < 120; ++i)
        (void)budget.UpdateForFrameSeconds(1.0F / 60.0F);
    return budget.DetailScale() == 1.0F;
}

constexpr std::array kTests{
    TestCase{"starts at full detail", StartsAtFullDetail},
    TestCase{"sustained slow frames reduce detail", SustainedSlowFramesReduceDetail},
    TestCase{"sustained fast frames recover detail", SustainedFastFramesRecoverDetail},
    TestCase{"invalid and pause samples are ignored", IgnoresInvalidAndLongPauseSamples},
    TestCase{"detail scale remains bounded", ScaleRemainsBounded},
    TestCase{"normal 60 Hz cadence does not reduce detail", NormalCadenceDoesNotTriggerQualityDrop}
};
} // namespace

int main()
{
    int failures = 0;
    for (const TestCase& test : kTests)
    {
        if (!test.run())
        {
            std::cerr << "FAIL: " << test.name << '\n';
            ++failures;
        }
        else
        {
            std::cout << "PASS: " << test.name << '\n';
        }
    }

    std::cout << (kTests.size() - static_cast<std::size_t>(failures))
              << '/' << kTests.size() << " adaptive-world-budget tests passed.\n";
    return failures == 0 ? 0 : 1;
}
