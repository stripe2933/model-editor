#pragma once

#include "../pch.hpp"

#include <stringzilla/stringzilla.hpp>

#include "ranges.hpp"

namespace utils {
    /**
     * Convert range of characters to <tt>std::filesystem::path</tt>. Shorthand for <tt>std::filesystem::path { r.begin(), r.end() }</tt>.
     * @param r Range of characters to be converted. Its <tt>value_type</tt> must be either <tt>std::filesystem::path::value_type</tt> or <tt>char8_t</tt>.
     * @return std::filesystem::path constructed from \p r.
     */
    template <typename R> requires (std::ranges::common_range<R> && std::same_as<std::ranges::range_value_t<R>, char8_t>)
    [[nodiscard]] std::filesystem::path toPath(R &&r) {
        return { std::ranges::begin(r), std::ranges::end(r) };
    }

    [[nodiscard]] std::string toUTF8NFCString(const std::filesystem::path &path);

    template <typename OutputIt>
    void getCaseInsensitiveOccurrenceOffsets(
        ashvardanian::stringzilla::string_view haystack,
        const ashvardanian::stringzilla::utf8_uncased_needle<> &needle,
        OutputIt outIt
    ) noexcept {
        std::size_t offsetAccum = 0;
        do {
            const auto [offset, length] = haystack.utf8_uncased_search(needle);
            if (offset == ashvardanian::stringzilla::string_view::npos) {
                break;
            }

            *outIt++ = offsetAccum + offset;
            haystack.remove_prefix(offset + length);
            offsetAccum += offset + length;
        } while (!haystack.empty());
    }
}