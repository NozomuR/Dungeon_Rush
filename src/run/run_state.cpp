// Definitions of the state declared in run_internal.hpp. Comments on what each group
// means live in the header; this file only gives the variables storage and defaults.
#include "run/run_internal.hpp"

namespace rush::run {

// Which run this is.
std::size_t active_dungeon = 0;
Mode active_mode = Mode::Dungeon;
Gauntlet series;
std::string series_id;
Category active_category = Category::Glitchless;
bool boss_run = false;

// Where the state machine is.
Phase phase = Phase::Idle;
bool initialized = false, restore_pending = false, world_held = false;
bool session_ready = false;
bool entrance_settling = false;
bool return_pending = false;
int64_t phase_at = 0, next_sample = 0;
int64_t next_audio_check = 0;
bool bank_warning = false;
std::string outcome;
int64_t finish_lead_in_ms = 7000;

// Save-and-quit resume.
bool savewarp_pending = false, savewarp_loaded = false, savewarp_scene_ready = false;

// The loadout the run started with.
std::size_t active_shield = 0;
uint32_t active_gear = forest;
int active_hearts = 3;
bool active_master = false;
std::array<int, 2> active_equipped{-2, -2};

// Boss fight progress.
bool boss_intro_seen = false, boss_intro_done = false;
int64_t boss_settle_at = 0;
bool phase_two_seen = false, phase_two_audio_restored = false;

// Recording.
dSv_save_c baseline;
Record record;
std::vector<Position> route;
bool route_truncated = false;
std::filesystem::path selected_recording, last_recording;
void* monkey_actor = nullptr;
unsigned monkey_count = 0, key_count = 0;
bool ghost_prepared = false;

// Results screen and presentation.
UiWindowHandle results_window = 0;
bool slowdown_blur_requested = false;
bool opening_results = false;
JAISoundHandle results_music;
u8 owned_results_wave = 0;
uint16_t results_music_id = 0, fairy_music_id = 0;
int results_track = 0, last_results_track = -1;
u32 results_sound_id = 0;
SetScale set_scale = nullptr;
void (*sync_presentation)() = nullptr;
GetScale get_scale = nullptr;
float original_scale = 1.f;
bool scale_owned = false, results_music_started = false;
bool effects_volume_owned = false;
float original_effects_volume = 1.f;
int64_t results_music_at = 0;

}  // namespace rush::run
