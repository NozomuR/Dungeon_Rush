#ifndef DUSK_GHOST_HPP
#define DUSK_GHOST_HPP

/**
 * ghost.hpp
 * Mario Kart style ghost run recording & playback for speedrun practice.
 *
 * The manager records the player's skeleton + equipment each sim tick and
 * replays it through a lightweight puppet actor (g_profile_DUSK_GHOST_PUPPET)
 * that renders Link (human or wolf) with the gear he had on every frame.
 * Base recordings remain compatible. Extra part animations live in a .tpa
 * sidecar; ghost_rig.cpp owns the puppet's models, resources and animation.
 */

#include <cstdint>
#include <cstddef>
#include <mods/service.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include "ghost/ghost_visuals.hpp"

#include "global.h"
#include "SSystem/SComponent/c_xyz.h"
#include "SSystem/SComponent/c_sxyz.h"

namespace rush::ghost {

// ------------------------------------------------------------------
// Recorded data
// ------------------------------------------------------------------

enum ClothesKind : u8 {
    ClothesHero = 0,  // Kmdl
    ClothesOrdon,     // Bmdl
    ClothesZora,      // Zmdl
    ClothesMagic,     // Mmdl
    kNumClothes,
};

enum SwordKind : u8 {
    SwordNone = 0,
    SwordWood,    // al_SWB.bmd (clothes arc)
    SwordOrdon,   // Alink AL_SWA
    SwordMaster,  // Alink AL_SWM
    kNumSwords,
};

enum ShieldKind : u8 {
    ShieldNone = 0,
    ShieldWoodCarved,  // CWShd
    ShieldWoodShop,    // SWShd
    ShieldHylian,      // HyShd
    kNumShields,
};

enum HeldKind : u8 {
    HeldNone = 0,
    HeldBoomerang,    // Alink AL_BOOM
    HeldBow,          // AlAnm AL_BOW
    HeldSlingshot,    // AlAnm AL_PACHI
    HeldClawshot,     // AlAnm AL_HS (+ tip)
    HeldBallChain,    // AlAnm AL_IB
    HeldDominionRod,  // AlAnm AL_CROD
    kNumHeld,
};

// Equipment matrix slots stored per frame (world-space base matrices).
enum EquipSlot : u8 {
    SlotSword = 0,
    SlotSheath,
    SlotShield,
    SlotHeld,
    SlotHookTip,
    SlotKantera,
    SlotChain0,
    SlotChain1,
    SlotChain2,
    SlotChain3,
    kNumEquipSlots,
};

constexpr u8 kFormHuman = 0;
constexpr u8 kFormWolf = 1;
constexpr u32 kMaxJointCount = 96;

// 3x4 row-major matrix, matches Mtx layout.
struct GhostMtx {
    f32 m[3][4];
};

struct FrameInfo {
    uint64_t milliseconds = 0;
    cXyz pos;
    csXyz angle;
    u8 stageIdx;  // index into Recording::stages
    s8 roomNo;
    u8 form;            // kFormHuman / kFormWolf
    u8 clothes;         // ClothesKind
    u8 sword;           // SwordKind
    u8 shield;          // ShieldKind
    u8 held;            // HeldKind
    u8 jointCount;      // body joint count for this frame
    u8 leftHandShape;   // material index of shown left hand shape (0xFF = none found)
    u8 rightHandShape;  // material index of shown right hand shape
    u16 visMask;        // bit per EquipSlot
    u32 mtxOffset;      // into mtxPool: [base][jointCount anm][kNumEquipSlots equips]
    u32 flagOffset;     // into scaleFlagPool: jointCount entries
};

struct StageName {
    char name[12];
};

struct Recording {
    VisualTrack visuals;
    std::vector<FrameInfo> frames;
    std::vector<GhostMtx> mtxPool;
    std::vector<u8> scaleFlagPool;
    std::vector<StageName> stages;

    // resource usage summary (computed by computeUsage / on load)
    u8 clothesMask = 0;  // bit per ClothesKind
    u8 swordMask = 0;    // bit per SwordKind
    u8 shieldMask = 0;   // bit per ShieldKind
    u8 heldMask = 0;     // bit per HeldKind
    bool anyWolf = false;
    bool anyHuman = false;

    void clear();
    void computeUsage();
    bool empty() const { return frames.empty(); }
};

// ------------------------------------------------------------------
// Manager API
// ------------------------------------------------------------------

// Captures at simulation ticks; playback follows run-relative wall-clock timestamps.
void on_sim_tick();
uint64_t render_tick();
bool can_spawn_test();
void spawn_test();
void remove_test();
ModResult initialize();
ModResult initialize_debug_menu();
ModResult register_puppet();
ModResult unregister_puppet();
ModResult shutdown();
extern int16_t puppet_profile;
bool resources_busy();
bool prepare_loaded();
inline int race_mode = 1;  // 0 off, 1 latest matching attempt, 2 best matching completion
void clear_loaded();
bool recording_truncated();

void start_recording(int64_t epoch);
void stop_recording();
bool start_playback(int64_t epoch, std::string* o_err = nullptr);
void stop_playback();
// Start playback of loaded ghost AND record a fresh take at the same time.

void stop_all();
void suspend_scene(bool suspended);

bool is_recording();
bool is_playing();
u32 record_frame_count();
u32 playback_position();
u32 playback_length();
bool playback_finished();

// Most recent finished take (stop_recording keeps it for saving).
bool has_take();
u32 take_frame_count();
// Currently loaded ghost.
bool has_ghost();
u32 ghost_frame_count();
const char* ghost_start_stage();

// Persistence (files under <data>/ghosts/*.tpg)
std::filesystem::path ghosts_dir();
bool save_take(const std::string& fileName, std::string* o_err = nullptr);
bool load_ghost_file(const std::filesystem::path& path, std::size_t expected_dungeon,
                     std::string* o_err = nullptr, bool require_visuals = false);

// Stable RGB identity for the currently loaded recording.
uint32_t ghost_color();

// ------------------------------------------------------------------
// Puppet interface (used by ghost_rig.cpp)
// ------------------------------------------------------------------

struct PuppetFrameView {
    const VisualTrack* visuals = nullptr;
    const VisualFrame* visualFrame = nullptr;
    const FrameInfo* info = nullptr;
    const GhostMtx* baseMtx = nullptr;   // 1 entry: player body base TR mtx
    const GhostMtx* bodyMtx = nullptr;   // jointCount entries (world space)
    const GhostMtx* equipMtx = nullptr;  // kNumEquipSlots entries (world space)
    const u8* scaleFlags = nullptr;      // jointCount entries
    bool valid() const { return info != nullptr; }
};

// Current playback frame if it should be shown on the current stage.
PuppetFrameView puppet_frame();
// Recording being played back (for resource usage info). NULL when idle.
const Recording* playback_recording();

}  // namespace rush::ghost

#endif  // DUSK_GHOST_HPP
