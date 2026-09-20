#pragma once
#include <mods/service.hpp>
#include <mods/svc/ui.h>
namespace rush::discord_ui {
void open();
void update(bool menu_open);
void attach(UiElementHandle pane);
void close();
}  // namespace rush::discord_ui
