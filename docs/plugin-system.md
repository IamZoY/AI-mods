# The plugin system

How plugins are written, registered, configured, persisted, and how external ones arrive through the
hub. The design rule this document exists to protect: **the simplest plugin stays simple**. Nothing
below asks a plugin author to write UI, annotations, manifests, or lifecycle boilerplate — the one
place the outside world is allowed to be complicated is the hub's manifest, and that is somebody
else's file, not your plugin's.

---

## 1. A built-in plugin

Extend `kewl.Plugin`, override what you need, add one line to the list. This one draws a marker on
the nearest cow and counts them:

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

The registry is a list, and the list **is** the registry — in `java/kewl/KewlKlient.java`:

```java
private static final List<Plugin> PLUGINS = new ArrayList<>(List.of(
        new kewl.plugins.PlayerVisuals(),
        new kewl.plugins.NpcVisuals(),
        new kewl.plugins.Woodcutter(),
        new kewl.plugins.CowSpotter()          // <- yours
));
```

Rebuild, restart. Your plugin is in both panels (the ImGui strip and the direct-inject Java2D one)
with a slider and a colour picker you never wrote. No scanning, no annotation processor, no
descriptor file — nothing that can silently fail to find your class.

The list's **order is load-bearing**: panel edit records name plugins by index into it
(`KewlKlient.plugins().get(i)`), so it is never re-sorted at runtime, and hub-installed plugins are
appended, never spliced in.

### The two methods, and the rules

| | for | rules |
|---|---|---|
| `tick()` | deciding and acting | runs every frame while enabled |
| `render(Graphics2D)` | drawing, and nothing else | runs after every plugin has ticked |

**Neither may block.** They run about thirty times a second on the overlay/frame thread; a
`Thread.sleep` in either freezes the overlay, and a network call in either is a ban-worthy stall.
For "every few seconds", keep a `long lastX` field and compare `System.currentTimeMillis()` —
`Woodcutter` shows the pattern. Expensive work belongs on your own executor with results handed back
to the frame thread; `shortestpath` is the worked example (its pathfinder runs on
`shortest-path-%d`, results arrive via `ClientThread.invokeLater`).

`onEnable()`/`onDisable()` reset and tear down state. They run on the same thread as everything
else, are individually caught (a plugin whose hook throws is left **disabled** and its failure is
surfaced, not swallowed — `kewl.plugin.PluginManager` owns that rule), and are idempotent: asking
for a state the plugin is already in does nothing.

### Optional metadata

`name()` is the only thing you must write. Everything else defaults, so a plugin that does not care
stays short:

| method | default | what it is for |
|---|---|---|
| `description()` | `""` | the line under the name |
| `id()` | derived from `name()` (lower-case, non-alphanumerics → `-`) | the key profiles and pins store this plugin's state under. Override only when two plugins would derive the same id or the name is something a file path should not carry — it must be stable across sessions |
| `version()` | `""` | built-ins version with the client |
| `author()` | `""` | display only |
| `tags()` | empty | extra search words for the plugin list. Declared today, consumed by none: the bridge model does not carry tags, so the launcher's search matches name, description and the live status line. Declaring tags is harmless and forward-compatible; do not rely on them being searched yet |
| `hotkey()` | `-1` | 0..7 = F1..F8; duplicates are allowed and both toggle |
| `status()` | `""` | a short line for the panel's status column |

## 2. Config: every setting type

Declare in the constructor; the panel builds the widget. Reading uses the same key
(`config.number("range")`), and an unknown key returns a harmless default rather than throwing — a
typo shows up as a plugin that does nothing, not an exception thirty times a second.

| declare | widget | stores | notes |
|---|---|---|---|
| `config.bool(key, label, description, def)` | toggle | `true`/`false` | |
| `config.number(key, label, description, def, min, max)` | slider + steppers | int, **clamped** to min..max on every set, so a slider cannot wedge | |
| `config.text(key, label, description, def)` | text field | `String` | |
| `config.colour(key, label, description, def)` | colour swatch | `java.awt.Color` | persisted as `#rrggbb`, or `#aarrggbb` when the colour has any alpha (standard ARGB — the order `Color.getRGB()` and `Color.decode` both speak) |
| `config.enumeration(key, label, description, enumConstant)` | drop-down | the enum constant | options are the enum's constants in declaration order; the *selected option* is stored, not an index, so a plugin that reorders or extends its enum keeps every stored value that still names one |

