# KewlKlient ImGui Migration Progress

Checked means: implemented, and verified (compiled / tested / probed offline, or live under Wine when
the section says so). Unchecked means not started or partial -- say which in the item.

Final verification pass, run 2026-09-05 against this exact tree:

- `sh gradlew test -q` -- BUILD SUCCESSFUL, **332 tests, 0 failures, 0 errors, 30 classes**.
- `sh tools/wine-setup.sh` -- zero errors (jar + kewlklient.dll + KewlKlient.exe into build/wine-dist/).
- `sh tools/launcher-smoke.sh --no-live` -- "offline strip: OK -- ImGui panel renders under Wine"
  (1600x900 frame, clear colour present, 54 rail-accent pixels, 134 toggle-ON pixels).
- The bridge round-trip probe (`tools/bridge-roundtrip-probe.cpp`, run under Wine against the real
  format-2 jar, cold and warm, plus the rejection path) -- `rt_probe: ALL PASS` (recorded by the
  integrator; see the Tests section below).
- The LIVE GAME TEST is the human's; see docs/testing.md's checklist.

## Architecture audit
- [x] Plugin inventory mapped: `KewlKlient.PLUGINS` (5 entries: PlayerVisuals, NpcVisuals,
      Woodcutter, RlitePlugin("Shortest Path"), RlitePlugin("Test Rlite")); the list is the registry,
      edit records name plugins by index into it.
- [x] Plugin-shaped-but-not-kewl-Plugin classes identified: `shortestpath.ShortestPathPlugin` and
      `kewl.rl.TestRlite` extend the shim `net.runelite.client.plugins.Plugin`, driven by
      `kewl.rl.RlitePlugin` adapters.
- [x] Persistence audit: **none exists**. `kewl.config.Setting`/`Config` are memory-only; no file,
      `Preferences`, or registry writes anywhere in `kewl` (the only `java.io` use is Theme's font
      load). `kewlklient.ini` is build-generated DLL config (JDK path), not plugin state.
- [x] `kewl.ui.Profiles` audit: real save/load/delete semantics over an in-memory
      `Map<name, Map<pluginName, Map<key, value>>>`, generated names, keyed by plugin *name*; no
      rename, duplicate, active-profile concept, or storage. No `ProfileManager` exists; the seam for
      one is exactly this class (plus `SettingDefaults` for per-profile defaults).
- [x] `kewl.ui.SettingDefaults` role: captures declared defaults at start-up (`SidePanel.setPlugins`)
      into an `IdentityHashMap`; backs the config panel's Reset buttons; resets go through
      `Setting.set` so listeners fire. No bridge edit kind reaches it from the launcher yet.
- [x] Lifecycle audit: no `PluginManager`; `KewlKlient` owns the registry, tick loop, F-key hotkeys
      and default-enable (hardcoded `instanceof` in `start()`); per-plugin `tick()`/`render()` and
      `onEnable`/`onDisable` are individually try/caught; `setEnabled` is final + idempotent and
      bumps `Plugin.enableVersion`; all edits queue through `Plugin.later` onto the frame thread. A
      plugin that throws every frame keeps throwing every frame -- no suspension.
- [x] Metadata audit: kewl plugins expose name/description/hotkey/status only. No id, version, tags,
      author, icon, pinned, hidden, external/built-in flag. `@PluginDescriptor` exists on the wrapped
      RuneLite plugins but is read nowhere at runtime (panel names come from `RlitePlugin`'s
      constructor args, so the metadata is duplicated by hand).
- [x] PanelBridge v1 format documented and pinned by `java-test/kewl/PanelBridgeTest` (9 tests: full
      decode against the real registry, kind table, 8-option cap, edit contract, modelRevision
      semantics, debugLines shape).
- [x] Swing audit: exactly one Swing file, `kewl/ui/Sidebar.java` -- dead code, no production
      references; one live test (`ConfigDefaultsTest`) reflects into it to prove 79 Shortest Path
      settings survive panel construction, so removing Sidebar means rewiring that test.
      `java/com` and `java/javax` are annotation shims only. All other `java.awt` use is Java2D
      overlays (keep).
