# Testing

What is proven automatically, what is probed offline under Wine, and what only a logged-in human can
prove. The project rule this file repeats everywhere: **nothing claims to work until it has been seen
working** — and "it compiled" is not "it works".

Build/test commands (all verified 2026-09-05):

| command | proves | result |
|---|---|---|
| `sh gradlew test -q` | the Java suite | BUILD SUCCESSFUL — **332 tests, 0 failures, 30 classes** |
| `sh tools/wine-setup.sh` | jar + `kewlklient.dll` + `KewlKlient.exe` cross-build under Linux/llvm-mingw, assembled into `build/wine-dist/` | zero errors |
| `sh tools/launcher-smoke.sh --no-live` | the launcher's ImGui strip actually renders under Wine, checked by pixel value | "offline strip: OK -- ImGui panel renders under Wine" |
| `sh tools/bridge-roundtrip-probe` (built by the wine-setup tree; run under Wine) | the bridge bytes agree on all three sides | `rt_probe: ALL PASS` |

---

## 1. Automated tests (`sh gradlew test`)

Java sources in `java/`, tests in `java-test/`. Only lombok (build-time code generation, for the
vendored RuneLite files' `@Getter`s), JUnit 4 and Mockito (test-only) are external; nothing external
reaches the shipped jar.

### The migration's own suites

**`kewl.panel.PanelBridgeTest`** (10) — the bridge contract, from Java's side, against the *real*
registry rather than fixtures:

- `snapshotDecodesToExactlyTheRealRegistry` — a full decode with a reader that mirrors the DLL's
  `buildModel`: exact field order, per-setting kinds against a restated kind table, the 8-option
  cap, the trailing-int "packer and docs agree" check.
- `enumOptionsAndCurrentIndexComeThrough`.
- The edit contract: `editsGoThroughSettingSetSoListenersFire` (the whole reason edits cross a
  process boundary instead of a value write), `intEditsAreClampedBySettingNotByTheBridge`,
  `enumEditOutOfRangeIsDroppedNotWrapped`, `unknownPluginOrKeyIsIgnoredNotFatal`,
  `enabledKeyFlipsThePluginNotASetting`.
- `modelRevisionMovesForEditsAndForEnables` / `modelRevisionIsStableWhenNothingChanged` — the
  five-counter revision moves when state moves and does not churn when it does not.
- `debugLinesCoversWhatTheJavaDebugTabShows`.

**`kewl.plugin.PluginManagerTest`** (8) — lifecycle: idempotent enable/disable; a plugin whose
`onEnable` throws is left **disabled** and surfaced; one whose `onDisable` throws still stops; a
recovering plugin loses its failure; registration notifies the listener and refuses duplicates;
unregister disables then removes; reset restores declared values *through `Setting.set`* (so
listeners and persistence fire); `shutdown` stops everything and refuses further transitions.

**`kewl.profile.ProfileManagerTest`** (14) — persistence and isolation: create/switch/rename/
duplicate/delete; profiles isolated from each other; a switch restores settings the new profile is
silent about; a switch that changes nothing fires no hooks; **pins are global, not per profile**
(the documented decision, pinned here as a test); first run migrates to a single default profile; a
corrupt index falls back to a fresh default; a corrupt profile config costs one profile, not the
client; setting changes reach the store through the debounce; the generation counter moves on model
changes but not on plain value writes; the 33rd profile is refused, not created.

**`kewl.profile.SettingCodecTest`** (6) — the persistence encoding: opaque colours round-trip, alpha
colours round-trip, **alpha is stored in the top byte** (ARGB — `#8000ff00` decodes to alpha 0x80,
not alpha 0x00 in the blue channel; this pins the byte order the fixer note describes), stored
strings decode directly, junk stays null (never an exception), and the bool/int/text kinds.

**`kewl.plugin.hub.HubTest`** (12) — the JSON parser (round-trip, escapes/unicode, malformed input
rejected rather than guessed, whole numbers stay whole); manifest validation (a well-formed entry is
accepted; entries missing anything are rejected; artifact URLs are scheme-checked; ids that would
escape their directory are rejected; a manifest that is neither a list nor `{"plugins": [...]}` is an
error state, not a crash); loader refusals (a mainClass that is not a `kewl.Plugin`, one that **is a
built-in resolved from the client**, and one that does not exist are all refused).

**`kewl.HubEndToEndTest`** (2) — the full path with no mocks: the test compiles a plugin, jars it,
serves it over `file:` in a manifest, and installs it — manifest → download → SHA-256 verify →
classloader → registered with the manager → removed. Also: an artifact that fails its checksum is
not installed, and (in the same install round trip) enabled state + settings survive a reinstall —
the profile store re-applies them.

**`net.runelite.client.config.ConfigDefaultsTest`** (3) — the shim's default recovery: a ranged int
default comes from the interface's default method (non-virtually — a `findStatic` here once silently
zeroed every default), an un-ranged int default is not capped by an imagined `@Range`, and
`declaringTheFullConfigSurvivesPanelConstruction` proves all **79** Shortest Path settings can be
declared and rendered without throwing. (The third test reflects into `kewl.ui.Sidebar`, which is
why that dead file is still there — see the note in `PROGRESS.md`.)

