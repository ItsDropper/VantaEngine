#pragma once

#include <iostream>
#include <string_view>

namespace vanta
{
inline void LogInfo(const std::string_view message)
{
    std::cout << "[Vanta] " << message << '\n';
}

inline void LogError(const std::string_view message)
{
    std::cerr << "[Vanta] ERROR: " << message << '\n';
}
} // namespace vanta