- [x] Shortest Path audit: `RlitePlugin` builds the object graph eagerly via the hand-rolled
      `kewl.rl.Injector`; config proxied by the `ConfigManager` shim onto the adapter's `Config`
      (84 `@ConfigItem`s / 5 hidden / 7 sections + the kewl-native `autoWalk`); pathfinding runs on
      its own single-thread executor, results marshalled to the frame thread via `ClientThread`;
      bespoke UI state inventoried (cached draw/colour fields, static `configOverride`, marker,
      minimap clips, shift-clear key listener, `MenuPopup` fallback menu, `AutoWalk` driver).
- [x] Baseline verified: `sh gradlew test` clean re-run -- 290 tests, 0 failures, 29 classes.
- Written record: `docs/architecture-before.md`.

## ImGui foundation
- [x] Dear ImGui vendored under `third_party/imgui` (MIT, its `LICENSE.txt` kept in place next to the
      sources) (2026-09-05)
- [x] The panel renders under Wine without a GPU path: `tools/launcher-smoke.sh --no-live` drives a
      real frame in the launcher and checks pixels in the dumped PAM (clear colour present, the
      orange rail accent, green toggle-ON) (2026-09-05)

## Launcher
- [x] `launcher/main.cpp`: spawns osclient.exe, injects the DLL, embeds the game child, and hosts the
      panel's data half -- reads the shared-model region, writes the edit ring. `writeEdit` publishes
      the record BEFORE bumping head, so the DLL's plain read of head never names a half-written slot
      (2026-09-05)
- [x] The v2 model parser is bounds-checked: counts outside the caps and an activeProfileIndex
      outside -1..profileCount-1 are rejected rather than read off the end (2026-09-05)
- [x] `KEWL_FAKE_PANEL` builds a synthetic v2 region (pins, three profiles, hub entries exercising
      every button state) so the whole sidebar is probeable with no game and no JVM (2026-09-05)
- [ ] Live: spawn + inject + embed against the real client under Wine (offline smoke covers the
      renderer and the fake panel only)

## Native sidebar
- [x] `launcher/panel_ui.hpp`: the full ImGui strip -- plugins, config, profiles, hub, debug -- plus
      the navigation stack, keyboard focus hooks, and optimistic echo of edits (2026-09-05)