Two more shapes exist, both arriving through the RuneLite config shim rather than the kewl-native
API: a **section** (a grouping header — `RlConfigMeta` recovers `@ConfigSection`s for the bridge so
both panels draw the grouping) and a **keybind** (an INT setting stored as an F-index, 0 = not set,
reported as kind 3 in the bridge model so the panel draws it as a hotkey). Both are documented from
the porting side in [`../java/net/runelite/README.md`](../java/net/runelite/README.md).

Semantics worth knowing:

- The only write path is `Setting.set(Object)` — it bumps the global revision the launcher's model
  refresh is built on, clamps numbers, fires change listeners, and notifies the persistence sink.
  Reset buttons go through `Setting.reset()`, which is `set(defaultValue)`, so a reset fires exactly
  what a user edit fires.
- Settings persist only when they **differ from their declared default**. A setting a plugin gains in
  an update starts on its new default instead of a stale copy.
- Keys are per plugin; the reserved key `"enabled"` is the plugin's on/off switch and is not a
  Setting (the bridge routes it to `PluginManager.setEnabled`).

## 3. Lifecycle and who owns it

`kewl.plugin.PluginManager` is the only thing that transitions a plugin. Four callers arrive there —
the F-key hotkey loop, the Java2D panel, the ImGui launcher's edit ring, and profile switches — and
none of them call `onEnable` themselves. The manager enforces:

- **Idempotence** — no second `onEnable`, no spurious persistence write.
- **Exception isolation** — a hook that throws leaves the plugin off and records the failure; the
  tick loop survives. Per-frame `tick()`/`render()` throws are caught per plugin in
  `KewlKlient.tick` (one broken plugin skips its frame), but nothing suspends a plugin that throws
  every frame — turn it off.
- **One thread** — transitions outside start-up are queued via `Plugin.later` and run at the top of
  the next frame.
- **The registry cap** (64, the panel bridge's limit) and duplicate-id refusal.

## 4. Profiles

Stored under the client's data directory (`KewlKlient.dataDir()` — `kewl.data.dir` overrides,
otherwise `~/.kewlklient`):

```
<dataDir>/profiles/index.json        the profile list, the active id, and the pins
<dataDir>/profiles/<id>/config.json  that profile's enabled map and non-default settings
```

- Ids are stable (`p1-<random>`); names are display-only and de-duplicated ("x", "x 2", ...), so
  renaming a profile moves nothing.
- **A profile is a complete statement**: silence means the plugin is off and its settings are at
  their declared defaults. That is what makes switching actually isolate profiles — a profile that
  never mentions `range` puts `range` back where the code declared it. It is self-consistent because
  every real change is captured the moment it happens.
- Switch persists the state being left (`flush()`), then re-states the new profile onto the live
  plugins; a plugin whose state does not change gets no second `onEnable`/`onDisable`.
- Create starts as a **copy of how the world is right now**, not blank — switching to a blank profile
  would silently reset every plugin, which reads as data loss. Duplicate copies stored state too.
- The last profile cannot be deleted; deleting the active one falls back to the first remaining.
- Writes are atomic (tmp + rename) and debounced 750 ms on one IO thread; a corrupt file is
  quarantined to `.bad` and fallen back from rather than losing the whole index.
- Migration is honest: nothing was persisted before this feature existed, so first run creates one
  empty "default" profile. Nothing was lost, because nothing was ever written down.
- **Pin scope (the decision): pins are global, not per profile.** A pin is a UI fact about the user
  ("these are the ones I want at the top of the list"), not a fact about a way of playing; nobody
  wants their pinned plugins to change because they switched from skilling to PvM. Pins live in
  index.json next to the profile list. If per-profile pins are ever wanted, `ProfileManager`'s class
  comment and the pins block in `indexJson()` are the two places that change.

## 5. External plugins and the hub

An external plugin is not a second plugin API. It is a jar containing a class that **extends
`kewl.Plugin`** (directly — a wrapper adapter works too, which is how the ported RuneLite plugins
run), loaded into the same client, registered with the same manager, configured by the same config
model, persisted under the same profiles.

### The manifest

`hub=` in `kewlklient.ini` (`[kewl]` or `[kewlklient]` section) — or the `KEWL_HUB` environment
variable, or the `kewl.hub.url` system property, or `"hub"` in `<dataDir>/client.json` — points at a
manifest. It is either a JSON list of plugin objects or `{"plugins": [...]}`. Each entry:

```json
{
  "id": "example-plugin",
  "name": "Example Plugin",
  "version": "1.0.0",
  "author": "Author",
  "description": "Example",
  "mainClass": "example.ExamplePlugin",
  "artifact": "https://example.com/jars/example-plugin-1.0.0.jar",
  "sha256": "64 lowercase or uppercase hex characters"
}
```

