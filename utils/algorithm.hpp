#pragma once

#include "../pch.hpp"

namespace ranges {
    template <typename T, typename R = std::initializer_list<T>>
    [[nodiscard]] constexpr bool any_of(const T &value, R &&candidates) {
        return std::ranges::contains(candidates, value);
    }
}