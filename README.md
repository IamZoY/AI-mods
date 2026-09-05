# KewlKlient

A small, readable OSRS client with a plugin panel, entity visuals, and a worked example bot.

It exists to be **learned from and hacked on**. The core — the API, the plugin model, the config
system, the drawing — is still the few thousand lines of `java/kewl/` you can hold in your head; the
bulk of the repo is the vendored Shortest Path plugin and the RuneLite API shim it runs on, which you
never have to read. There is no auto-updater and no account manager. The plugin hub is a manifest
endpoint you point it at, not a store of ours. What there is:

- **Everything is Java.** Plugins, settings, profiles, and all the drawing. No C++ toolchain needed
  to write a bot or an overlay.
- **Overlays are plain Java2D.** Your plugin gets a `Graphics2D` over the game window and draws whatever
  it likes — shapes, alpha, antialiased text, images. Nothing had to expose a "draw box" primitive.
- **Settings build their own UI.** Declare a setting; the control panel grows the right widget for it.
  No plugin writes a line of UI code.
- **Your state survives a restart.** Enabled states, settings, pins and profiles are persisted under
  `~/.kewlklient` — plugins declare nothing, do nothing, and get it for free.
- **No network code at all.** We never build a packet. We call the game's own "do this menu action"
  function and let it build and send the packet. That is the single biggest reason this codebase is
  small, and the reason it survives most game updates. (The one exception is the optional plugin
  hub, which fetches a manifest you point it at — see
  [docs/plugin-system.md](docs/plugin-system.md).)
- **Twenty-one native methods.** That is the entire unsafe surface, all in one file.

```
       launcher (ImGui)               injected into the game
    ┌──────────────┐                ┌────────────────────────────────┐
    │  "+ client"  │ ── spawn ───>  │  osclient.exe                  │
    │              │ ── inject ──>  │  └─ kewlklient.dll             │
    │              │                │     reads memory               │
    │  286px ImGui │                │     starts a JVM ──────────────┼──> kewlklient.jar
    │  panel strip │  <== shared == │     21 natives ────────────────┼──>   api + your plugins
    │  (CPU-raster)│     memory     │  <── one finished image/frame ─┼──    overlays (Java2D)
    └──────────────┘                └────────────────────────────────┘      plugin state (Java)
```

**Java draws, C++ shows.** Java renders the whole overlay into an image and hands it back once a frame;
C++ puts it on screen and otherwise stays out of the way. That inversion is why a plugin can draw
anything Java2D can draw, and it costs one memcpy a frame.

**Java owns the state, C++ shows the panel.** The ImGui strip is a view: it renders a snapshot of the
plugin model that Java publishes into shared memory, and every click comes back as an edit record that
Java applies through the same paths its own panel uses. There is no second copy of plugin state in
C++.

---

## Quick start

**Windows only.** The whole thing is Win32 — there is no Linux or Mac build and there is not going to be
one. (On a Linux desktop you can still run the game itself under Wine and test the whole stack there;
[`tools/wine.md`](tools/wine.md) is the recipe.)

You need three things installed. The first two you probably have; the third is the one people miss:

