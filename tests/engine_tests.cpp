#include "vanta/core/FrameTime.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace
{
struct TestCase
{
    std::string_view name;
    double input;
    double expected;
};

constexpr std::array kTests{
    TestCase{"zero delta remains zero", 0.0, 0.0},
    TestCase{"normal delta remains unchanged", 1.0 / 60.0, 1.0 / 60.0},
    TestCase{"negative delta clamps to zero", -0.5, 0.0},
    TestCase{"large delta clamps to quarter second", 3.0, 0.25},
};
} // namespace

int main()
{
    int failures = 0;
    for (const TestCase& test : kTests)
    {
        const double actual = vanta::ClampDeltaSeconds(test.input);
        if (std::abs(actual - test.expected) > 1e-9)
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
