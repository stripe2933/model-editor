#pragma once

#include "../pch.hpp"

#include "macros.hpp"

namespace ranges::views {
    template <std::integral T>
    [[nodiscard]] constexpr auto indices(T n) noexcept {
        return std::views::iota(T{}, n);
    }

    template <std::ranges::viewable_range R>
    [[nodiscard]] constexpr auto enumerate(R &&r) noexcept {
        using index_type = std::ranges::range_difference_t<R>;
        if constexpr (std::ranges::sized_range<R>) {
            const index_type size = std::ranges::size(r);
            return std::views::zip(indices(size), FWD(r));
        }
        else {
            return std::views::zip(std::views::iota(index_type{}), FWD(r));
        }
    }

    template <std::invocable G>
    [[nodiscard]] constexpr auto generate_n(std::size_t n, G &&gen LIFETIMEBOUND) noexcept {
        return std::views::transform(indices(n), [&gen](std::size_t) noexcept(std::is_nothrow_invocable_v<G>) -> decltype(auto) { return std::invoke(gen); });
    }

    template <typename T>
    [[nodiscard]] constexpr auto cast(std::ranges::input_range auto &&r) noexcept {
        return std::views::transform(FWD(r), [](auto &&x) noexcept(noexcept(static_cast<T>(FWD(x)))) -> decltype(auto) { return static_cast<T>(FWD(x)); });
    }
}