### The ported Shortest Path suite (vendored + written here, unchanged by the migration)

`shortestpath` tests (23 classes, 277 tests) cover the ported plugin's own logic:
`PathfinderTest` (78 — routing), `TransportLoaderTest` (45), `PrimitiveIntHashMapTests` (36),
`PrimitiveIntListTests` (21), `WorldPointUtilTest` (17), `TransportTypeConfigTest` (16),
`UtilTest` (9), `TransportRegionOverrideTest` (9), `TransportCountingTest` (7),
`TransportVarPlayerTest`/`TransportVarbitTest` (5 each), `WildernessCheckerTest`,
`LeagueModeStateTest`, `LeagueRegionCheckerTest`, `PathfinderConfigSeasonalTest`,
`VisitedTilesTest` (4 each), `TransportItemsTest`, `LeagueRegionTest`,
`PathfinderResultTest` (2 each), and one each of `SmokeTest`, `TransportDataLintTest`,
`TransportTypeTest`, `TransportVarCheckTest`. Together they are the regression wall under "do not
regress the ported plugin": the collision map, transport parsing, varp/varbit wiring, leagues,
wilderness checks and the pathfinder itself.

### What the suite deliberately does not cover

- No test forces `PanelBridge.snapshot()` to throw mid-walk (the registry is fixed; injecting a
  plugin that breaks mid-walk would mean making `KewlKlient.plugins()` mutable for tests). The
  per-section guards are read-verified, and the null catch-all's DLL handling is what the
  round-trip probe's "empty snapshot is rejected" line pins.
- Nothing here exercises a real GPU, a real game process, or a real network. That is sections 2–3.

## 2. Offline probes under Wine (no game, no login)

All of these run against `build/wine-dist/` produced by `sh tools/wine-setup.sh`.

### The launcher, rendered and pixel-checked

```
sh tools/launcher-smoke.sh --no-live
```

Runs `KewlKlient.exe` under Wine with `KEWL_FAKE_PANEL=1` and `KEWL_DUMP_FRAME=/tmp/kk-smoke-frame.pam`,
then checks **pixel values** in the dumped DIB (never a screenshot — xwd cannot see Wine child GDI
content, which is why the dump path exists): the clear colour, the orange rail accent, a green
toggle-ON. This is the proof that ImGui initialises with the software rasterizer and the strip
parses the real model format.

### The env probes `launcher/main.cpp` honours

| env | what it does |
|---|---|
| `KEWL_FAKE_PANEL=1` | skips all launch machinery (no spawn, no inject, no JVM) and feeds the **real** `readModel` parser a synthetic format-2 region: pins (some set, some not), three profiles (one non-ASCII name), hub entries exercising every button state — installed, hasUpdate, busy, error, installed-with-a-real-index. Edits go nowhere. |
| `KEWL_FAKE_CONFIG=1` | with the above, pushes the first plugin that has settings so the config view is dumpable — a view only a mouse could reach is a view that never gets verified. |
| `KEWL_FAKE_TAB=plugins\|profiles\|hub\|debug` | picks the starting tab so each of the four can be dumped. |
| `KEWL_DUMP_FRAME=<path>` | writes the window DIB as P7 PAM thirty frames in. The PAM bytes are R,G,B,A (the DIB is BGRA and the dump loop reorders) — do not channel-swap on the way out. |
| `KEWL_DUMP_EVERY=<sec>` | keeps dumping `<path>-N` every N seconds, for probes that need to watch a state change. |

`tools/launcher-smoke.sh` (without `--no-live`) additionally starts the **live** launcher window (no
fake data), prints its pid, and prints the manual probe list; it never clicks "+ client" itself.

### The bridge byte-level probe

