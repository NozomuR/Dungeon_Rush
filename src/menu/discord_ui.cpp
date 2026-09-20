#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <string>
#include <mods/service.hpp>
#include <mods/svc/ui.h>
#include "menu/discord_ui.hpp"
#include "online/online.hpp"
namespace rush::discord_ui {
namespace {
UiDialogHandle dialog = 0;
UiElementHandle profile_image = 0;
std::string code_input;
bool refresh_profile = false;
std::string last_avatar;
std::string last_body, opened_session;
std::chrono::steady_clock::time_point next_poll;
int dialog_stage = -1;
std::string escaped(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else if (c == '\"') {
            out += "&quot;";
        } else if (c == '\'') {
            out += "&#39;";
        } else {
            out += c;
        }
    }
    return out;
}
std::string detail(const std::string& error) {
    // "HTTP 403: Wrong code" -> "Wrong code"
    const auto colon = error.find(": ");
    return error.rfind("HTTP ", 0) == 0 && colon != std::string::npos ? error.substr(colon + 2)
                                                                      : error;
}
bool code_complete() {
    return code_input.size() == 6;
}
ModResult build_code_field(ModContext* ctx, UiElementHandle pane, void*, ModError*) {
    UiControlDesc field = UI_CONTROL_DESC_INIT;
    field.kind = UI_CONTROL_STRING;
    field.label = "Verification code";
    field.max_length = 6;
    field.string_set_mode = UI_STRING_SET_ON_CHANGE;
    field.get = [](ModContext*, void*, UiControlValue* value) {
        value->string_value = code_input.c_str();
    };
    field.set = [](ModContext*, void*, const UiControlValue* value) {
        code_input.clear();
        for (const char* c = value->string_value ? value->string_value : "";
             *c && code_input.size() < 6; ++c) {
            if (*c >= '0' && *c <= '9') {
                code_input += *c;
            }
        }
    };
    UiElementHandle element = 0;
    return svc_ui->pane_add_control(ctx, pane, &field, &element);
}
void dismissed(ModContext*, UiDialogHandle, void*) {
    dialog = 0;
    online::discord_cancel();
}
void show_dialog(int stage, const std::string& body) {
    if (dialog) {
        auto previous = dialog;
        dialog = 0;
        svc_ui->dialog_close(mod_ctx, previous);
    }
    UiDialogAction actions[2] = {UI_DIALOG_ACTION_INIT, UI_DIALOG_ACTION_INIT};
    actions[0].label = "Close";
    actions[0].on_pressed = dismissed;
    if (stage == 1) {
        actions[1].label = "Link account";
        actions[1].keep_open = true;
        actions[1].is_disabled = [](ModContext*, void*) {
            return online::busy() || !code_complete();
        };
        actions[1].on_pressed = [](ModContext*, UiDialogHandle, void*) {
            online::discord_confirm(code_input);
        };
    } else if (stage == 2) {
        actions[1].label = "Sign out";
        actions[1].is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        actions[1].on_pressed = [](ModContext*, UiDialogHandle, void*) {
            dialog = 0;
            online::discord_sign_out();
        };
    }
    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = stage == 2 ? "Discord account" : "Link Discord";
    desc.body_rml = body.c_str();
    desc.actions = actions;
    desc.action_count = (stage == 1 || stage == 2) ? 2 : 1;
    desc.on_dismiss = dismissed;
    if (stage == 1) {
        desc.build = build_code_field;
    }
    dialog_stage = stage;
    if (svc_ui->dialog_push(mod_ctx, &desc, &dialog) != MOD_OK) {
        dialog = 0;
    }
    last_body = body;
}
}  // namespace
void open() {
    if (dialog || online::busy()) {
        return;
    }
    opened_session.clear();
    next_poll = {};
    code_input.clear();
    show_dialog(
        0, "<div style='display:block;text-align:center;padding:20dp;'>Preparing sign-in...</div>");
    if (dialog) {
        online::discord_begin();
    }
}
void attach(UiElementHandle pane) {
    profile_image = 0;
    last_avatar.clear();
    refresh_profile = true;
    svc_ui->pane_add_rml(mod_ctx, pane, "<div/>", &profile_image);
    svc_ui->elem_set_class(mod_ctx, profile_image, "discord-profile-image", true);
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.label = "Account";
    control.on_pressed = [](ModContext*, void*) {
        open();
    };
    UiElementHandle button = 0;
    svc_ui->pane_add_control(mod_ctx, pane, &control, &button);
    svc_ui->elem_set_class(mod_ctx, button, "discord-profile-button", true);
}
void update(bool menu_open) {
    if (menu_open && refresh_profile && !online::busy() && !dialog) {
        refresh_profile = false;
        online::discord_refresh();
    }
    if (menu_open && profile_image) {
        auto profile = online::discord_link();
        auto image = profile.avatar.empty() ? "mod://local.dungeon_rush/res/icons/discord.png"
                                            : profile.avatar;
        if (image != last_avatar) {
            svc_ui->elem_set_rml(
                mod_ctx, profile_image,
                ("<img src='" + escaped(image) + "' style='width:36dp;height:36dp;'/>").c_str());
            last_avatar = image;
        }
    }
    if (!dialog) {
        return;
    }
    const auto link = online::discord_link();
    std::string text;
    if (link.status == "linked") {
        text = "Signed in as <b>" + escaped(link.username) + "</b>";
    } else if (link.status == "confirm") {
        text = "Continue as <b>" + escaped(link.username) +
               "</b>? Enter the code shown in your browser.";
        if (!link.error.empty()) {
            text += "<br/><span style='color:#f0b9a8;'>" + escaped(detail(link.error)) + "</span>";
        }
    } else if (link.status == "failed") {
        const auto error = online::status();
        text = error.find("not configured") != std::string::npos
                   ? "Discord sign-in is not available yet."
                   : "Could not sign in. Please close this window and try again.";
    } else if (link.status == "cancelled") {
        text = "Sign-in cancelled. Close this popup to try again.";
    } else {
        text = link.status == "starting" ? "Preparing sign-in..."
                                         : "Finish signing in through your browser.";
    }
    if (link.status == "pending" && opened_session != link.session && !link.browser_url.empty()) {
        opened_session = link.session;
        const std::wstring url(link.browser_url.begin(), link.browser_url.end());
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr,
                                                    SW_SHOWNORMAL)) <= 32) {
            text = "Could not open your browser. Close this popup and try again.";
        }
    }
    const auto body =
        std::string(
            "<div style='display:flex;flex-direction:column;align-items:center;padding:16dp 24dp;text-align:center;'>") +
        "<img src='mod://local.dungeon_rush/res/icons/discord.png' style='display:block;width:44dp;height:44dp;margin:0 0 16dp;'/><div style='display:block;width:100%;font-size:18dp;line-height:1.4;'>" +
        text + "</div></div>";
    const int stage = link.status == "confirm" ? 1 : link.status == "linked" ? 2 : 0;
    if (stage != dialog_stage) {
        show_dialog(stage, body);
    } else if (body != last_body) {
        svc_ui->dialog_set_body(mod_ctx, dialog, body.c_str());
        last_body = body;
    }
    const auto now = std::chrono::steady_clock::now();
    if ((link.status == "pending" || link.status == "browser" || link.status == "processing") &&
        now >= next_poll && !online::busy()) {
        next_poll = now + std::chrono::seconds(2);
        online::discord_poll();
    }
}
void close() {
    if (dialog) {
        auto current = dialog;
        dialog = 0;
        svc_ui->dialog_close(mod_ctx, current);
    }
}
}  // namespace rush::discord_ui