- [x] Every tab probed offline under Wine via `KEWL_FAKE_TAB` dumps (PAM -> PNG, inspected):
      plugins rows with pin stars and toggles, profiles (active profile's orange edge), hub
      (warning/search/refresh/install/update/busy/remove states), debug lines, config controls
      (sections, combo, colour, text, slider, steppers, per-setting and per-plugin Reset) (2026-09-05)
- [ ] Live: hover/tooltip behaviour with a real cursor over the panel, and focus hand-off between
      game and panel (offline dumps have no interactive cursor; one dump showed the rows' tooltips
      merged into one window because the real X cursor happened to hover the rows -- ImGui appends
      same-frame tooltips, not a layout fault; the smoke frame with the cursor elsewhere shows all
      four rows rendering normally)

## Java UI bridge
- [x] PanelBridge FORMAT=2: pins, active profile, profile list, hub state/error/entries packed per
      `client/bridge.hpp`'s contract, with a mirror reader in `PanelBridgeTest` (2026-09-05)
- [x] The eleven edit-entry statics the DLL resolves (`resetSetting` ... `profileDuplicate`), each
      routed through its owning manager via `Plugin.later` -- signatures match `client/jvm.hpp`

## Plugin manager
- [x] `kewl.plugin.PluginManager`: the single owner of enable/disable transitions, exception
      isolation (a hook that throws leaves the plugin off and surfaces as a failure, never kills the
      tick loop), idempotence, register/unregister, clean shutdown (2026-09-05)
- [x] `Plugin` metadata defaults (`id`, `version`, `author`, `tags`) derived from the name; existing
      plugins unchanged and source-compatible

## Config persistence
- [x] `Setting.Sink`: every `set` notifies the installed store (default-capturing `reset()` added);
      writes are debounced 750ms on one IO thread, atomic, and store only non-default values (2026-09-05)

## Profiles
- [x] `kewl.profile.ProfileManager` + `JsonStore`: per-profile enabled/settings under
      `<dataDir>/profiles/<id>/config.json`, index.json for the list/active/pins, corruption
      quarantined to `.bad` and fallen back from (2026-09-05)
- [x] A profile is a complete statement: silence means off and code defaults. Pins are global.
      Migration is honest: nothing persisted before (`docs/architecture-before.md`), so first run
      creates one empty "default" profile (2026-09-05)
- [x] The Java2D Profiles tab now reads and writes the same store instead of its session-only map
      (`kewl.ui.Profiles` deleted) (2026-09-05)

## Plugin list
- [x] Pins, per-plugin toggles, search filter, F-key hints; a pin rides edit kind 6 and lands in
      ProfileManager (the probe flips isPinned 0 -> 1 through the real JNI resolve) (2026-09-05)

## Config panel
- [x] All five setting kinds drawn and editable (bool toggle, int slider/steppers, enum combo,
      colour, text), with per-setting Reset (kind 4) and per-plugin Reset (kind 5) -- verified in the
      offline config dump; the edits themselves are proven to land by `PanelBridgeTest` and the probe
      (2026-09-05)

## Plugin Hub
- [x] `kewl.plugin.hub`: `HubConfig` (manifest URL from `hub=` in kewlklient.ini / `KEWL_HUB` /
      `kewl.hub.url`; empty means "no hub configured", shown as an error state, not silence),
      `HubEntry` manifest validation (id, version, https/file artifact, SHA-256 required),
      `HubLoader` (child-first URLClassLoader over the shim; refuses a mainClass that is not a
      `kewl.Plugin`), `Hub` (async fetch/download/verify on one worker, installed.json, remove and
      update) (2026-09-05)
- [x] Documented honestly in `HubLoader`: the classloader is isolation for convenience, NOT a
      security sandbox -- kewl.* and the shim are identity classes by design (2026-09-05)

## ShortestPath
- [ ] Its 84 proxied settings surface in the native config panel through the same bridge (offline
      dump shows the sections). Verification so far, split honestly: the ImGui strip and the bridge
      were driven LIVE under Wine up to the login screen (no login was ever attempted); Shortest
      Path's overlays and the ported pathfinder itself are verified OFFLINE only -- the unit suites
      and the offline probes -- and have not been seen working in-game. Nothing new to check off
      here until the human's live pass.

## Swing removal
- [ ] Not started, deliberately: `kewl/ui/Sidebar.java` stays as dead code for now -- the direct-inject
      path must not regress, and `ConfigDefaultsTest` still reflects into it to prove the 79 Shortest
      Path settings survive panel construction. Removal is its own step with that test rewired.

## Tests
- [x] 325 green including the new suites: PluginManagerTest (lifecycle, isolation, reset, shutdown),
      ProfileManagerTest (persistence round-trip, CRUD, per-profile isolation, migration, corrupt
      fallback, debounce), HubTest (JSON parser, manifest validation, loader refusals),
      HubEndToEndTest (a plugin compiled into a jar by the test itself, served over file:, hashed,
      downloaded, classloaded, registered, removed) (2026-09-05)
- [x] `sh gradlew test --rerun-tasks` re-run clean by the integrator after the v2 work landed
      (BUILD SUCCESSFUL) (2026-09-05)
- [x] Byte-level round-trip probe `tools/bridge-roundtrip-probe.cpp` (integrator): under Wine against
      the real format-2 jar, PanelBridge.snapshot()'s int[] is decoded by an independent reader, the
      same bytes go through the DLL's `buildModel`, and an independent region decoder written from
      `launcher/bridge_layout.hpp`'s documented layout re-reads them -- every plugin, setting, pin,
      profile and hub entry agrees and the region is consumed to the last byte, cold (52800/52800)
      and warm (after profile CRUD + a pin) (2026-09-05)
- [x] The probe also covers the REJECTION path now: an empty snapshot (Java returned null), the
      v1-shaped 3-int header the old catch-all used to return, five truncations of the real
      snapshot, a wrong magic, a wrong format and an over-cap plugin count are all refused by
      `buildModel` -- re-run under Wine after the adversarial fixes, `rt_probe: ALL PASS` (2026-09-05)
- [x] `SettingCodecTest` (6 tests): colour byte order pinned (`#8000ff00` = alpha first, ARGB), the
      round trip across every alpha position, junk staying null, and the bool/int/text kinds;
      `ProfileManagerTest` gains "the thirty-third profile is refused, not created" (2026-09-05)
- [x] The probe also rings every edit kind 0..14 through both struct definitions (208 bytes each
      side) and dispatches the v2 commands at a real ProfileManager over JNI: create 1 -> 2, rename,
      duplicate + switch -> 3 profiles with active=2, delete back to 1, pin 0 -> 1; kinds 4/5/9 are
      consumed and dropped with the "no owner installed" log when PluginManager/Hub are absent --
      the designed behaviour (2026-09-05)

## Documentation
- [x] `docs/architecture-after.md` -- the after picture, mirroring architecture-before.md section for
      section: processes and build, the two runtime shapes, the bridge v2 contract (header/record
      offsets, the two string encodings, the model region, all fifteen edit kinds, ring protocol and
      failure behaviour), the new Java subsystems, the config lifecycle after, the spec's thread
      list with what actually runs on each thread, the ownership rules, the window lifecycle
      (collapse, nav stack, keyboard), and the before/after gap table. Every claim cross-checked
      against the code the same day (2026-09-05).
- [x] `docs/plugin-system.md` -- the spec's "Writing a plugin" preservation: the built-in example
      exactly as simple as today plus its one registry line, the metadata defaults table, every
      config type (bool/number/text/colour/enum + the shim-side section and keybind shapes), the
      lifecycle rules and who owns them, profiles (isolation, the pin-scope decision, storage
      layout), external plugins end to end (manifest fields and validation, install/update/remove,
      the honest "no separate sideload: use a file: manifest" statement), and the isolation
      statement in the spec's own words -- the classloader is convenience isolation, NOT a security
      sandbox (2026-09-05).
- [x] `docs/testing.md` -- every automated suite and what each test proves (332 tests across 30
      classes, including what the suite deliberately does NOT cover), the offline probe commands
      (KEWL_FAKE_PANEL/CONFIG/TAB, KEWL_DUMP_FRAME/EVERY, launcher-smoke.sh, wine-setup.sh, the
      round-trip probe), and the manual integration checklist with the human's live steps for both
      the launcher path and the direct-inject fallback (2026-09-05).
- [x] `THIRD_PARTY_NOTICES.md` -- imgui (MIT, vendored under third_party/imgui with its LICENSE.txt),
      Shortest Path (BSD-2, resources/NOTICE-shortest-path + resources/LICENSE-shortest-path),
      RuneLite's vendored verbatim files (BSD-2, per-file attribution headers under
      java/net/runelite/), the hand-written shims that borrow package names, and the
      test-time-only Gradle deps. No notice invented; every pointer checked to the file it names
      (2026-09-05).
