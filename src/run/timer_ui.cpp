#include "run/timer_ui.hpp"
#include <mods/svc/hook.h>
#include <mods/svc/log.h>
#include <algorithm>
#include <chrono>
namespace rush::timer_ui {
namespace {
// Matching runtime's exported MSVC x64 methods. Objects stay opaque; no copied
// RmlUi class layouts. Show uses None for modal, focus and scroll (all zero).
void* (*context)() noexcept = nullptr;
void* (*load_document)(void*, const std::string&, const std::string&) = nullptr;
void (*show)(void*, int, int, int) = nullptr;
void (*hide)(void*) = nullptr;
void (*close_document)(void*) = nullptr;
void* (*find_element)(void*, const std::string&) = nullptr;
void (*set_rml)(void*, const std::string&) = nullptr;
void *document = nullptr, *clock_element = nullptr, *split_element = nullptr,
     *hint_element = nullptr;
bool shown = false, failed = false;
void* blur_document = nullptr;
std::string last_time, last_split, last_hint;
constexpr char source[] = R"(
<rml><head><link type="text/rcss" href="res/rml/theme.rcss"/><style>
body { width:100%; height:100%; margin:0; padding:0; pointer-events:none;
 font-family:Fira Sans; color:#f3fff9; display:flex; justify-content:center;
 align-items:flex-start; z-index:1; }
#timer { margin-top:28dp; padding:10dp 28dp; min-width:220dp;
 text-align:center; background-color:#06110c55; border-radius:6dp;
 border-bottom:1dp #d9fff233; animation:.45s cubic-out timer-in; }
#clock,#split { display:block; font-family:Noto Mono; }
#clock { font-size:42dp; }
#split { font-size:15dp; color:#a7ddcd; margin-top:3dp; }
#hint { display:block; margin-top:8dp; }
#hint .hold-label { display:block; font-size:13dp; color:#a7ddcd; margin-bottom:5dp; }
#hint .track { display:block; position:relative; width:100%; height:7dp;
 border:1dp #b1dbcc38; border-radius:4dp; background-color:#06110c66; overflow:hidden; }
#hint .fill { display:block; position:absolute; left:0; top:0; height:100%;
 background-color:#3d6f5f; border-radius:3dp; overflow:hidden; }
#hint .wave { display:block; position:absolute; top:0; width:45%; height:100%;
 decorator:linear-gradient(to right, #a0eed600, #d4ffedd0, #a0eed600); }
@keyframes timer-in { from { opacity:0; transform:translateY(-10dp); }
 to { opacity:1; transform:translateY(0dp); } }
</style></head><body><div id="timer"><div id="clock"/><div id="split"/><div id="hint"/></div></body></rml>)";
std::string escaped(const std::string& s) {
    std::string out;
    for (char c : s) {
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
}  // namespace
ModResult initialize() {
#define RESOLVE(name, dest)                                                                 \
    do {                                                                                    \
        auto r = svc_hook->resolve(mod_ctx, name, reinterpret_cast<void**>(dest), nullptr); \
        if (r != MOD_OK)                                                                    \
            return r;                                                                       \
    } while (false)
    RESOLVE("aurora::rmlui::get_context", &context);
    RESOLVE("Rml::Context::LoadDocumentFromMemory", &load_document);
    RESOLVE("Rml::ElementDocument::Show", &show);
    RESOLVE("Rml::ElementDocument::Hide", &hide);
    RESOLVE("Rml::ElementDocument::Close", &close_document);
    RESOLVE("Rml::Element::GetElementById", &find_element);
    RESOLVE("Rml::Element::SetInnerRML", &set_rml);
#undef RESOLVE
    return MOD_OK;
}
std::string hold_bar_rml(const std::string& label, float progress) {
    if (progress <= 0.0f) {
        return "";
    }
    const int percent = static_cast<int>(std::min(progress, 1.0f) * 100.0f);
    // A highlight band slides across the filled part about once a second.
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    const float phase = static_cast<float>(ms % 900) / 900.0f;
    const int wave_left =
        -45 + static_cast<int>(phase * 145.0f);  // enters from the left, exits right
    return "<div class='hold-label'>" + escaped(label) +
           "</div><div class='track'><div class='fill' style='width:" + std::to_string(percent) +
           "%;'><div class='wave' style='left:" + std::to_string(wave_left) + "%;'/></div></div>";
}
void update(bool visible, const std::string& time, const std::string& split,
            const std::string& hold_label, float hold_progress) {
    if (!visible) {
        if (document && shown) {
            hide(document);
        }
        shown = false;
        return;
    }
    if (failed || !context) {
        return;
    }
    if (!document) {
        auto* ctx = context();
        if (!ctx) {
            return;
        }
        document = load_document(ctx, source, "dungeon-rush-timer.rml");
        if (!document) {
            failed = true;
            svc_log->warn(mod_ctx, "Rush: timer RML could not load");
            return;
        }
        clock_element = find_element(document, "clock");
        split_element = find_element(document, "split");
        hint_element = find_element(document, "hint");
        if (!clock_element || !split_element || !hint_element) {
            close();
            failed = true;
            return;
        }
    }
    if (time != last_time) {
        set_rml(clock_element, escaped(time));
        last_time = time;
    }
    if (split != last_split) {
        set_rml(split_element, escaped(split));
        last_split = split;
    }
    const auto hint = hold_bar_rml(hold_label, hold_progress);
    if (hint != last_hint) {
        set_rml(hint_element, hint);
        last_hint = hint;
    }
    if (!shown) {
        show(document, 0, 0, 0);
        shown = true;
    }
}
bool begin_finish_blur() {
    end_finish_blur();
    if (!context || !context()) {
        return false;
    }
    // Own the animation on a disposable document. Removing an animated stylesheet
    // from the host overlay can leave its final animated property on that document.
    static const std::string blur_source = R"(
 <rml><head><style>
 body { width:100%; height:100%; margin:0; padding:0; pointer-events:none;
  background-color:transparent; backdrop-filter:blur(5dp);
  animation:2s cubic-in-out rush-finish-blur; }
 @keyframes rush-finish-blur {
  from { backdrop-filter:blur(0dp); }
  to { backdrop-filter:blur(5dp); }
 }
 </style></head><body/></rml>)";
    blur_document = load_document(context(), blur_source, "dungeon-rush-finish-blur.rml");
    if (!blur_document) {
        return false;
    }
    show(blur_document, 0, 0, 0);
    return true;
}
void end_finish_blur() {
    if (!blur_document) {
        return;
    }
    // Hide synchronously: Close may defer destruction until the next context update.
    hide(blur_document);
    close_document(blur_document);
    blur_document = nullptr;
}
void close() {
    end_finish_blur();
    if (document) {
        close_document(document);
    }
    document = clock_element = split_element = hint_element = nullptr;
    shown = false;
    failed = false;
    last_time.clear();
    last_split.clear();
    last_hint.clear();
}
}  // namespace rush::timer_ui
