#include "ghost/ghost.hpp"
#include <cstring>
#include <mods/svc/hook.hpp>

// Exact overloads from this runtime's symbol table. Use the host's ImGui
// implementation and context; never compile a second ImGui into the mod.
DEFINE_HOOK_SYMBOL("?BeginMenu@ImGui@@YA_NPEBD_N@Z", bool(const char*, bool), HostBeginMenu);
DEFINE_HOOK_SYMBOL("?MenuItem@ImGui@@YA_NPEBD0_N1@Z", bool(const char*, const char*, bool, bool),
                   HostMenuItem);

namespace rush::ghost {
ModResult initialize_debug_menu() {
    if (!HostMenuItem::resolved_target()) {
        return MOD_UNAVAILABLE;
    }
    return mods::hook::add_post<HostBeginMenu>(
        [](ModContext*, void* arguments, void* result, void*) {
            if (!result || !*static_cast<bool*>(result)) {
                return;
            }
            const auto* label = mods::arg<const char*>(arguments, 0);
            if (!label || std::strcmp(label, "Debug") != 0) {
                return;
            }
            const auto menuItem = reinterpret_cast<bool (*)(const char*, const char*, bool, bool)>(
                HostMenuItem::resolved_target());
            if (menuItem("Spawn ghost puppet", nullptr, false, can_spawn_test())) {
                spawn_test();
            }
            if (menuItem("Remove test ghost", nullptr, false, true)) {
                remove_test();
            }
        });
}
}  // namespace rush::ghost