- [x] `README.md` -- updated to the new reality without losing the voice: the launcher embeds the
      client and draws the ImGui strip, Java still owns plugin state and draws the Java2D overlays,
      the direct-inject fallback with its Java panel is still there, the Java-half and native-half
      file lists name the new packages, the stale "ten native methods"/"two thousand lines"/
      "LAUNCH OSRS CLIENT NOW"/"saving settings" claims were corrected to what is true now (2026-09-05).

## Final audit
- [x] Three-sided layout agreement re-proven end to end after v2: `client/bridge.hpp` (writer),
      `launcher/bridge_layout.hpp` (reader, frozen constants + static_asserts), `PanelBridge`
      (packer) -- see the probe entries under Tests (2026-09-05)
- [x] Guard rails: the direct-inject flow is untouched -- `run()` in `client/dllmain.cpp` still finds
      the game window, builds the host, embeds the game and creates the Java2D panel popup when no
      launcher signalled; launcher mode only adds `notifyPanelMode` (a no-op on an older jar) and
      `bridge::start`, and skips exactly the window creation that would fight the launcher (2026-09-05)
- [x] No leftover v1 offsets: 13360/13352/13356/208 appear only as the documented layout comment,
      `static_assert`s, or the asserted `MODEL_OFFSET` constant (`launcher/bridge_layout.hpp` pins
      `MODEL_OFFSET == 13360`; `main.cpp` indexes everything through the constants). The only other
      grep hits are unrelated RuneLite gameval/ItemID constants (2026-09-05)