`tools/bridge-roundtrip-probe.cpp` — the three-sided agreement test that a unit test cannot be:
under Wine, against the real format-2 jar, `PanelBridge.snapshot()`'s `int[]` is decoded by an
independent reader, the same bytes go through the DLL's `buildModel`, and an independent region
decoder written from `launcher/bridge_layout.hpp`'s documented layout re-reads them. Every plugin,
setting, pin, profile and hub entry must agree and the region must be consumed to the last byte —
cold, and warm after profile CRUD + a pin. It also rings every edit kind 0..14 through both struct
definitions (208 bytes each side), dispatches the v2 commands at a real `ProfileManager` over JNI
(create → rename → duplicate + switch → delete, pin 0→1), and runs the **rejection** path: an empty
snapshot, a v1-shaped header, five truncations, a wrong magic, a wrong format and an over-cap plugin
count must all be refused by `buildModel`. Result: `rt_probe: ALL PASS`.

## 3. Manual integration checklist (the human's live steps)

Nothing below can be marked done by an agent: it needs a real `osclient.exe`, a real login, and a
real cursor. Setup:

```
# kewlklient.ini next to build/wine-dist/KewlKlient.exe:
#   [kewl]
#   game=Z:\path\to\osclient.exe        (dll= defaults to kewlklient.dll beside the exe)
sh tools/launcher-smoke.sh             # offline check, then leaves the launcher up
```

The launcher path (ImGui strip):

1. Press **+ client**. Expect: status "osclient.exe started (pid N)", then "embedded. waiting for the
   DLL bridge…", the game's window inside the launcher's, the 286 px strip on the right.
2. Bridge success looks like: the header note changing from "bridge: opening…" to nothing, and the
   plugin list showing the **real** names (Player visuals, Npc visuals, Woodcutter, Shortest Path,
   Test Rlite) — that list can only have come from Java through the bridge.
