#pragma once
namespace rush {
inline constexpr char kStyle[] = R"RCSS(
body { padding:0; background-color:#07110e; font-family:Fira Sans; font-weight:normal; color:#f3fff9;
 animation:.55s cubic-out backdrop-in; }
body window { position:fixed; left:0; top:0; width:100vw; height:100vh; min-width:100vw; min-height:100vh; max-width:100vw; max-height:100vh; box-sizing:border-box; margin:0;
 border-width:0; border-radius:0; box-shadow:none; background-color:transparent;
 backdrop-filter:blur(4dp); transform:translateY(10dp); filter:opacity(0);
 transition:filter transform .35s cubic-in-out;
 decorator:radial-gradient(ellipse farthest-corner at 58% 38%, #03090e48 8%, #03090e99 60%, #03090eeb 100%); }
body window[open] { transform:translateY(0dp); filter:opacity(1); }
body window tab-bar,body window > close { display:none; }
body window content { align-items:center; padding:5% 6%; gap:7%; }
body window content pane { flex:1 1 0; height:auto; max-height:100%; padding:20dp;
 background-color:transparent; border-width:0; gap:10dp; font-family:Fira Sans; font-size:16dp;
 animation:.65s cubic-out menu-rise; }
body window content pane:not(:last-of-type) { border-right-width:0; }
body window content pane.unused { display:none; }
div,h1,h2,h3,p { display:block; }
h1 { font-family:Fira Sans; font-weight:normal; font-size:48dp; color:#f3fff9; margin:8dp 0 6dp; }
h2 { font-family:Fira Sans; font-weight:normal; font-size:28dp; margin:12dp 0 22dp; }
h3 { font-family:Fira Sans; font-weight:normal; font-size:21dp; margin:12dp 0; }
p { margin:6dp 0 12dp; line-height:1.5; color:#d1ded8; font-size:16dp; }
.label { font-size:11dp; font-weight:normal; letter-spacing:2dp; color:#b1dbcc; }
.subtitle { margin-bottom:24dp; color:#cfddd7; font-size:18dp; }
.gallery-heading { margin-bottom:18dp; }
.gallery-heading h1 { margin:0; text-align:center; font-size:36dp; }
body window content pane.gallery { flex:1.1 1 0; padding:24dp 12dp 24dp 0; gap:8dp;
 animation:.65s cubic-out menu-rise-margin; }
/* No transform/position here or the fixed Discord button sticks to this pane. */
body window content pane.gallery-summary { flex:1 1 0; padding:24dp 28dp;
 border-left:1dp #d9fff244; background-color:#07100d44; border-radius:4dp; }
body window content pane.rankings { flex:1 1 0; padding:18dp 24dp; border-radius:5dp;
 background-color:#06100c66; border-left:1dp #d9fff244; }
body window content pane.settings-pane { background-color:#06100c99; border:1dp #d9fff233; border-radius:5dp; padding:26dp; }
body window button { font-family:Fira Sans; font-size:18dp; font-weight:normal;
 border-radius:4dp; border:1dp #c4e7db44; background-color:#08110c88;
 color:#f3fff9; box-shadow:none; padding:12dp 18dp;
 transition:background-color border-color transform .18s cubic-out; }
body window button:not(:disabled):hover,body window button:not(:disabled):focus-visible {
 background-color:#28483dcc; border-color:#a0eed6; transform:translateX(3dp); }
body window button.begin { background-color:#264e43a6; border-color:#a0e4ce88; }
body window button:disabled { opacity:.4; }
body window button.back { background-color:#07100d44; border-color:#c4e7db22; color:#d1ded8; font-size:15dp; }
body window button.dungeon-card { box-sizing:border-box; flex:0 0 auto; width:75%; align-self:center; height:3.75vw; min-height:37.5dp; padding:0 14dp; line-height:3.75vw;
 text-align:left; font-size:16dp; background-color:#0b1715aa; border-radius:2dp; }
/* Banner PNGs carry their own left fade (tools/fade_boss_banners.py); this overlay only keeps the label readable. */
.card-0 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/diababa.png contain right center); }
.card-1 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/fyrus.png contain right center); }
.card-2 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/morpheel.png contain right center); }
.card-3 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/stallord.png contain right center); }
.card-4 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/blizzeta.png contain right center); }
.card-5 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/armagohma.png contain right center); }
.card-6 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/argorok.png contain right center); }
.card-7 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/zant.png contain right center); }
.card-8 { decorator:linear-gradient(to right,#06100d 0%,#06100d 40%,#06100dcc 60%,#06100d88 78%,#06100d88 100%),image(mod://local.dungeon_rush/res/bosses/ganondorf.png contain right center); }
/* Dungeon page: a wrapping row so tiles and paired buttons can sit side by side. */
body window content pane.dungeon-main { flex:1 1 0; flex-flow:row wrap; align-content:flex-start; gap:10dp;
 animation:.65s cubic-out menu-rise-margin; }
body window .dungeon-main > * { flex:0 0 100%; box-sizing:border-box; }
body window .dungeon-main .hidden { display:none; }
body window .dungeon-main .dungeon-title h1 { text-align:left; margin-top:2dp; }
body window .dungeon-main .dungeon-title .subtitle { text-align:center; margin-bottom:10dp; }
body window button.return-link { position:fixed; top:14dp; left:16dp; width:auto; padding:4dp 8dp; font-size:14dp;
 color:#c1d3c9; background-color:transparent; border-width:0; text-align:left; z-index:6; }
body window button.begin { height:64dp; font-size:22dp; background-color:#2f6b58d0; border:1dp #a0eed6;
 box-shadow:0 0 22dp #7fd9ba44; margin:6dp 0 2dp; }
body window button:not(:disabled).begin:hover { background-color:#3a8069e6; }
.label.section { margin-top:12dp; }
body window button.slot-button { flex:0 0 104dp; width:104dp; height:56dp; padding:6dp 10dp;
 font-size:0dp; color:transparent; }
body window button.slot-button:selected { border-color:#a0eed6; background-color:#284b40bb; }
body window button.gear-tile { flex:0 0 54dp; width:54dp; height:54dp; padding:6dp; font-size:0dp; color:transparent;
 background-color:#091510bb; border:1dp #96b7ab66; border-radius:7dp; }
body window button.gear-tile:selected { border-color:#a0eed6; background-color:#345d4ee0; }
/* The item drawer sits on black so it reads as a different thing from the gear tiles. */
body window button.gear-tile.drawer-tile { background-color:#000000d9; border-color:#5a635f; }
body window button.gear-tile.drawer-tile:selected { border-color:#a0eed6; background-color:#0f1f1ae6; }
/* Same height as the X/Y boxes; the HEARTS text is hidden so the number sits by the heart. */
body window .dungeon-main .hearts-field { flex:0 0 104dp; width:104dp; height:56dp; margin-left:14dp;
 padding:0 12dp 0 62dp; box-sizing:border-box; }
body window .dungeon-main .hearts-field key { display:none; }
body window .dungeon-main .hearts-field value { text-align:left; font-size:22dp; font-family:Noto Mono; }
body window .dungeon-main .ghost-toggle { text-align:left; padding:10dp 16dp; font-size:16dp; }
body window .dungeon-main .ghost-toggle:selected { background-color:#28483dcc; border-color:#a0eed6; }
body window button.ghost-chip { flex:1 1 28%; width:auto; height:auto; padding:7dp 6dp; font-size:14dp; }
body window button.ghost-chip:selected { background-color:#28483dcc; border-color:#a0eed6; }
.music-credit { font-size:11dp; margin:16dp 0 0; color:#b5c9bf; }
.loadout { margin:18dp 0 10dp; }
.items { display:flex; flex-flow:row wrap; gap:7dp; margin-top:12dp; padding-bottom:5dp; }
.item { display:flex; align-items:center; justify-content:center; position:relative;
 width:46dp; height:46dp; background-color:#06100c66; border-radius:7dp; border:1dp #b1d4c344; }
.item:hover { background-color:#28483dcc; border-color:#a0eed6; }
.item-tip { display:none; position:absolute; top:-32dp; left:0dp; z-index:5;
 white-space:nowrap; height:16dp; line-height:16dp; text-align:center;
 background-color:#060e0bf5; color:#f3fff9; border-radius:4dp; padding:6dp 9dp; font-size:11dp; }
.item:hover .item-tip { display:block; }
.stats { display:flex; align-items:center; gap:10dp; margin-top:12dp; color:#dae9e2; font-size:14dp; }
.heart { width:25dp; height:19dp; }
.ability { padding-left:10dp; border-left:1dp #d9fff244; }
.board-header { display:flex; align-items:baseline; justify-content:space-between;
 padding:15dp 0; border-bottom:1dp #d9fff233; font-size:17dp; color:#f3fff9; }
body window .leaderboard button.board-category { width:auto; flex:1 1 20%; padding:7dp 6dp; font-size:14dp; height:auto; }
body window .leaderboard button.board-category:selected { background-color:#28483dcc; border-color:#a0eed6; }
.board-header span:last-child { font-family:Noto Mono; text-align:right; }
body window .discord-profile-image { position:fixed; top:18dp; left:18dp; width:36dp; height:36dp; pointer-events:none; z-index:5;
 border-radius:18dp; overflow:hidden; }
body window .discord-profile-image img { display:block; }
body window button.discord-profile-button { position:fixed; top:12dp; left:12dp; width:48dp; height:48dp; padding:0; color:transparent; background-color:#5865f233; border:1dp #b1dbcc66; border-radius:24dp; z-index:6; }
body window .leaderboard .board-card-label { height:44dp; margin:0 0 -52dp; pointer-events:none; }
.board-card-content { display:flex; align-items:center; height:44dp; padding:0 12dp; gap:12dp; }
.board-card-content img { width:28dp; height:28dp; }
.board-card-content span { white-space:pre; font-family:Noto Mono; font-size:13dp; }
body window .leaderboard button.board-card-button { flex:0 0 100%; height:44dp; margin:0; padding:0; color:transparent; background-color:#06110c22; }
body window .board-card-hidden { display:none; }
body window content pane.leaderboard { overflow-y:auto; }
.empty-board { padding:36dp 0; }
body window content pane.leaderboard { position:relative; height:auto; max-height:100%;
 display:flex; flex-flow:row wrap; align-content:flex-start; gap:8dp; }
body window .leaderboard > div,body window .leaderboard > p { width:100%; flex:0 0 100%; }
body window button.board-tab { position:relative; top:auto; left:auto; width:auto; flex:1 1 0;
 min-width:0; padding:7dp 0; font-size:12dp; height:auto; }
body window button.board-source { flex:1 1 45%; width:auto; padding:7dp 12dp; font-size:14dp; height:auto; }
body window button.board-tab:selected,body window button.board-source:selected { background-color:#28483dcc; border-color:#a0eed6; }
body window .leaderboard .board-title { margin:4dp 0; font-size:22dp; }
body window .leaderboard .board-title h2 { font-size:22dp; margin:4dp 0; }
body window .leaderboard .board-heading { display:flex; align-items:baseline; justify-content:space-between; }
body window .leaderboard .board-count { font-size:14dp; color:#8fa39b; }
body window .leaderboard ui-list { flex:1 1 100%; width:100%; height:32vh; min-height:140dp; max-height:360dp; margin:0; }
body window .leaderboard ui-list-content { padding:0; }
body window .leaderboard button.ui-list-row { font-family:Noto Mono; font-size:13dp;
 white-space:pre; text-align:left; padding:11dp 12dp; }
body window .leaderboard button:hover,body window .leaderboard button:focus-visible { transform:none; }
body window button.board-tool { flex:1 1 28%; width:auto; height:auto; padding:6dp 10dp; font-size:13dp; }
body window .leaderboard.board-local button.board-page { display:none; }
body window .leaderboard .board-status { font-size:12dp; margin:0; color:#c1d3c9; }
body window .leaderboard .music-credit { font-size:10dp; margin:0; }
body window .leaderboard button.back { width:100%; flex:0 0 100%; padding:7dp 10dp; }
body window button.replay-delete { font-size:14dp; padding:7dp 12dp; color:#f0b9a8; border-color:#cf897755; }
.empty-board p,.board-footer { color:#c1d3c9; font-size:14dp; }
@keyframes menu-rise { from { opacity:0; transform:translateY(18dp); } to { opacity:1; transform:translateY(0dp); } }
@keyframes menu-rise-margin { from { opacity:0; margin-top:36dp; } to { opacity:1; margin-top:0dp; } }
@keyframes backdrop-in { from { opacity:0; } to { opacity:1; } }
@keyframes banner-in { 0% { opacity:0; transform:translateX(-16dp); }
 30% { opacity:0; transform:translateX(-16dp); } 100% { opacity:1; transform:translateX(0dp); } }
button.card-0 { animation:.45s cubic-out banner-in; }
button.card-1 { animation:.50s cubic-out banner-in; }
button.card-2 { animation:.55s cubic-out banner-in; }
button.card-3 { animation:.60s cubic-out banner-in; }
button.card-4 { animation:.65s cubic-out banner-in; }
button.card-5 { animation:.70s cubic-out banner-in; }
button.card-6 { animation:.75s cubic-out banner-in; }
button.card-7 { animation:.80s cubic-out banner-in; }
button.card-8 { animation:.85s cubic-out banner-in; }
@media (max-height:760dp) {
 body window content { padding:3% 5%; gap:5%; }
 body window content pane { padding:10dp; gap:7dp; }
 h1 { font-size:36dp; } h2 { font-size:23dp; margin-bottom:14dp; }
 body window button.dungeon-card { padding:0 12dp; font-size:15dp; }
 body window button { padding:9dp 14dp; font-size:16dp; }
 .subtitle { margin-bottom:12dp; } .loadout { margin-top:10dp; }
}
@media (max-width:850dp) {
 body window content { padding:3%; gap:3%; }
 h1 { font-size:30dp; }
 body window content pane.rankings,body window content pane.gallery-summary { padding:14dp; }
 .item { width:42dp; height:42dp; } .board-header { font-size:14dp; }
}
)RCSS";
}
