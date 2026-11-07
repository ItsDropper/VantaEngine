#pragma once

#include "vanta/platform/InputState.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vanta
{
enum class InputBindingType
{
    Key,
    MouseButton
};

struct InputBinding
{
    InputBindingType type = InputBindingType::Key;
    int code = 0;
    [[nodiscard]] constexpr bool operator==(const InputBinding&) const noexcept = default;
};

struct InputActionState
{
    bool down = false;
    bool pressed = false;
    bool released = false;
};

class InputActionMap final
{
public:
    [[nodiscard]] bool BindKey(std::string action, int virtualKey);
    [[nodiscard]] bool BindMouseButton(std::string action, int button);
    [[nodiscard]] bool Unbind(std::string_view action, InputBinding binding) noexcept;
    [[nodiscard]] bool ClearBindings(std::string_view action) noexcept;
    void Clear() noexcept;

    // Evaluate once per input frame. Press/release edges describe aggregate action state,
    // so alternate bindings do not generate a false release while another remains held.
    void Evaluate(const InputState& input) noexcept;
    [[nodiscard]] InputActionState State(std::string_view action) const noexcept;
    [[nodiscard]] std::size_t BindingCount(std::string_view action) const noexcept;
    [[nodiscard]] std::vector<InputBinding> Bindings(std::string_view action) const;
    [[nodiscard]] std::vector<std::string> ActionNames() const;

private:
    [[nodiscard]] bool Bind(std::string action, InputBinding binding);

    std::map<std::string, std::vector<InputBinding>, std::less<>> m_bindings;
    std::map<std::string, InputActionState, std::less<>> m_states;
};
} // namespace vanta