Validation (`kewl.plugin.hub.HubEntry`) is exactly what the loader depends on:

- `id` — 1..64 chars of `[A-Za-z0-9._-]`, starting with an alphanumeric. It becomes a directory name,
  which is why path-shaped ids are rejected outright.
- `name`, `version`, `mainClass` — non-empty. `mainClass` is checked again after loading, when it has
  to actually extend `kewl.Plugin`.
- `artifact` — an `https://` URL. A `file:` URL is also accepted: that is how a local hub is
  developed and how the test suite builds one. Anything else (`ftp:`, `jar:`, ...) is rejected.
- `sha256` — **required**, 64 hex characters. A hub without checksums is a hub asking the user to
  trust every mirror in between, and the download path refuses an artifact it cannot check.
- `author`/`description` — optional, display-only. Extra fields in the manifest are ignored, so a
  hub can grow without breaking old clients.

A malformed row is dropped with a log line naming the field; it does not empty the tab. A manifest
with no valid rows is an error state, not silence. The first 64 valid entries are shown (the panel
bridge's cap).

### Install, update, remove

1. The jar is downloaded to `<dataDir>/hub-tmp/` (anything claiming to be larger than 64 MB is
   refused), hashed while streaming, and the hash is compared to the manifest's. A mismatch deletes
   the download and lands in the hub's error state.
2. Only then does `<dataDir>/external/<id>/<version>.jar` appear (atomic move), so a failed
   checksum never leaves an empty directory pretending something was installed.
3. `HubLoader` opens a classloader of its own for the jar, loads `mainClass`, refuses anything that
   is not a `kewl.Plugin` (or that resolved from the client rather than the jar), and instantiates it.
4. The plugin is registered with the `PluginManager` — queued onto the frame thread, because the
   registry is frame-thread state — and the active profile immediately applies whatever state it has
   stored for that id. That is how an installed plugin comes back enabled with its settings.
5. **Update** is the same path: the tab shows "update available" when the manifest's version differs
   from the installed one, and installing replaces the jar and swaps the plugin.
6. **Remove** unregisters the plugin, closes its classloader after it is out of the registry, and
   deletes the directory. Its saved profile state is kept — never silently deleted because a plugin
   is temporarily unavailable.
7. At start-up, `<dataDir>/hub/installed.json` is replayed: each jar that survived is reloaded in
   the background. A plugin that fails to load after an update **stays installed on disk, disabled**,
   with a log line — the user keeps their config and their "remove" button.

### Sideload for development

There is no separate sideload mechanism, on purpose — one loader path, one set of validation rules.
To run your own jar locally, point `hub=` (or `KEWL_HUB`) at a local manifest that uses `file:` URLs:

```
KEWL_HUB=/home/me/hub/manifest.json
```

```json
[{
  "id": "dev-plugin", "name": "Dev plugin", "version": "0.1",
  "mainClass": "dev.DevPlugin",
  "artifact": "file:///home/me/hub/dev-plugin-0.1.jar",
  "sha256": "<sha256sum of the jar>"
}]
```

Refresh the hub tab, press install. `HubEndToEndTest` does exactly this end to end — jar built by
the test itself, served over `file:`, hashed, downloaded, classloaded, registered, removed — if you
want a template. For iteration on a **built-in** plugin you do not need any of this: it is on the
list or it does not exist.

## 6. Isolation: what is real, and what is not

Each external plugin loads in its own child-first `URLClassLoader`. What that buys:

- A plugin can bundle its own copy of a library without fighting the client's version, and two
  plugins can disagree about a library without either winning.
- Child-first stops at a short list of parent-first prefixes — `java.*`/`javax.*`/`jdk.*`/`sun.*`,
  `kewl.*`, and the shim (`net.runelite.*`, `org.slf4j.*`) — because those are **identity**, not
  bytecode: the client hands a plugin live objects, and a second class named `kewl.Plugin` would make
  the first cast a `ClassCastException` nobody could act on.
- Closing the loader unloads the plugin (that is the remove path).

**What it does not buy: security.** The classloader is isolation for *convenience*, not a sandbox,
and nothing in this codebase should be described as one. A `Plugin` runs with every permission this
process has: it can read and write files, open sockets, load natives and call the game's memory.
Installing one from a hub is running someone else's code inside your client, and the hub tab says so
in those words. The defences that do exist are validation (manifest shape, checksum, the
`extends kewl.Plugin` check) and failure containment (one plugin's exceptions never kill the tick
loop, the hub, or the other plugins) — not confinement.
