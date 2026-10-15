#include "vanta/core/FrameTime.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
struct TestCase
{
    std::string_view name;
    double input;
    double expected;
};

const std::array kTests{
    TestCase{"zero delta remains zero", 0.0, 0.0},
    TestCase{"normal delta remains unchanged", 1.0 / 60.0, 1.0 / 60.0},
    TestCase{"negative delta clamps to zero", -0.5, 0.0},
    TestCase{"large delta clamps to quarter second", 3.0, 0.25},
    TestCase{"NaN delta clamps to zero", std::numeric_limits<double>::quiet_NaN(), 0.0},
    TestCase{"positive infinity clamps to quarter second", std::numeric_limits<double>::infinity(), 0.25},
    TestCase{"negative infinity clamps to zero", -std::numeric_limits<double>::infinity(), 0.0},
};
} // namespace

int main()
{
    int failures = 0;
    for (const TestCase& test : kTests)
    {
        const double actual = vanta::ClampDeltaSeconds(test.input);
        if (!std::isfinite(actual) || std::abs(actual - test.expected) > 1e-9)
        {
            std::cerr << "FAIL: " << test.name << " (expected " << test.expected
                      << ", got " << actual << ")\n";
            ++failures;
        }
        else
        {
            std::cout << "PASS: " << test.name << '\n';
        }
    }

    std::cout << (kTests.size() - static_cast<std::size_t>(failures))
              << '/' << kTests.size() << " tests passed.\n";
    return failures == 0 ? 0 : 1;
}
