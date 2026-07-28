#pragma once

#include "../pch.hpp"

#include "macros.hpp"

#define DECLARE_IMGUI_SCOPE(Type, ...) [[maybe_unused]] imgui::Type##Scoped CONCAT(_##Type##_, __COUNTER__) { __VA_ARGS__ }

namespace imgui {
    struct GroupScoped {
        explicit GroupScoped() {
            ImGui::BeginGroup();
        }

        ~GroupScoped() {
            ImGui::EndGroup();
        }
    };

    struct DisabledScoped {
        explicit DisabledScoped(bool disable = true) {
            ImGui::BeginDisabled(disable);
        }

        ~DisabledScoped() {
            ImGui::EndDisabled();
        }
    };

    struct IDScoped {
        explicit IDScoped(std::string_view id) {
            ImGui::PushID(id.data(), id.data() + id.size());
        }

        explicit IDScoped(const void *id) {
            ImGui::PushID(id);
        }

        explicit IDScoped(int id) {
            ImGui::PushID(id);
        }

        ~IDScoped() {
            ImGui::PopID();
        }
    };

    struct ItemWidthScoped {
        explicit ItemWidthScoped(float width) {
            ImGui::PushItemWidth(width);
        }

        ~ItemWidthScoped() {
            ImGui::PopItemWidth();
        }
    };

    struct ItemFlagScoped {
        explicit ItemFlagScoped(ImGuiItemFlags option, bool enabled) {
            ImGui::PushItemFlag(option, enabled);
        }

        ~ItemFlagScoped() {
            ImGui::PopItemFlag();
        }
    };

    struct StyleColorScoped {
        StyleColorScoped(int index, const ImVec4 &color) {
            ImGui::PushStyleColor(index, color);
        }

        StyleColorScoped(int index, ImU32 color) {
            ImGui::PushStyleColor(index, color);
        }

        ~StyleColorScoped() {
            ImGui::PopStyleColor();
        }
    };

    struct StyleVarScoped {
        StyleVarScoped(int index, float value) {
            ImGui::PushStyleVar(index, value);
        }

        StyleVarScoped(int index, const ImVec2 &value) {
            ImGui::PushStyleVar(index, value);
        }

        ~StyleVarScoped() {
            ImGui::PopStyleVar();
        }
    };

namespace widget {
    template <typename Allocator>
    bool InputTextWithHint(const char *label, const char *hint, std::basic_string<char, std::char_traits<char>, Allocator>* str, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr) {
        struct ChainedUserData {
            std::basic_string<char, std::char_traits<char>, Allocator> *Str;
            ImGuiInputTextCallback ChainCallback;
            void *ChainCallbackUserData;
        };

        constexpr auto chainCallback = [](ImGuiInputTextCallbackData *data) -> int {
            const auto *userData = static_cast<ChainedUserData*>(data->UserData);
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                // Resize string callback
                // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
                auto* str = userData->Str;
                assert(data->Buf == str->c_str());
                str->resize(data->BufTextLen);
                data->Buf = const_cast<char*>(str->c_str());
            }
            else if (userData->ChainCallback) {
                // Forward to user callback, if any
                data->UserData = userData->ChainCallbackUserData;
                return userData->ChainCallback(data);
            }
            return 0;
        };

        assert((flags & ImGuiInputTextFlags_CallbackResize) == 0);
        flags |= ImGuiInputTextFlags_CallbackResize;

        ChainedUserData chainedUserData {
            .Str = str,
            .ChainCallback = callback,
            .ChainCallbackUserData = userData,
        };
        return ImGui::InputTextWithHint(label, hint, str->data(), str->capacity() + 1, flags, chainCallback, &chainedUserData);
    }

    inline void TextUnformatted(std::string_view str) {
       ImGui::TextUnformatted(str.data(), str.data() + str.size());
    }

    inline void HelperMarker(const char *label, std::string_view description) {
        ImGui::TextDisabled("%s", label);
        if (ImGui::BeginItemTooltip()) {
            TextUnformatted(description);
            ImGui::EndTooltip();
        }
    }

    // https://github.com/ocornut/imgui/issues/1901#issue-335266223
    inline void Spinner(std::string_view label, float radius, float thickness, ImU32 color) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label.data(), label.data() + label.size());

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size((radius )*2, (radius + style.FramePadding.y)*2);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return;

        // Render
        window->DrawList->PathClear();

        int num_segments = 30;
        int start = abs(ImSin(g.Time*1.8f)*(num_segments-5));

        const float a_min = IM_PI*2.0f * ((float)start) / (float)num_segments;
        const float a_max = IM_PI*2.0f * ((float)num_segments-3) / (float)num_segments;

        const ImVec2 centre = ImVec2(pos.x+radius, pos.y+radius+style.FramePadding.y);

        for (int i = 0; i < num_segments; i++) {
            const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
            window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a+g.Time*8) * radius,
                                                centre.y + ImSin(a+g.Time*8) * radius));
        }

        window->DrawList->PathStroke(color, thickness);
    }

    template <typename F>
    struct TableColumnInfo {
        const char *label;
        F f;
        ImGuiTableColumnFlags flags;
    };

namespace details {
    template <bool RowNumber, typename... Fs>
    void TableBody(std::size_t rowStart, std::ranges::input_range auto &&items, const Fs &...fs) {
        for (auto &&item : FWD(items)) {
            ImGui::TableNextRow();

            if constexpr (RowNumber) {
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", rowStart);
            }

            INDEX_SEQ(Is, sizeof...(Fs), {
                ((ImGui::TableSetColumnIndex(Is + RowNumber), [&]() {
                    if constexpr (std::invocable<Fs, std::size_t /* row */, decltype(item)>) {
                        fs(rowStart, FWD(item));
                    }
                    else {
                        fs(FWD(item));
                    }
                }()), ...);
            });
            ++rowStart;
        }
    }
}

    template <bool RowNumber = true, typename... Fs>
    void Table(const char *str_id, ImGuiTableFlags flags, std::ranges::input_range auto &&items, const TableColumnInfo<Fs> &...columnInfos) {
        if (ImGui::BeginTable(str_id, RowNumber + sizeof...(Fs), flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (ImGui::TableSetupColumn(columnInfos.label, columnInfos.flags), ...);
            ImGui::TableHeadersRow();

            details::TableBody<RowNumber>(0, FWD(items), columnInfos.f...);

            ImGui::EndTable();
        }
    }

    template <bool RowNumber = true, typename... Fs>
    void TableWithVirtualization(const char *str_id, ImGuiTableFlags flags, std::ranges::random_access_range auto &&items, const TableColumnInfo<Fs> &...columnInfos) {
        // If item count is less than 32, use the normal Table function.
        if (items.size() < 32) {
            Table<RowNumber>(str_id, flags, FWD(items), columnInfos...);
        }
        else if (ImGui::BeginTable(str_id, RowNumber + sizeof...(Fs), flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            if constexpr (RowNumber) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
            }
            (ImGui::TableSetupColumn(columnInfos.label, columnInfos.flags), ...);
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(items.size()));
            while (clipper.Step()) {
                auto clippedItems = FWD(items) | std::views::drop(clipper.DisplayStart) | std::views::take(clipper.DisplayEnd - clipper.DisplayStart);
                details::TableBody<RowNumber>(clipper.DisplayStart, clippedItems, columnInfos.f...);
            }
            ImGui::EndTable();
        }
    }
}

}
