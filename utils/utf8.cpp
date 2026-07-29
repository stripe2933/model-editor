#include "utf8.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::string utils::toUTF8NFCString(const std::filesystem::path &path) {
    std::string result;

#if defined(_WIN32)
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded > 0) {
        result.resize(sizeNeeded - 1);
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), sizeNeeded, nullptr, nullptr);
    }
#elif defined(__APPLE__)
    result = path.string();

    // macOS uses Unicode Normalization Form D (NFD) for filesystem paths. When displaying paths containing Korean
    // characters or diacritics in ImGui, they must be converted to Unicode Normalization Form C (NFC).
    // https://github.com/ocornut/imgui/issues/6036

    // Check if the string is NFC.
    if (sz_cptr_t p = ashvardanian::stringzilla::string_view { result }.utf8_find_denormalized(sz_normal_form_nfc_k); p != SZ_NULL_CHAR) {
        // Find the smallest suffix that violates NFC.
        const std::size_t offset = p - result.data();
        ashvardanian::stringzilla::string suffixToFix { &result[offset], result.size() - offset };

        // Try normalize suffixToFix.
        // - When succeed, concat the prefix and the fixed suffix.
        // - When failed, do nothing (just return the non-NFC string as fallback).
        if (suffixToFix.try_utf8_normalize(sz_normal_form_nfc_k)) {
            result.resize(offset + suffixToFix.size());
            std::ranges::copy(suffixToFix, &result[offset]);
        }
    }
#else
    result = path.string();
#endif

    return result;
}