| | why | get it |
|---|---|---|
| **JDK 17+** | the plugins are Java | IntelliJ ships one, or [adoptium.net](https://adoptium.net) |
| **CMake** | the injected part is C++ | [cmake.org/download](https://cmake.org/download/) — tick *Add CMake to the system PATH* |
| **A C++ compiler** | same | [Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/) → *Desktop development with C++*. The full IDE is not needed. |

You do **not** need Gradle — the wrapper in this repo fetches it.

### From IntelliJ

1. **File → Open** and pick the folder you cloned. It imports as a Gradle project.
2. Pick the **KewlKlient** run configuration (it is checked into the repo) and press **Run**.
3. In the ImGui launcher that appeared, press **+ client**. It starts `osclient.exe`, injects the DLL
   and puts the game inside the launcher's own window.
4. **Log in.**

That is it — no config file to edit. The build points `kewlklient.ini` at whichever JDK IntelliJ is
using, and puts everything in `build\dist\`.

### From a terminal

```bat
gradlew run
```

Same thing. `gradlew dist` builds without launching, and `build.bat` is a wrapper around it for people
who prefer a double-click.

A **panel strip** opens down the right-hand side of the game with a switch, a pin and settings for
every plugin — plus tabs for profiles, the plugin hub and bridge debug info. The same plugins
have hotkeys:

| key | does |
|-----|------|
| F1 | player visuals on/off |
| F2 | NPC visuals on/off |
| F5 | the woodcutter on/off |
| F3, F4, F6–F8 | free, for your plugins |

If nothing appears, see [Troubleshooting](#troubleshooting).

---

## Writing a plugin

Extend `Plugin`, override what you need, add one line to the list. This one draws a marker on the
nearest cow and counts them:

```java
package kewl.plugins;

import java.awt.Color;
import java.awt.Graphics2D;
import java.awt.Point;

import kewl.Plugin;
import kewl.api.Entity;
import kewl.api.Npcs;
import kewl.ui.Hud;

public final class CowSpotter extends Plugin {

    public CowSpotter() {
        config.number("range", "Range", "How far to look", 10, 1, 30);
        config.colour("colour", "Colour", "", new Color(255, 120, 200));
    }

    @Override public String name()        { return "Cow spotter"; }
    @Override public String description() { return "Marks the nearest cow."; }
    @Override public int    hotkey()      { return 5; }          // F6

    @Override
    public void render(Graphics2D g) {
        Entity cow = Npcs.nearestWithin(config.number("range"), 2805);
        if (cow == null) return;
        Point at = cow.screen();
        if (at != null) Hud.entityBox(g, at, 16, 30, config.colour("colour"));
    }
}
```

Add it in `java/kewl/KewlKlient.java`:

```java
private static final List<Plugin> PLUGINS = new ArrayList<>(List.of(
        new kewl.plugins.PlayerVisuals(),
        new kewl.plugins.NpcVisuals(),
        new kewl.plugins.Woodcutter(),
        new kewl.plugins.CowSpotter()          // <- yours
));
```

Rebuild, restart the client. Your plugin is in the panel with a range slider and a colour picker you
never wrote, and its state is saved with everyone else's. **That list is the entire plugin system** —
no scanning, no annotations, no manifest, nothing that can silently fail to find your class.

**Read [`Woodcutter.java`](java/kewl/plugins/Woodcutter.java) next.** It is the worked example and does
all four things at once: settings, a decision loop that acts on the game, a world overlay, and a
statistics panel.

Every setting type, the profile model, and how to ship a plugin through the hub instead of the list
are in [`docs/plugin-system.md`](docs/plugin-system.md).

### The two methods

| | for | rules |
|---|---|---|
| `tick()` | deciding and acting | runs every frame while enabled |
| `render(Graphics2D)` | drawing, and nothing else | runs after every plugin has ticked |

Keeping them apart means you can turn the drawing off without changing behaviour.

**Neither may block.** They run about thirty times a second on the overlay thread; a `Thread.sleep` in
either freezes the overlay. For "every few seconds", keep a `long lastX` field and compare
`System.currentTimeMillis()` — `Woodcutter` shows the pattern, and also shows why you want a timer
*and* an animation check rather than either alone.

### What a plugin can see and do

```java
// kewl.api.Game -- the world, as of the start of this frame
Game.ready()                          Game.me()
Game.npcs()   Game.players()          Game.entities()
Game.toScene(worldX, worldY)          // world -> scene, null if not loaded

// projection, through the game's own camera maths
Game.projectWorld(x, y)               // -> Point, or null when off screen
Game.tileOutlineWorld(x, y)           // -> Polygon lying flat on the ground

// kewl.api.Npcs / Players -- finding things
Npcs.nearest(1278)                    Npcs.nearestWithin(10, 2805)
Npcs.withId(ids...)                   Players.nearest()

// an Entity
e.id()  e.uid()  e.worldX()  e.worldY()  e.distance()
e.animation()  e.isIdle()  e.orientation()  e.screen()

// you
Game.me().worldX()   .isIdle()   .health()   .healthPercent()   .runEnergy()

// stats
Skills.level(Skill.WOODCUTTING)   Skills.experience(...)   Skills.boost(...)

// kewl.api.Actions -- doing things, in WORLD coordinates
Actions.walkTo(x, y)
Actions.object(treeId, x, y)          // chop / mine / open
Actions.npc(entity)                   // first option
Actions.npc(entity, 2)                // second option
```

**Two coordinate systems, and mixing them up is the most common bug here.** World coordinates are what
your minimap shows (Lumbridge ≈ 3222, 3218). Scene coordinates are 0–104 within the loaded chunk, and
they are what the game's click function actually wants. Everything a plugin touches is in **world**
coordinates; the conversion happens in one place.

### Drawing

`render()` hands you a `Graphics2D` over the whole game window, already antialiased, with (0,0) at the
top-left of the game's client area. Anything Java2D can do works. `kewl.ui.Hud` has the three things
every overlay ends up wanting:

```java
Hud.entityBox(g, point, 16, 30, colour);          // a box standing on a point
Hud.tile(g, Game.tileOutlineWorld(x, y), colour); // a tile highlight lying on the ground
Hud.text(g, "hello", x, y, colour);               // text with an outline, readable on any background

Hud.panel(g, 12, 12, "My plugin", new Hud.Lines() // a statistics panel that sizes itself
        .add("state", "running")
        .add("xp/hr", 41234));
```

Tile outlines go through the game's own projection corner by corner, so they sit on the ground with the
right perspective and stay correct while the camera turns.

---

## How it works

### The launcher
`launcher/main.cpp` — an ImGui window with a "+ client" button. Pressing it spawns `osclient.exe`
(the path comes from `[kewl] game=` in `kewlklient.ini`), injects `kewlklient.dll` into it with
`CreateRemoteThread` + `LoadLibraryW` (the oldest, most boring injection there is), and reparents the
game's window into the launcher's as a child. From then on one window holds the game on the left and
a 286px Dear ImGui panel strip on the right, drawn by the launcher process with the CPU
(`client/imgui_sw.hpp`) — the game owns the only OpenGL context, so the panel never touches a GPU.

The panel's data lives in the game process (Java owns the plugin model, the profiles and the hub), so
it crosses a shared-memory bridge: Java packs a snapshot (`kewl.panel.PanelBridge`), the DLL repacks
it into the model region (`client/bridge.hpp`), the launcher reads and renders it
(`launcher/bridge_layout.hpp`). Clicks travel back as small edit records in a ring in the same region,
and the DLL applies them by calling Java — through `Setting.set` and the owning managers, never
around them. The static_asserts in both C++ files make the sides fail a build rather than disagree
about where byte 40 is, and `tools/bridge-roundtrip-probe.cpp` checks the whole round trip byte for
byte against the real jar.

Running `osclient.exe` yourself and injecting the DLL by hand skips all of this: the DLL detects
that it was not launcher-spawned and builds its own host window and Java2D panel exactly as it always
did (`tools/wine_inject.exe` does the injecting on Linux). Same plugins, same settings, same profiles
— a different thing drawing the panel.

### The native half
- `client/offsets.hpp` — every game-specific number, in one file, each with a note on how to re-find it.
- `client/game.hpp` — guarded memory reads, entity enumeration, projection, and `doAction`.
- `client/overlay.hpp` — the transparent window, and the one function that puts pixels in it.
- `client/jvm.hpp` — starts the JVM and registers the natives. The whole unsafe surface.
- `client/bridge.hpp` — the DLL's half of the shared-memory panel bridge (model out, edits in).
- `client/dllmain.cpp` — finds the game window, detects launcher mode, runs the 30 fps loop.
- `client/imgui_sw.hpp` — the software rasterizer the launcher draws ImGui with (no GPU: the game owns
  OpenGL).
- `launcher/main.cpp` + `launcher/panel_ui.hpp` — the launcher window, the "+ client" spawn/inject/
  embed, and the ImGui strip itself (plugins, config, profiles, hub, debug, collapse, search).

### The Java half
- `java/kewl/Natives.java` — the natives, declared. You will not call these directly.
- `java/kewl/api/` — the world with names on it: `Game`, `Entity`, `Local`, `Npcs`, `Players`, `Skills`,
  `Actions`.
- `java/kewl/Plugin.java` — the base class, and `java/kewl/plugins/` — the plugins.
- `java/kewl/config/` — settings that build their own controls.
- `java/kewl/plugin/` — `PluginManager` (the one place a plugin is switched on or off) and
  `plugin/hub/` — the external plugin hub (manifest, download, verify, classload, remove).
- `java/kewl/profile/` — profiles and persistence: per-profile enabled states and settings, debounced
  atomic writes under `~/.kewlklient`.
- `java/kewl/panel/PanelBridge.java` — the packed panel model Java publishes and the edits it accepts.
- `java/kewl/ui/` — `Theme` (all the colours), `Hud` (drawing helpers), and the Java2D panel views the
  direct-inject path draws (`SidePanel`, `PluginListView`, `ConfigView`, `ProfilesView`, `DebugView`).

More on all of this: [`docs/architecture-after.md`](docs/architecture-after.md) is the architecture
map, [`docs/plugin-system.md`](docs/plugin-system.md) covers plugins, config, profiles and the hub,
and [`docs/testing.md`](docs/testing.md) is what is tested and how to verify the rest by hand.

The overlay is a **transparent always-on-top window**, not a renderer hook. Hooking would need a detour
library, a graphics API to get right, and it crashes inside someone else's render loop when you get it
wrong. A layered window is a page of code you can read in one sitting. The cost: it will not show up in
screenshots or recordings, and it can flicker. That trade is on purpose.

**Java does the drawing.** C++ used to draw the boxes and it was the wrong split: every new visual meant
a new primitive exposed across the JNI boundary. Now Java renders the frame into an image and C++ shows
it, so a plugin has the whole of Java2D and C++ has one job. It costs one memcpy of a full-screen image
per frame — about 8 MB at 1080p, well under a millisecond, and we were already blitting those pixels.

### Why there is no packet code
Bot clients usually reimplement the game's network protocol — a big table of opcodes and byte layouts
that changes every update and fails silently when it drifts.

KewlKlient does not. `doAction` is the client's own menu-action entry point: the same function that runs
when you right-click a tree and choose "Chop down". We call it with the same arguments and the client
builds and sends the packet itself. Consequences:

- We never need to know the wire format.
- Anti-cheat sees a normally-constructed packet, because it *is* one.
- One function to re-find after an update instead of a hundred opcodes.

### RuneLite plugins

There is a second way to write a plugin: port one. `java/net/runelite/` is a **shim, not RuneLite** —
our own implementation of the `net.runelite.*` API that RuneLite plugins are written against. A plugin
ported to `java/shortestpath/` (Shortest Path, from the plugin hub) reads the game through that shim and
runs as an ordinary kewl plugin on the overlay thread; see `java/net/runelite/README.md` and
`resources/NOTICE-shortest-path` for what is vendored, what is shimming, and what waits on a new offset.

The honest state of it: everything derivable from what kewl already reads works (pathfinding, tile
overlays, config, events); the parts of the API that need memory offsets kewl does not have yet —
varps, item containers, widgets, the world map, the menu — return honest defaults through
`net.runelite.api.ClientState`, and each method there names the offset it is waiting for. Porting more
hub plugins mostly means deriving those offsets once; the shim is shared.

Auto-walk is the one place kewl extends a ported plugin rather than just hosting it: upstream Shortest
Path never moves for you, the `Auto-walk` toggle in its panel does, using the same `doAction` walk as
every other plugin here.

---

## When the game updates

Jagex rebuilds the client roughly weekly. Two kinds of number in `offsets.hpp` rot at different speeds:

- **Function RVAs** (`DO_ACTION`, `WORLD_TO_SCREEN`) — assume these are wrong after any patch.
- **Struct offsets** (`ENTITY_SCENE_X`, `SCENE`…) — stabler, but they do move. One of them shifted by
  `0x10` between builds a few weeks apart.

`BUILD_ID` is the fingerprint. If it does not match, **do not just bump it** — every other number was
measured on that build.

### Re-deriving offsets

```powershell
.\tools\ghidra_headless.ps1 -Ghidra "C:\ghidra_11.4" -Script FindOffsets.java
type tools\offsets_found.txt
```

First run analyses 16 MB and takes 10–20 minutes; it is cached after that.

**The method: anchor on a name the client uses for itself, then walk to what you want.** The client ships
a Lua binding layer that registers its own functions by name, and those names are plain text in the
binary — `worldToScreenCoord`, `npcCoord`, `playerCoord`. Find the string, find its references, and you
are inside the registration code for the function you are after. `FindOffsets.java` does the finding;
you open the referencing function and read off the pointer it registers.

**Do not scan for byte patterns.** A byte pattern is a guess about instructions the compiler may
rearrange. When it breaks it does not error — it gives you an address that decompiles into something
plausible and wrong.

Prefer IDA if you own it (better decompiler on this binary): `.\tools\ida_headless.ps1`. It works on a
copy and deletes the stale database when the exe changes, because IDA will happily answer from last
month's build without telling you.

There is also a **Claude skill** in `.claude/skills/deob/` that carries this whole method — if you use
Claude Code, ask it to find an offset and it will follow it.

### Finding a new action

`Game.OPLOC1 = 3` is "first option on a scenery object". Other actions have other numbers. To find one,
attach a debugger to `DO_ACTION`, do the action by hand in game, and read the opcode argument. That is
how every number in this repo was found — none of them were guessed.

---

## Good first contributions

Genuinely useful, roughly easiest first:

1. **Find the nearest tree automatically.** The woodcutter needs a typed-in tile because nothing here can
   enumerate scenery — that means walking the scene's object grid, about six more offsets. Land this and
   every gathering plugin gets shorter, and the woodcutter loses its most awkward setting.
   *(the highest-value one on this list, by a distance)*
2. **Names.** NPC and player names live behind a pointer chain the client's own `npcName` binding walks.
   Everything currently shows an id where it wants to show a name.
3. **Inventory reading.** Nearly every bot needs "am I full yet".
4. **Ground items.** Pairs with the above to make a looter possible.
5. ~~**Saving settings.**~~ Done — settings, enabled states, pins and profiles persist under
   `~/.kewlklient` (see `kewl/profile/ProfileManager.java`). Kept on the list so nobody redoes it:
   the open problem there now is *syncing* a profile between machines, which is a file format
   question, not an offsets one.
6. **A minimap overlay.** All the pieces exist; it just needs the minimap's own projection.

Please keep the three rules this codebase is built on: **no packet building**, **every offset gets a
comment saying how it was found**, and **nothing claims to work until it has been seen working**.

---

## Troubleshooting

**Nothing happens when I press the button.**
Is the game actually running and logged in? The launcher looks for `osclient.exe` by name. If it says the
game refused the DLL, you built 32-bit — the game is x64 and rejects a 32-bit DLL silently.

**The overlay says "java: could not load jvm.dll".**
`java=` in `kewlklient.ini` is wrong. It needs a folder with `bin\server\jvm.dll` under it. A JDK always
has that; some JREs do not.

**The build says "CMake is not installed (or not on your PATH)".**
Install it from the table above. If you just installed it, **restart IntelliJ** — it caches the `PATH`
it was started with, so a new install is invisible to it until then.

**The build says "No CMAKE_CXX_COMPILER could be found".**
No C++ compiler. Install the Build Tools from the table above.

**The overlay says "kewl/KewlKlient not found".**
`kewlklient.jar` must sit next to `kewlklient.dll`. `gradlew dist` puts all four files in `build\dist\`
and now checks they are actually there before claiming success — it used to be possible for the build
to pass while producing no DLL at all.

**Boxes are in the wrong place, or the client crashes on inject.**
The game updated and the offsets moved. See [When the game updates](#when-the-game-updates).

**Boxes flicker.**
Expected — it is a layered window, not a renderer hook. See "The native half".

**The panel strip says "bridge: opening…" forever, or shows nothing.**
The DLL could not create the shared-memory mapping, or the jar beside the DLL predates the bridge.
The game's stdout prints `[bridge]` lines saying which; the debug tab in the strip shows the same
state. A jar without `kewl.panel.PanelBridge` gets an empty panel rather than a hung one.

**My settings did not come back after a restart.**
They live in `~/.kewlklient/profiles/` — a profile is a complete statement, so only what differs from
a plugin's declared defaults is written, and a plugin that was never switched on under the active
profile starts off. A corrupt file is quarantined as `.bad` and fallen back from; look for
`[profile]` lines in the game's stdout.

---

## Licence

**GPL-3.0.** See [`LICENSE`](LICENSE). In plain terms:

- **Anyone can use it**, for anything, including commercially.
- **It has to stay open.** If you distribute a modified version — or anything built on it — you have to
  ship the source under the GPL too. You cannot take this closed.
- **Credit stays with it.** Keep the copyright notices and say what you changed.

If you fork it, a link back here is appreciated on top of what the licence requires.

Copyright (C) 2026 StoneShorts and the KewlKlient contributors.

---

This is a personal-use tool for your own account. Automating a game breaks its rules and can get that
account banned. That is your call to make, and yours to live with.
