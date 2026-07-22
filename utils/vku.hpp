#pragma once

#include "../pch.hpp"

#include "macros.hpp"

namespace vku {
    template <typename T>
    [[nodiscard]] const T &lvalue(const T &&rvalue LIFETIMEBOUND) noexcept {
        return rvalue;
    }

    template <typename T>
    [[nodiscard]] const std::initializer_list<T> &lvalue(const std::initializer_list<T> &&rvalue LIFETIMEBOUND) noexcept {
        return rvalue;
    }

    template <typename T> requires (vk::FlagTraits<T>::isBitmask)
    [[nodiscard]] constexpr bool contains(vk::Flags<T> super, T sub) noexcept {
        return static_cast<bool>(super & sub);
    }
}