#include "menu/ui_common.hpp"

namespace rush::ui {
namespace {
ModResult first_error = MOD_OK;
}

void remember(ModResult result) {
    if (result != MOD_OK) {
        first_error = result;
    }
}

ModResult deferred_error() {
    return first_error;
}

std::string asset(const char* kind, const char* file) {
    return std::string("mod://local.dungeon_rush/res/") + kind + "/" + file + "?rev=0.2.0";
}

std::string rml_text(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else {
            out += c;
        }
    }
    return out;
}

ModResult button(ModContext* ctx, UiElementHandle pane, const char* label, UiPressedFn pressed,
                 void* data, const char* css, UiPredicateFn disabled) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.label = label;
    c.on_pressed = pressed;
    c.user_data = data;
    c.is_disabled = disabled;
    UiElementHandle element = 0;
    CHECK(svc_ui->pane_add_control(ctx, pane, &c, &element));
    return svc_ui->elem_set_class(ctx, element, css, true);
}

}  // namespace rush::ui
