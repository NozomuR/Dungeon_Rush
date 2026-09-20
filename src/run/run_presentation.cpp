// The results screen, its music, the slow-motion finish, and the READY / GO countdown on the HUD.
#include "run/run_internal.hpp"

#include "run/combos.hpp"
#include "rules/dungeon_starts.hpp"
#include "menu/equipment_ui.hpp"
#include "ghost/ghost.hpp"
#include "replay/replay.hpp"
#include "run/timer_ui.hpp"

#include "d/d_meter2.h"
#include "d/d_meter2_info.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_graphic.h"

#include <cmath>
#include <random>

namespace rush::run {

void choose_results_music() {
    // Independent RNG: never perturb the dungeon's deterministic game RNG.
    static std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    int next = std::uniform_int_distribution<int>(0, last_results_track < 0 ? 3 : 2)(rng);
    if (last_results_track >= 0 && next >= last_results_track) {
        ++next;
    }
    results_track = last_results_track = next;
    const u32 ids[] = {0x02000000u | results_music_id, 0x02000000u | fairy_music_id, Z2BGM_FISHING,
                       Z2BGM_SHOP_MARO};
    results_sound_id = ids[next];
}

void results_closed(ModContext*, UiWindowHandle handle, void*) {
    // Programmatic closes may finish their animation after a retry has begun.
    // A stale close must never release the new run's countdown hold.
    if (handle != results_window) {
        return;
    }
    results_window = 0;
    clear_presentation();
    if (phase == Phase::Finished) {
        world_held = false;
    }
}

void clear_presentation() {
    timer_ui::end_finish_blur();
    slowdown_blur_requested = false;
    if (scale_owned) {
        set_scale(original_scale);
        scale_owned = false;
    }
    if (results_music) {
        results_music->stop(0);
        results_music.releaseSound();
    }
    results_music_started = false;
    if (owned_results_wave) {
        if (auto* sound = Z2GetAudioMgr()) {
            if (owned_results_wave != sound->requestBgmWave_1 &&
                owned_results_wave != sound->requestBgmWave_2) {
                sound->eraseBgmWave(owned_results_wave);
            }
        }
        owned_results_wave = 0;
    }
    if (effects_volume_owned) {
        if (auto* sound = Z2GetAudioMgr()) {
            sound->mSoundMgr.getSeMgr()->getParams()->moveVolume(original_effects_volume, 0);
        }
        effects_volume_owned = false;
    }
}

void close_results() {
    clear_presentation();
    if (results_window) {
        const auto handle = results_window;
        results_window = 0;
        svc_ui->window_close(mod_ctx, handle);
    }
}

void retry_pressed(ModContext*, void*) {
    request(false, boss_run, active_dungeon);
}

void next_pressed(ModContext*, void*) {
    if (gauntlet(active_mode) && outcome == "Finished" && series.has_next() &&
        series.completed == active_dungeon + 1) {
        request(false, boss_run, series.completed);
    }
}

void dismiss_pressed(ModContext*, void*) {
    return_pending = true;
}

ModResult build_results(ModContext* ctx, UiWindowHandle, UiElementHandle left,
                        UiElementHandle right, void*, ModError*) {
    auto result = svc_ui->pane_add_rml(
        ctx, left,
        ("<div class='result-summary'><div class='result-title'>" +
         std::string(boss_run ? kStarts[active_dungeon].boss : kDungeons[active_dungeon].name) +
         "</div><h1>" + time_text(record.elapsed(now_ms())) + "</h1><p>" +
         (outcome == "Finished" ? (boss_run ? "Boss complete" : "Run complete") : outcome) +
         "</p></div>")
            .c_str(),
        nullptr);
    if (result != MOD_OK) {
        return result;
    }
    const bool next = gauntlet(active_mode) && series.has_next() && outcome == "Finished";
    if (gauntlet(active_mode)) {
        const auto summary = "<p>" + std::to_string(series.completed) + " / 9 cleared · Total " +
                             time_text(series.total()) + "</p>";
        svc_ui->pane_add_rml(ctx, left, summary.c_str(), nullptr);
    }
    for (const auto& action :
         {std::pair{next ? (boss_run ? "Next boss" : "Next dungeon") : "Retry",
                    next ? next_pressed : retry_pressed},
          std::pair{gauntlet(active_mode)
                        ? (boss_run ? "Exit boss gauntlet" : "Exit dungeon gauntlet")
                        : "Back to selection",
                    dismiss_pressed}}) {
        if (gauntlet(active_mode) && outcome == "Finished" && !next &&
            action.second == retry_pressed) {
            continue;
        }
        UiControlDesc control = UI_CONTROL_DESC_INIT;
        control.label = action.first;
        control.on_pressed = action.second;
        result = svc_ui->pane_add_control(ctx, left, &control, nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    if (next || !(gauntlet(active_mode) && outcome == "Finished")) {
        UiControlDesc equip = UI_CONTROL_DESC_INIT;
        equip.label = next ? "Equip for next run" : "Equip for retry";
        equip.on_pressed = [](ModContext* ctx, void*) {
            const auto target = gauntlet(active_mode) && outcome == "Finished" && series.has_next()
                                    ? series.completed
                                    : active_dungeon;
            equipment_ui::open(ctx, svc_ui, target, boss_run);
        };
        result = svc_ui->pane_add_control(ctx, left, &equip, nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    if (!last_recording.empty()) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = active_mode == Mode::Dungeon ? "Preview / Submit run" : "Preview replay";
        c.is_disabled = [](ModContext*, void*) {
            return last_recording.empty();
        };
        c.on_pressed = [](ModContext*, void*) {
            replay::open(last_recording);
        };
        svc_ui->pane_add_control(ctx, left, &c, nullptr);
    }
    std::string html = gauntlet(active_mode) ? "<h2>Gauntlet splits</h2>" : "<h2>Splits</h2>";
    auto displayed = record.splits;
    if (gauntlet(active_mode)) {
        displayed.clear();
        int64_t cumulative = 0;
        for (size_t i = 0; i < series.completed; ++i) {
            cumulative += series.times[i];
            displayed.push_back(
                {std::to_string(i), boss_run ? kStarts[i].boss : kDungeons[i].name, cumulative});
        }
    }
    unsigned row = 0;
    int64_t previous = 0;
    for (const auto& s : displayed) {
        html += "<div class='split-row' style='animation: " + std::to_string(1.1f + .12f * row++) +
                "s cubic-out result-row;'><span class='split-label'>" + s.label +
                "</span><span class='split-time'>" + time_text(s.milliseconds) +
                "</span><span class='segment'>+" + time_text(s.milliseconds - previous) +
                "</span></div>";
        previous = s.milliseconds;
    }
    return svc_ui->pane_add_rml(ctx, right, html.c_str(), nullptr);
}

void show_results() {
    timer_ui::end_finish_blur();  // Remove the topmost blur before pushing sharp results.
    static constexpr char style[] = R"(
        body { padding: 0; background-color: transparent; backdrop-filter: blur(5dp); font-family: Fira Sans;
            decorator: radial-gradient(ellipse farthest-corner at 50% 42%, #03090e88 20%, #03090ea6 65%, #03090ee8 100%); }
        body window { width: 100%; height: 100%; max-width: none; max-height: none;
            margin: 0; border-width: 0; border-radius: 0; box-shadow: none;
            background-color: transparent; backdrop-filter: none; transform: none; }
        body window[open] { transform: none; }
        body window tab-bar, body window > close { display: none; }
        body window content { align-items: center; padding: 5% 6%; gap: 7%; }
        body window content pane { flex: 1 1 0; height: auto; max-height: 90%; padding: 0;
            background-color: transparent; border-width: 0; gap: 12dp; }
        body window content pane:not(:last-of-type) { border-right-width: 0; }
        div,h1,h2,p { display: block; }
        h1 { font-size: 96dp; color: #f3fff9; margin: 22dp 0; animation: 1.1s cubic-out result-rise; }
        h2 { font-size: 28dp; color: #f3fff9; margin-bottom: 24dp; animation: 1.2s cubic-out result-row; }
        p { font-size: 16dp; color: #dae9e2; animation: 1.3s cubic-out result-row; }
        .result-summary { text-align: center; }
        .result-title { font-size: 30dp; color: #c2eee0; animation: .8s cubic-out result-rise; }
        .split-row { display: flex; align-items: baseline; padding: 16dp 0; border-bottom: 1dp #d9fff244; font-size: 23dp; color: #f1f7f3; }
        .split-label { flex: 1 1 0; min-width: 0; padding-right: 12dp; }
        .split-time { flex: 0 0 145dp; text-align: right; font-family: Noto Mono; }
        .segment { flex: 0 0 120dp; text-align: right; font-family: Noto Mono; color: #dae9e2; font-size: 17dp; }
        @media (max-width: 1100dp) {
            h1 { font-size: 68dp; }
            .split-row { font-size: 18dp; padding: 12dp 0; }
            .split-time { flex-basis: 115dp; }
            .segment { font-size: 15dp; flex-basis: 110dp; }
        }
        body window button { background-color: #08110c55; border-color: #c4e7db44; border-radius: 4dp; animation: 1.6s cubic-out result-row; }
        @keyframes result-rise { from { opacity: 0; transform: translateY(28dp); } to { opacity: 1; transform: translateY(0dp); } }
        @keyframes result-row { 0% { opacity: 0; transform: translateY(18dp); } 35% { opacity: 0; transform: translateY(18dp); } 100% { opacity: 1; transform: translateY(0dp); } }
    )";
    UiTabDesc tab = UI_TAB_DESC_INIT;
    tab.title = "Results";
    tab.build = build_results;
    tab.update = [](ModContext*, void*, ModError*) {
        update();
        return MOD_OK;
    };
    UiWindowDesc window = UI_WINDOW_DESC_INIT;
    window.tabs = &tab;
    window.tab_count = 1;
    window.rcss = style;
    window.on_closed = results_closed;
    opening_results = true;
    const auto pushed = svc_ui->window_push(mod_ctx, &window, &results_window);
    opening_results = false;
    if (pushed != MOD_OK) {
        clear_presentation();
        world_held = false;
        toast("Results could not open. Run saved locally.");
        return;
    }
    // Transfer the finished blur to the results backdrop so overlay text stays sharp.
    timer_ui::end_finish_blur();
    // Actors/events stay held. Keep the clock alive for audio stream preparation
    // and stop/fade commands; scaling to zero stalled those along with the scene.
    if (scale_owned) {
        set_scale(original_scale);
    }
}

HookAction menu_sound(ModContext*, void* args, void*, void*) {
    // Silence only the host window-open cue during our results push. Retry,
    // navigation and unrelated windows keep their ordinary menu feedback.
    if (opening_results && mods::arg<u32>(args, 0) == Z2SE_SY_MENU_NEXT) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void presentation_tick(ModContext*, void*, void*, void*) {
    if (world_held && sync_presentation) {
        sync_presentation();
    }
    // The last part of the slowdown has very few simulation ticks. Advance the
    // transition from the presentation loop so it still finishes on real time.
    if (phase == Phase::Cinematic) {
        update();
    }
    const bool visible =
        !savewarp_pending &&
        (phase == Phase::Loading || phase == Phase::Ready || phase == Phase::Running) &&
        dComIfGp_getPlayer(0);
    std::string latest;
    if (phase == Phase::Running && !record.splits.empty()) {
        const auto& split = record.splits.back();
        latest = split.label + "  " + time_text(split.milliseconds);
    }
    timer_ui::update(visible,
                     phase == Phase::Running ? time_text(record.elapsed(now_ms())) : "0:00.000",
                     latest, combos::hold_label(), combos::hold_progress());
}

HookAction effects_volume(ModContext*, void* args, void*, void*) {
    if (phase == Phase::Running && in_run_stage()) {
        const float volume = mods::arg<float>(args, 1);
        svc_log->info(mod_ctx,
                      ("Rush audio: effects fade target=" + std::to_string(volume)).c_str());
    }
    return HOOK_CONTINUE;
}

HookAction countdown_draw(ModContext*, void* args, void* result, void*) {
    // Recording/camera settings can suppress the normal meter draw. Keep the
    // native Ready/Start animation visible regardless of those HUD settings.
    auto* meter = mods::arg<dMeter2_c*>(args, 0);
    if ((phase == Phase::Ready || (phase == Phase::Running && record.elapsed(now_ms()) < 3000)) &&
        meter->getSubContents() == 3 && meter->mpSubContents) {
        dComIfGd_set2DOpaTop(meter->mpSubContents);
        *static_cast<int*>(result) = 1;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

}  // namespace rush::run