3. Log in (the user's step; no login is ever attempted by automation).
4. Duplicate injection blocked: press + client again / re-run injection into the same game — the
   launcher refuses, and the DLL's already-loaded path is a no-op.
5. No Swing panel appears; overlays render.
6. Toolbar: open/close each of the four rail tabs; collapse/expand the strip (the game widens the
   same frame; the state survives a restart via `sidebar=` in kewlklient.ini).
7. Plugin list: all five built-ins present, search filters (case-insensitive substring over name,
   description and the live status line — tags are declared metadata but are not carried in the
   bridge model, so they are not searched), pin stars persist, hotkey hints show.
8. Toggle a plugin: the pill flips green at once (optimistic echo), the overlay appears/disappears,
   the status column changes — the enable travelled edit ring → DLL → `PluginManager.setEnabled`.
9. Open a config (gear or label): back arrow returns; sections collapse; bool/int/enum/colour/text
   widgets all work; a changed value survives a panel refresh (a value that snaps back means the
   edit never reached `Setting.set`).
10. Per-setting Reset and per-plugin Reset put declared values back (watch a Shortest Path setting —
    79 of them to pick from).
11. Restart: enabled states and changed settings come back (the profile store's debounced write).
12. Profiles: create (starts as a copy of now), switch (enabled states and settings swap mid-session;
    a plugin the new profile is silent about turns off), rename, duplicate, delete, active profile
    persists.
13. Pin/unpin persists and is **not** changed by a profile switch.
14. Hub: press refresh with no `hub=` configured and expect the honest error ("no hub configured"),
    then set `KEWL_HUB` or `hub=` to a real manifest and refresh; search; install; the installed row
    shows its plugin in the list; update when the manifest's version moves; remove.
15. Installed external plugin survives a restart (restored in the background from installed.json).
16. Shortest Path through the standard plugin system: its sections and settings in the strip, tile
    overlays on the ground, auto-walk walking the path.
17. Debug tab: bridge up, model revision and edit sequence moving.
18. Window: resize, move, maximize/restore, minimize/restore the launcher — the strip stays aligned,
    no flicker, no input leaking through the strip into the game, no persistent focus stealing.
19. Shut down the game: plugins disabled, profile store flushed (shutdown hook), no crash.

The direct-inject fallback (must keep working, unchanged on purpose):

20. Start `osclient.exe` yourself and inject by hand (`wine build/wine-dist/wine_inject.exe
    "Z:<abs path>/kewlklient.dll"` on Linux; on Windows any injector). Expect the **Java2D** panel
    popup beside the game — the same plugins, the same settings, the same profile store, the same
    profiles tab — and overlays as always. Nothing in this path may have changed.

Spec items 30–42 of the original checklist (game overlay rendering, resize/move/maximize/minimize,
DPI across monitors, sidebar alignment, flicker, input leakage, focus, shutdown cleanliness) are the
items above — the mapping is: 31→5, 32–35→18, 36–37→18, 38–40→18, 41–42→19.

## 4. What live testing has already confirmed (Wine, 2026-09-05)

Verified against a real embedded `osclient.exe` (still on the login screen — everything below was
exercised without a login):

- Bridge v2 comes up, the strip shows the real plugin names, and the debug tab reports the model
  revision and edit counters. Edits round-trip end to end: **pin** a plugin (star fills, list
  re-sorts, `profiles/index.json` gains `"pins"`), **enable/disable** one (`profiles/p1-*/config.json`
  gains the `enabled` entry), and the optimistic echo lands within a frame or two.
- Search accepts typed text and filters the list; backspace edits the field.
- Profile and hub tabs render; the pin state survives the model re-publish (it is Java's state, not
  a UI-local one).

Three launcher bugs this shook out, all fixed in `launcher/main.cpp` / `panel_ui.hpp`:

- **keepKeyboard stole focus from the OSRS login box.** While a strip text field was active,
  `keepKeyboard()` re-asserted `SetFocus(g_main)` every frame; clicking into the game never
  deactivates the field (ImGui cannot see game-child clicks), so the login box received no
  keystrokes at all. Fix: when focus is with the game child and the pointer is over the game area,
  the launcher surrenders — `kbSurrenderFlag()` makes `draw()` call `ImGui::ClearActiveID()`
  (hence `imgui_internal.h`) and stops fighting. The strip-vs-game pointer position is what
  disambiguates "the DLL refocused after our own strip click" from "the user went back to the game".
- **A cross-thread `SetFocus` hung the whole launcher.** With `AttachThreadInput`, `SetFocus`
  syncs with the game's input queue, and NXT boot has long stretches that do not pump messages —
  the launcher froze at 0% CPU. Fix: `gamePumps()` — a bounded `SendMessageTimeoutW(WM_NULL, 50ms)`
  probe, cached for 250 ms, gating every `SetFocus` site.
- **NXT recreating its window mid-boot read as "the game closed."** The embedded handle died, the
  launcher tore down, and the DLL fell back to direct-inject. Fix: while the game *process* is
  still alive, re-enter `WaitWindow` and re-embed (the second `injectDll` is a refcount-only
  `LoadLibrary`; `DllMain` does not run again).

Known quirks of the test rig, not the client (relevant when re-running the checks above):

- `KEWL_DUMP_FRAME` PAM dumps are **RGBA**, and a dump is only written when the frame changed — a
  stale dump can make a successful click look like it failed. Always check the dump's mtime against
  the action.
- NXT's login-screen pointer grab redirects **button** events to the game but not keys: "typing
  arrives but the click was eaten" is the grab, and clicking into the game releases it. After a
  pointer warp, dwell ~1s before clicking or Wine's hover state is stale.
- Synthetic XTEST input races the keyboard dual-path dedup (a key can be delivered by both the
  window-message and the polling path — doubled letters). Real keyboards go through the message
  path and are deduped by `g_kbMsgSeen`.

Landed after this section was written, so none of the checks above cover it: the right-click popup
and the world-map plumbing. `kewl/rl/MenuPopup.java` now detects a right-click from the `input()`
snapshot, fires the same `MenuOpened`/`MenuEntryAdded` events RuneLite would, and draws the
plugin-contributed entries itself — including a synthetic "Walk here" that walks to the tile the
right-click landed on. It does not show the game's own menu entries and cannot stop the game
handling the right-click too, because the game menu struct is still unread (`DO_ACTION` is not
derived this build; `Natives.doAction` is a guarded no-op that returns false, and auto-walk reports
"cannot act" instead of pretending to walk). On the map side, the `worldMap` native is derived and
live — `kewl/rl/Events.pushWorldMap` feeds the shim's `WorldMap` the real centre tile every frame
(`centreTile = 8*WM_CENTRE = WM_ORIGIN + 48`, pinned in the decompile and cross-checked at the GE) —
but there is deliberately no zoom: the binary provably has no zoom field, so `WorldMap` holds a
placeholder 4.0f. The map overlays (`PathMapOverlay`, `PathMapTooltipOverlay`) are therefore live
rather than gated off — they render whenever the world-map widget is open and `drawMap` is on — and
are anchored at the correct centre, but their on-map geometry runs at that placeholder zoom; world-map
markers additionally require the map data to be live. None of this has been seen working against a
logged-in client; it belongs in
the human's live pass (section 3).
