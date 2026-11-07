#include "vanta/platform/InputActionMap.hpp"

#include <algorithm>
#include <utility>

namespace vanta
{
bool InputActionMap::Bind(std::string action, const InputBinding binding)
{
    if (action.empty())
        return false;
    if ((binding.type == InputBindingType::Key &&
         (binding.code < 0 || binding.code >= static_cast<int>(InputState::kKeyCount))) ||
        (binding.type == InputBindingType::MouseButton &&
         (binding.code < 0 || binding.code >= static_cast<int>(InputState::kMouseButtonCount))))
        return false;

    m_states.try_emplace(action);
    auto& bindings = m_bindings[std::move(action)];
    if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end())
        bindings.push_back(binding);
    return true;
}

bool InputActionMap::BindKey(std::string action, const int virtualKey)
{
    return Bind(std::move(action), {InputBindingType::Key, virtualKey});
}

bool InputActionMap::BindMouseButton(std::string action, const int button)
{
    return Bind(std::move(action), {InputBindingType::MouseButton, button});
}

bool InputActionMap::Unbind(const std::string_view action, const InputBinding binding) noexcept
{
    const auto iterator = m_bindings.find(action);
    if (iterator == m_bindings.end())
        return false;
    auto& bindings = iterator->second;
    const auto found = std::find(bindings.begin(), bindings.end(), binding);
    if (found == bindings.end())
        return false;
    bindings.erase(found);
    return true;
}

bool InputActionMap::ClearBindings(const std::string_view action) noexcept
{
    const auto iterator = m_bindings.find(action);
    if (iterator == m_bindings.end())
        return false;
    iterator->second.clear();
    return true;
}

void InputActionMap::Clear() noexcept
{
    m_bindings.clear();
    m_states.clear();
}

void InputActionMap::Evaluate(const InputState& input) noexcept
{
    for (auto& [action, state] : m_states)
    {
        const auto bindings = m_bindings.find(action);
        bool down = false;
        if (bindings != m_bindings.end())
        {
            for (const InputBinding& binding : bindings->second)
            {
                const bool bindingDown = binding.type == InputBindingType::Key
                    ? input.IsKeyDown(binding.code)
                    : input.IsMouseButtonDown(binding.code);
                if (bindingDown)
                {
                    down = true;
                    break;
                }
            }
        }
        state.pressed = down && !state.down;
        state.released = !down && state.down;
        state.down = down;
    }

}

InputActionState InputActionMap::State(const std::string_view action) const noexcept
{
    const auto iterator = m_states.find(action);
    return iterator == m_states.end() ? InputActionState{} : iterator->second;
}

std::vector<InputBinding> InputActionMap::Bindings(const std::string_view action) const
{
    const auto iterator = m_bindings.find(action);
    return iterator == m_bindings.end() ? std::vector<InputBinding>{} : iterator->second;
}

std::vector<std::string> InputActionMap::ActionNames() const
{
    std::vector<std::string> names;
    names.reserve(m_bindings.size());
    for (const auto& [action, bindings] : m_bindings)
    {
        (void)bindings;
        names.push_back(action);
    }
    return names;
}

std::size_t InputActionMap::BindingCount(const std::string_view action) const noexcept
{
    const auto iterator = m_bindings.find(action);
    return iterator == m_bindings.end() ? 0U : iterator->second.size();
}
} // namespace vanta