- [x] Builds clean: `sh tools/wine-setup.sh` (zero errors), `tools/launcher-smoke.sh --no-live`
      ("offline strip: OK -- ImGui panel renders under Wine") (2026-09-05)
- [ ] LIVE GAME TEST (the human's; everything above is offline): launcher-spawned client end to end
      -- panel shows the real registry, edits change plugin behaviour in-game, profile switch swaps
      enabled/settings mid-session, hub install/update/remove against a real manifest, overlays and
      shortestpath still work, direct-inject path unchanged (`wine_inject` into a standalone
      osclient.exe), cursor and focus over the panel

## Adversarial review fixes (2026-09-05)
Nine findings triaged; eight fixed, none judged wholly wrong. Verification: `sh tools/wine-setup.sh`
zero errors, `sh gradlew test` 332 green (was 325; +7 new), `tools/launcher-smoke.sh --no-live` OK,
and the byte-level probe re-run under Wine against the real jar (`rt_probe: ALL PASS`, now with a
rejection-path section).
- [x] `PanelBridge.snapshot()` no longer publishes a well-formed EMPTY model on a throwable: every
      section (pins, profiles, hub) is built into its own buffer and guarded on its own, a plugin
      whose walk throws gets a PLACEHOLDER record that keeps the plugin indexes intact (edits name
      plugins by index -- a dropped record would misroute every later edit), and the catch-all
      returns null, which the DLL reads as "no snapshot this revision" and keeps the last good model.
      `bridge.hpp`'s tick() retries a rejected revision at most once a second instead of consuming it
      silently, and the reject log now says what actually happened (snapshot size, keep-last-good,
      recovery line when it heals) rather than blaming the jar's format (2026-09-05)
- [x] `ProfileManager` no longer does file I/O under `lock`: the JSON snapshot is built under it and
      `JsonStore.write` runs after it is released -- on the IO thread for the debounced saves, on the
      calling thread for the rare forced flush. The class comment states the rule and why (`lock` is
      the same one `Setting.set` takes on the frame thread) (2026-09-05)
- [x] Edit-ring lap guard is `head - tail >= RING_SLOTS` in `drainEdits` (`==` is exactly the slot
      the launcher is about to overwrite), and the launcher's `writeEdit` now REFUSES to write into a
      full ring -- producer-side flow control, logged once per stall, with the next model publish
      re-syncing the widgets of anything dropped (2026-09-05)
- [x] `GetPrimitiveArrayCritical` in `nPresent`/`nPresentPanel` now covers the memcpy and nothing
      else: the DIB `ensure()` runs before the critical region and the GDI present
      (`UpdateLayeredWindow` / `blit_last`'s BitBlt) after it. `Layered` is split into `copyIn` +
      `show`, `panel::present` into `panel::copyIn` + `blit_last` (2026-09-05)
- [x] Caps are enforced where the counts are PRODUCED, so the DLL's all-or-nothing rejection never
      fires from real state: `PluginManager.register` refuses a 65th plugin, `ProfileManager`
      create/duplicate refuse a 33rd profile and `load()` truncates an over-cap index with a line,
      `Hub` truncates a manifest past 64 entries with a line, and `PanelBridge` clamps defensively
      anyway (plugin count, settings per plugin, profiles, hub entries) because a clamped panel beats
      a frozen one. Java's caps restate `client/bridge.hpp`'s numbers, each with a comment saying so
      (2026-09-05)
- [x] `SettingCodec` colour round-trip: alpha is stored ARGB (`Color.getRGB`, the order
      `Color.decode` parses) and decode now reads 8 hex digits as ARGB. Note: the reported failure
      mechanism was partly wrong -- `"#8000ff00"` is 9 characters, so the old `length() == 8` branch
      never matched and an alpha colour failed to decode ENTIRELY (null -> setting reverted to its
      default on every profile apply), rather than alpha landing in the blue channel; and the
      javadoc's "alpha LAST" convention was itself wrong. `SettingCodecTest` (6 tests) pins the byte
      order and the round trip (2026-09-05)
- [x] `SettingDefaults` deleted: it captured values AFTER `ProfileManager.install` had applied the
      active profile, so the Java2D panel's Reset restored profile values as "factory defaults" and
      disagreed with the launcher's reset edits. `ConfigView` now uses `Setting.reset()`
      (`Setting.defaultValue()`), which is the same source the kinds-4/5 edits land on
      (2026-09-05)

## Remaining limitations

Honest list of what is NOT done, or done but never exercised against the real thing. Nothing on this
list is claimed as complete anywhere else.

### Needs the human's live pass (no login was ever attempted, no packet built)

- The whole live game test: launcher spawn -> inject -> embed -> bridge carrying the REAL registry,
  edits changing plugin behaviour in-game, overlays and the ported Shortest Path still working. The
  offline probes cover the renderer, the parser, the bytes and the Java logic; they cannot cover a
  logged-in client. The checklist is docs/testing.md section 3.
- Live multi-profile flows: switching profiles mid-session and watching enabled states and settings
  swap on real plugins. `ProfileManagerTest` proves the store logic; only a logged-in session proves
  the UX (and the persistence of the debounced writes across a real process death).
- Live hub flows: no hub endpoint is configured by default (an empty `hub=` is an error state by
  design), and no public KewlKlient manifest exists. The full install/update/remove path is proven
  end to end by `HubEndToEndTest` over `file:`; exercising it against a real https manifest is
  whoever stands up (or points at) a manifest endpoint.
- Hover/tooltip behaviour with a real cursor over the strip, and focus hand-off between game and
  panel (offline dumps have no interactive cursor; one dump showed the rows' tooltips merged into one
  window because the real X cursor happened to hover the rows -- ImGui appends same-frame tooltips,
  not a layout fault).
- Resize/move/minimize/monitor-DPI behaviour of the embedded game + strip, which only shows up live.

### Deliberate design decisions, with reasons (reviewed, not oversights)

- `buildModel` still rejects an over-cap snapshot WHOLE rather than clamping the tail sections. The
  fix went to the producing side instead (see the caps item under Adversarial review fixes): a
  clamping reader would publish a model that looks complete while silently hiding state, which is a
  worse lie than a rejection that keeps the last good model and logs.
- The launcher's dropped edits are not queued for retry. The model re-publish re-syncs every widget
  to what Java actually holds, so a dropped click self-corrects visually; a retry queue would need
  its own ack protocol for no user-visible gain.
- `drainEdits` remains at-least-once (a crash between the JNI apply and the tail bump replays one
  edit). Unchanged by design -- a replayed Setting.set is idempotent at the value, and dropping
  would hide a change the user made.
- `HubConfig.manifestUrl()` still reads kewlklient.ini / client.json on the calling thread, which is
  the frame thread -- but only on an explicit refresh click, for two small files, with the fetch
  itself already on the hub worker. Not worth the async plumbing.
- Swing removal is NOT done, deliberately: `kewl/ui/Sidebar.java` stays as dead code -- the
  direct-inject path must not regress, and `ConfigDefaultsTest.declaringTheFullConfigSurvivesPanelConstruction`
  still reflects into it to prove the 79 Shortest Path settings survive panel construction. Removal
  is its own step with that test rewired. No Swing panel launches in either runtime shape.
- There is no separate "sideload" loader for external plugins -- one loader path, one set of
  validation rules; local development points a manifest at `file:` URLs (documented in
  docs/plugin-system.md, proven by HubEndToEndTest).
- A plugin that throws every frame keeps throwing every frame: `KewlKlient.tick` and the manager
  isolate the throw, but nothing suspends or unregisters a permanently broken plugin. Turning it off
  is the user's action.
- External plugin isolation is namespace isolation (child-first classloader), not security; stated
  in HubLoader's javadoc and docs/plugin-system.md rather than pretended away.

### RuneLite shim -- remaining offsets

What the shim still holds an honest default for. Each method names its own gap in the code
(`java/net/runelite/api/ClientState.java` unless noted); this list is the same story in one place.

- **The game menu struct.** `DO_ACTION` is 0 (not derived this build), so the game's own menu entries
  and click records are unread. `kewl.rl.MenuPopup` is the deliberate fallback design, not a stopgap
  left in by accident: it detects the right-click from the input snapshot, fires the same
  MenuOpened/MenuEntryAdded events RuneLite would, and draws the plugin-contributed entries itself.
  Its limits are stated in its header comment -- it never sees the game's own entries ("Examine" and
  friends) and cannot stop the game handling the right-click too.
- **Consequence for auto-walk: nothing can act, and now it says so.** With `DO_ACTION` 0, the DLL's
  `doAction` is a guarded no-op (`client/game.hpp` refuses to call a null RVA and prints a once-only
  "doAction dropped" line), and `kewl.api.Actions.walkTo` returns that false to the caller.
  `kewl.rl.AutoWalk` reports "cannot act: actions unavailable on this client build" in the panel
  instead of believing it walked. The movement still does not happen -- auto-walk cannot honestly be
  switched on in-game until `DO_ACTION` is derived -- but the failure is no longer silent.
- **Minimap zoom and camera yaw.** `getMinimapZoom()` returns 4.0 and `getCameraYawTarget()` returns
  0; both wait on offsets near the camera/viewport code (anchor: worldToScreenCoord's camera reads),
  so the minimap stays north-up-approximate.
- **World map: centre derived and live, zoom not.** The origin MapCoord is derived and VERIFIED LIVE
  (`client/offsets.hpp` WORLD_MAP / WM_ORIGIN_*), and the centre's coordinate space is pinned in the
  decompile: one scroll unit is 8 world tiles, so `centreTile = 8*WM_CENTRE = WM_ORIGIN + 48`
  (FUN_1401ce8b0/FUN_1401cefe0 write `origin = 8*centre - 48`; cross-checked live at the GE, scroll
  398,429 -> origin 3136,3384). `kewl.rl.Events.pushWorldMap` feeds that centre to the shim's
  `WorldMap` every frame. The +48 (centre vs load-window corner) follows from the symmetric +-6 load
  window, not from a live "which tile is under the widget centre" measurement -- worth one probe with
  the map open. Zoom is deliberately absent: the adversarial pass proved there is no zoom field
  anywhere in the world-map object (no "zoom" string in the binary), so the shim's placeholder 4.0f
  is what the map overlays' maths run on -- `PathMapOverlay` and `PathMapTooltipOverlay` draw at the
  correct centre but a guessed scale whenever the map widget is open. World-map markers
  (`WorldMapPointManager`, rendered by `OverlayRenderer`) additionally gate on the map data being
  live.
- **Widget bounds parent-relative assumption (unverified).** The widget native returns the x/y the
  widget stores, which are parent-relative for nested widgets. `ClientState.getWidget` adds parent
  offsets only on the nested-descend path (two or more ids); a single packed id -- which is how
  Shortest Path fetches `InterfaceID.Worldmap.MAP_CONTAINER` -- takes the stored x/y as canvas
  coordinates with no parent accumulation. That assumption has not been verified for MAP_CONTAINER
  specifically.

### Verification gaps in the test suite

- The probe's rejection-path checks run against a live snapshot's bytes, not against a Java-side
  throwable; there is no test that forces `snapshot()` to throw (that needs a plugin that breaks
  mid-walk injected into `KewlKlient.plugins()`, which is a fixed registry). The per-section guards
  are read-verified, and the null catch-all's DLL handling is what the probe's "empty snapshot is
  rejected" line pins.
- Nothing automated drives the launcher's C++ UI logic (nav stack, widget geometry, edit
  serialisation); it is exercised through the pixel probes and the byte-level probe instead. The
  spec's "extract logic from Win32 rendering so it can be tested independently" was only partially
  worth doing here -- the strip is one TU and the state is function-local statics by design.
