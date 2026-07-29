#pragma once

#include "../pch.hpp"

#include "macros.hpp"

namespace utils {
    [[nodiscard]] constexpr auto decomposer(auto &&f LIFETIMEBOUND) noexcept {
        return [&](auto &&tupleLike) noexcept(noexcept(std::apply(FWD(f), FWD(tupleLike)))) {
            return std::apply(FWD(f), FWD(tupleLike));
        };
    }
}