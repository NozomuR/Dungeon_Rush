#pragma once
// Internal to the run module: the state every run_*.cpp file shares, and the functions
// they call across files. Nothing outside run_*.cpp should include this; run.hpp is the API.
#include "run/run.hpp"

#include <mods/svc/audio_res.h>
#include <mods/svc/game_mode.h>
#include <mods/svc/hook.hpp>
#include <mods/svc/host.h>
#include <mods/svc/item.h>
#include <mods/svc/log.h>
#include <mods/svc/resource.h>
#include <mods/svc/ui.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/d_com_inf_game.h"
#include "run/run_record.hpp"
#include "rules/presets.hpp"
#include "rules/categories.hpp"
#include "audio/audio_cache.hpp"

namespace rush::run {

inline constexpr int forest_save = 16;  // STAG save table in D_MN05/stage.dzs.
inline constexpr int entrance_room = 22;
inline constexpr int64_t finish_fade_ms = 2000;
inline constexpr const char* results_track_names[] = {"Spirit", "Great Fairy Fountain",
                                                      "Fishing Hole", "Malo Mart"};

enum class Phase { Idle, AwaitFile, Loading, Ready, Running, Cinematic, Finished };
using SetScale = void (*)(float) noexcept;
using GetScale = float (*)() noexcept;

struct Position {
    int64_t ms;
    std::string stage;
    int room;
    float x, y, z;
    short angle;
    float map_x, map_z;
};

// Which run this is.
extern std::size_t active_dungeon;
extern Mode active_mode;
extern Gauntlet series;
extern std::string series_id;
extern Category active_category;
extern bool boss_run;

// Where the state machine is.
extern Phase phase;
extern bool initialized, restore_pending, world_held;
extern bool session_ready;
extern bool entrance_settling;
extern bool return_pending;
extern int64_t phase_at, next_sample;
extern int64_t next_audio_check;
extern bool bank_warning;
extern std::string outcome;
extern int64_t finish_lead_in_ms;

// Save-and-quit resume.
extern bool savewarp_pending, savewarp_loaded, savewarp_scene_ready;

// The loadout the run started with.
extern std::size_t active_shield;
extern uint32_t active_gear;
extern int active_hearts;
extern bool active_master;
extern std::array<int, 2> active_equipped;

// Boss fight progress.
extern bool boss_intro_seen, boss_intro_done;
extern int64_t boss_settle_at;
extern bool phase_two_seen, phase_two_audio_restored;

// Recording.
extern dSv_save_c baseline;
extern Record record;
extern std::vector<Position> route;
extern bool route_truncated;
extern std::filesystem::path selected_recording, last_recording;
extern void* monkey_actor;
extern unsigned monkey_count, key_count;
extern bool ghost_prepared;

// Results screen and presentation.
extern UiWindowHandle results_window;
extern bool slowdown_blur_requested;
extern bool opening_results;
extern JAISoundHandle results_music;
extern u8 owned_results_wave;
extern uint16_t results_music_id, fairy_music_id;
extern int results_track, last_results_track;
extern u32 results_sound_id;
extern SetScale set_scale;
extern void (*sync_presentation)();
extern GetScale get_scale;
extern float original_scale;
extern bool scale_owned, results_music_started;
extern bool effects_volume_owned;
extern float original_effects_volume;
extern int64_t results_music_at;

// ---- cross-file functions -----------------------------------------------------

// run.cpp
const char* ruleset();
bool matching_ruleset(const std::string& rule);
int64_t now_ms();
bool mode_active();
bool in_run_stage();
bool ensure_scene_audio();
void toast(const char* title);
bool worth_saving(std::string_view status);
void finish(const char* status);

// run_baseline.cpp
void capture_loadout();
const char* entry_stage();
int entry_spawn();
int entry_layer();
int entry_save();
int entry_room();
void prepare_baseline();
HookAction game_start(ModContext*, void*, void*, void*);
HookAction play_entry(ModContext*, void*, void*, void*);
HookAction stage_init(ModContext*, void*, void*, void*);

// run_recording.cpp
std::string time_text(int64_t ms);
std::string json_string(const std::string& value);
void sample_position(int64_t timestamp = -1);
void save_record();
void prepare_ghost();
void start_ghost(int64_t epoch);
void expire_incomplete_recordings();

// run_presentation.cpp
void choose_results_music();
void results_closed(ModContext*, UiWindowHandle handle, void*);
void clear_presentation();
void close_results();
void retry_pressed(ModContext*, void*);
void next_pressed(ModContext*, void*);
void dismiss_pressed(ModContext*, void*);
ModResult build_results(ModContext* ctx, UiWindowHandle, UiElementHandle left,
                        UiElementHandle right, void*, ModError*);
void show_results();
HookAction menu_sound(ModContext*, void* args, void*, void*);
void presentation_tick(ModContext*, void*, void*, void*);
HookAction effects_volume(ModContext*, void* args, void*, void*);
HookAction countdown_draw(ModContext*, void* args, void* result, void*);

// run_game_hooks.cpp
void boss_post(ModContext*, void* args, void*, void*);
void start_ending();
void fyrus_post(ModContext*, void* args, void*, void*);
void morpheel_post(ModContext*, void* args, void*, void*);
void stallord_death_post(ModContext*, void* args, void*, void*);
void blizzeta_death_post(ModContext*, void* args, void*, void*);
void argorok_post(ModContext*, void* args, void*, void*);
void zant_post(ModContext*, void* args, void*, void*);
void gohma_post(ModContext*, void* args, void*, void*);
void ganondorf_post(ModContext*, void* args, void*, void*);
void split(std::string id, std::string label);
HookAction hold_actor(ModContext*, void*, void* result, void*);
HookAction hold_event(ModContext*, void*, void* result, void*);
HookAction monkey_pre(ModContext*, void* args, void*, void*);
void monkey_post(ModContext*, void*, void*, void*);
void treasure_post(ModContext*, void* args, void*, void*);
void item_given(ModContext*, const ItemGiveInfo* item, void*);

}  // namespace rush::run
