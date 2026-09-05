// jvm.hpp -- start a Java VM inside the game and hand it the few things it needs.
//
// This is the bit that makes KewlKlient worth forking: plugins are Java, not C++. You edit a .java file,
// run one build, restart the client, and your plugin is live. Nobody needs a C++ toolchain to write a
// bot, or to draw an overlay.
//
// The split is deliberately lopsided:
//
//   C++  does the unsafe things -- reading a live process, calling into the game, putting pixels on the
//        screen. That is this file, and it is the entire unsafe surface.
//   Java does everything else   -- plugin logic, state machines, config, and ALL the drawing.
//
// Every native below is a place that can crash the game, so the bar for adding one is: could the Java
// side do this with what it already has? Nine is more than the four we started with, and each of the
// extra five earned its place by removing a whole category of thing C++ would otherwise have to know
// about -- what an NPC is, what a skill is, what a box looks like.
#pragma once
#include <windows.h>
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "game.hpp"
#include "overlay.hpp"
#include "panel.hpp"

namespace kk {

inline JavaVM*   g_vm     = nullptr;
inline jclass    g_api    = nullptr;   // kewl.KewlKlient  -- lifecycle
inline jclass    g_nat    = nullptr;   // kewl.Natives     -- where the natives are registered
inline jmethodID g_tick   = nullptr;
inline jmethodID g_status = nullptr;
// Mouse events from the panel window, delivered up to Java. Optional: an older jar without
// panelMouse still runs, it just never hears about clicks on the panel.
inline jmethodID g_panelMouse = nullptr;

// The panel bridge (launcher mode), resolved lazily by bridgeResolve(). Everything here is optional:
// a jar from before the launcher has no kewl.panel.PanelBridge at all, and launcher mode then runs
// with an empty panel model and no edits rather than a dead game. Each method is guarded on its own,
// so a half-matched jar degrades exactly as far as it has to.
inline jclass    g_bridgeCls      = nullptr;   // kewl.panel.PanelBridge
inline bool      g_bridgeTried    = false;
inline jmethodID g_bridgeRevision = nullptr;   // static long modelRevision()
inline jmethodID g_bridgeSnapshot = nullptr;   // static int[] snapshot()
inline jmethodID g_bridgeSetBool  = nullptr;   // static void setBool(int, String, boolean)
inline jmethodID g_bridgeSetInt   = nullptr;   // static void setInt(int, String, int)
inline jmethodID g_bridgeSetEnum  = nullptr;   // static void setEnum(int, String, int)
inline jmethodID g_bridgeSetText  = nullptr;   // static void setText(int, String, String)

// The format-2 edits (bridge.hpp EditKind 4..14). These are COMMANDS -- reset, pin, profile and hub
// operations -- so each lands on its own PanelBridge static rather than on the four set* methods
// above, and a jar built before format 2 has none of them. They are resolved by bridgeResolveV2 the
// first time a v2 edit arrives, not with the rest of the bridge: a session that never touches pin,
// profiles or the hub never pays for eleven GetStaticMethodID calls, and an old jar is only
// "missing methods" from the moment it is asked to do something it cannot.
inline bool      g_bridgeV2Tried         = false;
inline jmethodID g_bridgeResetSetting    = nullptr;   // static void resetSetting(int, String)
inline jmethodID g_bridgeResetPlugin     = nullptr;   // static void resetPlugin(int)
inline jmethodID g_bridgeSetPinned       = nullptr;   // static void setPinned(int, boolean)
inline jmethodID g_bridgeHubInstall      = nullptr;   // static void hubInstall(String)
inline jmethodID g_bridgeHubRemove       = nullptr;   // static void hubRemove(String)
inline jmethodID g_bridgeHubRefresh      = nullptr;   // static void hubRefresh()
inline jmethodID g_bridgeProfileSwitch   = nullptr;   // static void profileSwitch(int)
inline jmethodID g_bridgeProfileCreate   = nullptr;   // static void profileCreate(String)
inline jmethodID g_bridgeProfileDelete   = nullptr;   // static void profileDelete(int)
inline jmethodID g_bridgeProfileRename   = nullptr;   // static void profileRename(int, String)
inline jmethodID g_bridgeProfileDuplicate = nullptr;  // static void profileDuplicate(int)
// Bit per edit kind that has been logged as "no Java method for this" -- one line PER KIND, not one
// per edit (the ring can carry a burst) and not one line total (with kinds 4..14 in play, a single
// flag meant the first dropped kind permanently silenced the ones behind it: a launcher sending pins
// plus profile edits against an old jar would show only whichever arrived first). Kind 15+ shares
// bit 31 -- anything that far off contract is garbage, and one line for the family is enough.
inline std::uint32_t g_bridgeKindsLogged = 0;

/// Set by dllmain: the game's own window, so we can report its size to Java.
inline HWND g_gameWindow = nullptr;

/// The window Java's pixels and coordinates are measured against -- see overlay.hpp g_canvasWindow.
inline HWND canvasWindow() {
    if (kk::g_canvasWindow && IsWindow(kk::g_canvasWindow)) return kk::g_canvasWindow;
    return g_gameWindow;
}

// ---------------------------------------------------------------------------------------------------
// The natives. These are the ONLY things Java can do to the game.
// ---------------------------------------------------------------------------------------------------

/// True once the game has built its client object -- i.e. you are actually in-game.
inline jboolean JNICALL nReady(JNIEnv*, jclass) {
    return clientObj() ? JNI_TRUE : JNI_FALSE;
}

/// Every visible entity, flattened, seven ints each:
///     uid, sceneX, sceneY, isPlayer, typeId, animation, orientation
///
/// `typeId` is the NPC's type id; for players it is always -1 on this build -- PLAYER_COMBAT_LEVEL
/// (offsets.hpp) is wrong here and reading it shipped pointer-fragment garbage as combat levels -- so
/// the slot is -1 for players until that offset is re-derived.
///
/// One array rather than one object per entity on purpose -- this is called thirty times a second, and
/// allocating a few hundred short-lived objects a frame is exactly the kind of thing that turns into a
/// stutter you then spend an evening profiling. Java unpacks it into records once.
inline jintArray JNICALL nEntities(JNIEnv* env, jclass) {
    std::vector<jint> flat;
    flat.reserve(256 * 7);
    forEachEntity([&](const Entity& e) {
        bool player = isPlayerUid(e.uid);
        flat.push_back(e.uid);
        flat.push_back(e.sceneX);
        flat.push_back(e.sceneY);
        flat.push_back(player ? 1 : 0);
        // -1 for players: combatLevel() is gated off because PLAYER_COMBAT_LEVEL is wrong on this
        // build (see offsets.hpp and game.hpp).
        flat.push_back(player ? combatLevel(e.addr) : npcTypeId(e.addr));
        flat.push_back(e.animation);
        flat.push_back(e.orientation);
    });
    jintArray arr = env->NewIntArray(static_cast<jsize>(flat.size()));
    if (arr && !flat.empty()) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(flat.size()), flat.data());
    return arr;
}

/// int[] {worldX, worldY} of the scene's south-west corner, or empty if the world is not loaded.
inline jintArray JNICALL nSceneBase(JNIEnv* env, jclass) {
    Tile b = sceneBase();
    jint v[2] = { b.x, b.y };
    jintArray arr = env->NewIntArray(b.ok ? 2 : 0);
    if (arr && b.ok) env->SetIntArrayRegion(arr, 0, 2, v);
    return arr;
}

/// You: {uid, sceneX, sceneY, plane, animation, orientation, runEnergy, cycle}. Empty before you spawn.
inline jintArray JNICALL nLocal(JNIEnv* env, jclass) {
    bool found = false;
    Entity me = localPlayer(found);
    if (!found) return env->NewIntArray(0);
    jint v[8] = { me.uid, me.sceneX, me.sceneY, me.plane,
                  me.animation, me.orientation, runEnergy(), cycle() };
    jintArray arr = env->NewIntArray(8);
    if (arr) env->SetIntArrayRegion(arr, 0, 8, v);
    return arr;
}

/// One varp (client config variable) by id. 0 before the game has the array up.
///
/// A pass-through read rather than a snapshot array: the shim asks for individual ids a handful of
/// times per frame (transport gates in the pathfinder), and copying a few thousand ints per frame to
/// maybe read three of them is allocation for its own sake.
inline jint JNICALL nVarp(JNIEnv*, jclass, jint id) {
    return varp(id);
}

/// An item container's current shape: {slot0Id, slot0Qty, slot1Id, slot1Qty, ...}, or empty when the
/// container does not exist right now (bank closed, before login). One snapshot call per
/// ItemContainerChanged-sized read rather than one JNI hop per slot.
inline jintArray JNICALL nContainer(JNIEnv* env, jclass, jint containerId) {
    jintArray arr = nullptr;
    int size = containerSize(containerId);
    if (size <= 0 || size > 4096) return env->NewIntArray(0);

    std::vector<jint> flat;
    flat.reserve(static_cast<std::size_t>(size) * 2);
    for (int i = 0; i < size; ++i) {
        flat.push_back(containerItem(containerId, i));
        flat.push_back(containerQty(containerId, i));
    }
    arr = env->NewIntArray(static_cast<jsize>(flat.size()));
    if (arr && !flat.empty()) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(flat.size()), flat.data());
    return arr;
}

/// All 25 skills at once: effective[25], base[25], xp[25], in that order. One call rather than 75.
inline jintArray JNICALL nSkills(JNIEnv* env, jclass) {
    if (!clientObj()) return env->NewIntArray(0);
    jint v[SKILL_COUNT * 3];
    for (int i = 0; i < SKILL_COUNT; ++i) {
        v[i]                     = skillEffective(i);
        v[SKILL_COUNT + i]       = skillBase(i);
        v[SKILL_COUNT * 2 + i]   = skillXp(i);
    }
    jintArray arr = env->NewIntArray(SKILL_COUNT * 3);
    if (arr) env->SetIntArrayRegion(arr, 0, SKILL_COUNT * 3, v);
    return arr;
}

/// Project a fine-coordinate point to the screen. Returns the two screen coordinates packed into a
/// long, or Long.MIN_VALUE when the point is not on screen.
///
/// Packed into a long rather than returned in an array because a plugin drawing tile outlines calls
/// this four times per tile, and an allocation per call would dominate the cost of the whole overlay.
inline jlong JNICALL nProject(JNIEnv*, jclass, jint fineX, jint fineHeight, jint fineY) {
    float sx = 0.f, sy = 0.f;
    if (!projectFine(fineX, fineHeight, fineY, sx, sy)) return static_cast<jlong>(0x8000000000000000ULL);

    // The leaf has no "is it visible" answer -- it writes two floats, always. A point BEHIND the
    // camera comes back mirrored rather than rejected (the perspective division flips sign), and
    // projectFine's ±10000 clamp is far wider than the canvas, so entities the camera never faced
    // drew markers anyway, scattered over the window: the "boxes everywhere but not on the npcs"
    // report. The honest screen test is the canvas itself -- the overlay is exactly the canvas
    // window's client rect -- so anything outside it (64px margin covers a box + label hanging half
    // off the edge) is not visible and must not draw. Needs canvasWindow() and the CAMERA_*/VIEW_*
    // offsets (offsets.hpp, both NOT VERIFIED in-game).
    HWND cw = canvasWindow();
    int cwW = 0, cwH = 0;
    if (cw && IsWindow(cw)) {
        RECT r{};
        GetClientRect(cw, &r);
        cwW = r.right - r.left;
        cwH = r.bottom - r.top;
    }
    if (cwW <= 0 || cwH <= 0) return static_cast<jlong>(0x8000000000000000ULL);
    if (sx < -64.f || sy < -64.f || sx > cwW + 64.f || sy > cwH + 64.f)
        return static_cast<jlong>(0x8000000000000000ULL);

    // Off unless KEWL_LOG is set -- the same gate the launcher's input diagnostics print behind
    // (dllmain.cpp redirects stdout to the file it names). Unconditional here meant one line per
    // second in every session, which is noise nobody asked for. Once enabled, it MEASUREs the space
    // instead of arguing about it: project your own tile and print it next to the canvas size, the
    // camera ints the leaf subtracts, and the divide/multiply pair of its final rescale (offsets.hpp
    // CAMERA_FINE_*, VIEW_*). Standing still, that point must land on your own character. When it
    // does not, this one line says which space is wrong and by how much: cam values near
    // sceneBase<<7 mean the camera is world-based and our scene-fine input is not (fix: project
    // world fine coords); numerator != denominator means the leaf returns a scaled space, not canvas
    // pixels (fix: the ratio is the correction). This is diagnosis for an offset that has never been
    // verified in-game -- delete the probe once the boxes sit on the NPCs.
    static int probe = 0;
    static bool probeEnabled = ::getenv("KEWL_LOG") != nullptr;
    if (probeEnabled && (probe++ % 30) == 0) {
        bool found = false;
        Entity me = localPlayer(found);
        float mx = 0.f, my = 0.f;
        if (found && projectFine(me.sceneX << 7, 0, me.sceneY << 7, mx, my)) {
            int camX = 0, camH = 0, camY = 0, bw = 0, bh = 0, vw = 0, vh = 0;
            std::uintptr_t c = clientObj();
            if (c) {
                camX = rd<std::int32_t>(c + off::CAMERA_FINE_X);
                camH = rd<std::int32_t>(c + off::CAMERA_FINE_H);
                camY = rd<std::int32_t>(c + off::CAMERA_FINE_Y);
                std::uintptr_t v10 = rdp(c + off::VIEW_OBJ) + off::VIEW_OBJ_SCALE_BASE;
                bw = rd<std::int32_t>(v10 + off::VIEW_BASE_W);
                bh = rd<std::int32_t>(v10 + off::VIEW_BASE_H);
                vw = rd<std::int32_t>(v10 + off::VIEW_CANVAS_W);
                vh = rd<std::int32_t>(v10 + off::VIEW_CANVAS_H);
            }
            std::printf("[proj] you@scene(%d,%d) -> (%.1f,%.1f) canvas=%dx%d cam=(%d,%d,%d) view=(%d/%d, %d/%d)\n",
                        me.sceneX, me.sceneY, mx, my, cwW, cwH, camX, camH, camY, vw, bw, vh, bh);
            std::fflush(stdout);
        }
    }

    jlong x = static_cast<jlong>(static_cast<jint>(sx));
    jlong y = static_cast<jlong>(static_cast<jint>(sy));
    return (x << 32) | (y & 0xFFFFFFFFLL);
}

/// Perform a menu action. SCENE coordinates. See game.hpp for why this is the only way we act.
///
/// Returns false when the action was NOT issued: DO_ACTION is 0 for this build (the address was never
/// derived -- calling a guessed address would crash the game) or the client object is not up yet. The
/// game-side call is then a silent no-op, so this boolean is the caller's only signal that nothing
/// happened; plugins must not report success on it.
inline jboolean JNICALL nDoAction(JNIEnv*, jclass, jint sx, jint sy, jint opcode, jint targetId) {
    return doAction(sx, sy, opcode, targetId) ? JNI_TRUE : JNI_FALSE;
}

/// Interact with an NPC by uid, looking its tile up for you. Returns false when the uid did not
/// resolve (it despawned this frame) or the action was dropped -- see nDoAction.
inline jboolean JNICALL nInteractNpc(JNIEnv*, jclass, jint uid, jint opcode) {
    return interactNpc(uid, opcode) ? JNI_TRUE : JNI_FALSE;
}

/// The game's client area on screen: {x, y, width, height}. Java needs the size to make its image and
/// the position to park the control panel beside the game.
inline jintArray JNICALL nViewport(JNIEnv* env, jclass) {
    jintArray arr = env->NewIntArray(4);
    HWND w = canvasWindow();
    if (!arr || !w || !IsWindow(w)) return arr;
    RECT r{};
    GetClientRect(w, &r);
    POINT tl{ r.left, r.top };
    ClientToScreen(w, &tl);
    jint v[4] = { tl.x, tl.y, r.right - r.left, r.bottom - r.top };
    env->SetIntArrayRegion(arr, 0, 4, v);
    return arr;
}

/// Put a finished frame on the screen. `px` is w*h premultiplied ARGB pixels, top row first.
///
/// GetPrimitiveArrayCritical rather than GetIntArrayElements: the former hands back a pointer to the
/// array's real storage instead of copying eight megabytes we are about to copy again. The window
/// between the two calls must contain nothing that could block or allocate, which is why the only
/// thing in it is the memcpy into a DIB that ALREADY exists -- ensure() (CreateDIBSection on a resize)
/// and the GDI present (UpdateLayeredWindow, an X11 round trip under Wine that can take milliseconds)
/// both happen after ReleasePrimitiveArrayCritical. Holding the critical region across them would
/// stall every Java thread, the frame thread mid-tick included, for the length of a GDI call.
inline void JNICALL nPresent(JNIEnv* env, jclass, jintArray px, jint w, jint h) {
    if (!px || w <= 0 || h <= 0) return;
    if (env->GetArrayLength(px) < w * h) return;          // never trust a length we did not compute
    if (!g_overlay.ensure(w, h)) return;                  // may allocate a new DIB: outside the critical

    void* raw = env->GetPrimitiveArrayCritical(px, nullptr);
    if (!raw) return;
    std::memcpy(g_overlay.pixels, raw, static_cast<std::size_t>(w) * h * 4);
    env->ReleasePrimitiveArrayCritical(px, raw, JNI_ABORT);   // ABORT: we did not modify it

    g_overlay.show();
}

/// The control panel's frame: same pixels-in contract as nPresent, but it lands on the panel window,
/// pinned to the game's right edge. See panel.hpp for why the panel is a second window.
inline void JNICALL nPresentPanel(JNIEnv* env, jclass, jintArray px, jint w, jint h) {
    if (!px || w <= 0 || h <= 0) return;
    if (env->GetArrayLength(px) < w * h) return;

    // Same split as nPresent: memcpy inside the critical region, GDI after it. The window check is
    // the one panel::copyIn would have done -- no panel (launcher mode) means no DIB to copy into.
    if (!panel::g_panel.hwnd || !IsWindow(panel::g_panel.hwnd)) return;
    if (!panel::g_panel.ensure(w, h)) return;

    void* raw = env->GetPrimitiveArrayCritical(px, nullptr);
    if (!raw) return;
    std::memcpy(panel::g_panel.pixels, raw, static_cast<std::size_t>(w) * h * 4);
    env->ReleasePrimitiveArrayCritical(px, raw, JNI_ABORT);

    panel::blit_last();
}

/// Mouse, modifier keys and keyboard edges, all read from Windows rather than game memory:
///     {mouseX, mouseY, shift, ctrl, alt, leftButton, rightButton, middleButton, vk1, vk2, ...}
///
/// mouseX/mouseY are client-area coordinates of the game window (Java's canvas space), so this needs
/// no offset at all -- the same window handle nViewport already uses. Held down = 1. GetAsyncKeyState's
/// short is signed; mask it rather than comparing, since the high bit is the "pressed since last call"
/// bit and comparing the raw value breaks the moment the process has seen any other key.
///
/// The trailing entries are KEY EDGES: the virtual-key codes that went from up to down since the
/// previous call, so Java can dispatch real KeyEvents for plugin hotkeys. The game's own key handling
/// is untouched -- this is a global poll, the same one every bit of Windows software does. Two honest
/// costs: keys typed into the game's chat also show up here (Java filters nothing, hotkeys can fire
/// while you type -- matching RuneLite without its focus widget is a later problem), and the scan is
/// per-frame global state, so a press is seen by whichever frame runs next, ~33ms later at worst.
inline jintArray JNICALL nInput(JNIEnv* env, jclass) {
    jint v[8 + 16];
    int n = 8;
    v[0] = v[1] = v[2] = v[3] = v[4] = v[5] = v[6] = v[7] = 0;
    // Measured against the same window the projection answers in (canvasWindow): mouse coordinates
    // and marker coordinates must share one space, or the popup draws next to the cursor.
    if (canvasWindow() && IsWindow(canvasWindow())) {
        POINT p{};
        if (GetCursorPos(&p)) {
            ScreenToClient(canvasWindow(), &p);
            v[0] = p.x;
            v[1] = p.y;
        }
    }
    v[2] = (GetAsyncKeyState(VK_SHIFT) & 0x8000)   ? 1 : 0;
    v[3] = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
    v[4] = (GetAsyncKeyState(VK_MENU) & 0x8000)    ? 1 : 0;
    v[5] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
    v[6] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1 : 0;
    v[7] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 1 : 0;

    // Edge detection against last frame. 0x08-0xFF are the keys (the buttons are the fixed fields
    // above); 0xFF is unassigned. Static, not per-window: keyboard state is global in Windows anyway.
    // prevDown updates for EVERY key regardless of buffer space -- if it stopped early, a key still
    // held next frame would look like a fresh edge and re-fire.
    static bool prevDown[256] = {};
    for (int vk = 0x08; vk < 0xFF; ++vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down && !prevDown[vk] && n < 8 + 16) v[n++] = vk;
        prevDown[vk] = down;
    }

    jintArray arr = env->NewIntArray(n);
    if (arr) env->SetIntArrayRegion(arr, 0, n, v);
    return arr;
}

/// The client's own state machine -- 10 title, 20 logging in, 25 loading, 30 logged in. This is the
/// field the game itself branches on (offsets.hpp GAME_STATE), so the shim's GameStateChanged events
/// fire on real transitions instead of a synthesised login sequence. 0 before the client object exists.
inline jint JNICALL nGameState(JNIEnv*, jclass) {
    return gameState();
}

/// An entity's name by uid: player names come off the heap NxtString at entity+0x718, NPC names off
/// the definition's +0x8 (offsets.hpp; both VERIFIED LIVE in the GE). "" when it despawned or the read
/// failed -- a name is cosmetic, it never blocks anything.
inline jstring JNICALL nEntityName(JNIEnv* env, jclass, jint uid) {
    bool found = false;
    Entity e = findEntity(uid, found);
    std::string name = found ? (isPlayerUid(uid) ? playerName(e.addr) : npcName(e.addr)) : std::string{};
    return env->NewStringUTF(name.c_str());
}

/// One widget's state: {ok, x, y, width, height, hidden}, or empty when the id is not loaded right
/// now -- which is constant for background groups, not an error. x/y are the values the widget stores
/// (relative to its parent for nested widgets); id is the client's own (group << 16) | component.
inline jintArray JNICALL nWidget(JNIEnv* env, jclass, jint id) {
    Widget w = widget(id);
    if (!w.ok) return env->NewIntArray(0);
    jint v[6] = { 1, w.x, w.y, w.width, w.height, w.hidden ? 1 : 0 };
    jintArray arr = env->NewIntArray(6);
    if (arr) env->SetIntArrayRegion(arr, 0, 6, v);
    return arr;
}

/// A widget's primary text line, exactly as the game stores it (colour tags and all -- the shim's
/// net.runelite.client.util.Text strips them where a plugin wants that). "" when not loaded.
inline jstring JNICALL nWidgetText(JNIEnv* env, jclass, jint id) {
    Widget w = widget(id);
    return env->NewStringUTF(w.text.c_str());
}

/// A widget's dynamic child by index: {ok, x, y, width, height, hidden}, or empty when out of range.
/// x/y are relative to the parent -- Java accumulates them while descending, matching RuneLite's
/// absolute getCanvasLocation.
inline jintArray JNICALL nWidgetChild(JNIEnv* env, jclass, jint id, jint childIndex) {
    std::uintptr_t w = widgetChildObj(id, childIndex);
    if (!w) return env->NewIntArray(0);
    jint v[6] = {
        1,
        rd<std::int32_t>(w + off::IFTYPE_X),
        rd<std::int32_t>(w + off::IFTYPE_Y),
        rd<std::int32_t>(w + off::IFTYPE_WIDTH),
        rd<std::int32_t>(w + off::IFTYPE_HEIGHT),
        rd<std::uint8_t>(w + off::IFTYPE_HIDDEN) != 0 ? 1 : 0,
    };
    jintArray arr = env->NewIntArray(6);
    if (arr) env->SetIntArrayRegion(arr, 0, 6, v);
    return arr;
}

/// The world map's state: {level, originX, originZ, centreX, centreZ}, or empty when the map object
/// does not exist yet. The origin is the map's own coordinate base in world tiles (MapCoord at
/// wm+0x54B8, VERIFIED LIVE). The centre ints are the map centre in 8-world-tile units
/// (centreTile = 8 * centre = origin + 48; see the WM_* block in offsets.hpp), but they are passed
/// through RAW here: the centre is derived while the zoom is NOT, so there is nothing to turn them
/// into, and nothing on the Java side consumes them right now. There is deliberately no zoom in this
/// array either: the derivation pass proved there is no zoom field anywhere in the world-map object
/// or its view, so inventing one would be a lie.
inline jintArray JNICALL nWorldMap(JNIEnv* env, jclass) {
    std::uintptr_t wm = worldMap();
    if (!wm) return env->NewIntArray(0);
    jint v[5] = {
        rd<std::int32_t>(wm + off::WM_ORIGIN_LEVEL),
        rd<std::int32_t>(wm + off::WM_ORIGIN_X),
        rd<std::int32_t>(wm + off::WM_ORIGIN_Z),
        rd<std::int32_t>(wm + off::WM_CENTRE_X),
        rd<std::int32_t>(wm + off::WM_CENTRE_Z),
    };
    jintArray arr = env->NewIntArray(5);
    if (arr) env->SetIntArrayRegion(arr, 0, 5, v);
    return arr;
}

/// The ids of every widget group whose component data is loaded right now, ascending. The shim diffs
/// this against the previous frame to fire WidgetLoaded/WidgetClosed -- the same trigger shape the
/// client itself uses (a group's data is built on demand when its interface opens). Bounded by the
/// same 0x1000 group-count ceiling as the widget lookup.
inline jintArray JNICALL nLoadedGroups(JNIEnv* env, jclass) {
    std::vector<jint> ids;
    std::uintptr_t c = clientObj();
    if (c) {
        std::uintptr_t mgr = rdp(c + off::IFACE_MANAGER);
        if (mgr) {
            std::uint64_t gcount = rd<std::uint64_t>(mgr + off::IFACE_GROUP_COUNT);
            std::uintptr_t garr = rdp(mgr + off::IFACE_GROUP_ARRAY);
            if (garr && gcount > 0 && gcount <= 0x1000) {
                ids.reserve(static_cast<std::size_t>(gcount));
                for (std::uint64_t g = 0; g < gcount; ++g) {
                    if (rdp(garr + g * off::IFACE_GROUP_ENTRY_STRIDE + off::IFACE_GROUP_ENTRY_DATA))
                        ids.push_back(static_cast<jint>(g));
                }
            }
        }
    }
    jintArray arr = env->NewIntArray(static_cast<jsize>(ids.size()));
    if (arr && !ids.empty()) env->SetIntArrayRegion(arr, 0, static_cast<jsize>(ids.size()), ids.data());
    return arr;
}

// ---------------------------------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------------------------------

/// Windows paths are wide; the JNI option string is narrow. Convert properly rather than truncating.
///
/// The obvious `std::string(w.begin(), w.end())` compiles, works on every path you personally test, and
/// then mangles the classpath for anybody whose Windows username is not pure ASCII -- which is a lot of
/// people, and whose symptom is "kewl/Natives not found" with a perfectly correct-looking path in the
/// error. UTF-8 is what the JVM expects here.
inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

/// Load jvm.dll. `javaHome` comes from kewlklient.ini so nobody has to guess where your JDK is.
/// `detail` is filled in on failure with the exact path tried and the Win32 error, because "check
/// java= in kewlklient.ini" is useless advice on its own -- it does not say what the client READ, and a
/// path that is subtly mangled (a lost backslash, a stray quote) looks correct at a glance in the file.
/// Print what was attempted and the problem is usually obvious on sight.
inline HMODULE loadJvmDll(const std::wstring& javaHome, std::string& detail) {
    // Tell the loader about the JDK's own bin directory before asking for jvm.dll.
    //
    // jvm.dll does not stand alone -- it pulls in siblings that live in the JDK's bin, one level up
    // from bin\server. A plain LoadLibrary resolves those through the HOST process's search path, and
    // we are inside somebody else's process: if the game has narrowed its default search directories
    // (a normal hardening step), the load fails with ERROR_MOD_NOT_FOUND for a file that is plainly
    // sitting right there. AddDllDirectory is additive and per-process rather than replacing anything,
    // so unlike SetDllDirectory it cannot disturb how the game resolves its own DLLs.
    std::wstring bin = javaHome + L"\\bin";
    AddDllDirectory(bin.c_str());

    // A JDK has it under bin\server, a JRE sometimes under bin\client. Try both, then give up.
    const wchar_t* rel[] = { L"\\bin\\server\\jvm.dll", L"\\bin\\client\\jvm.dll" };
    DWORD lastError = 0;
    for (const wchar_t* r : rel) {
        std::wstring full = javaHome + r;

        // The widened search first; then a plain load, because the flags below need the directory to
        // have been registered and an older or stranger host may not cooperate. Whichever works, works.
        HMODULE m = LoadLibraryExW(full.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                   LOAD_LIBRARY_SEARCH_USER_DIRS |
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
        if (!m) m = LoadLibraryW(full.c_str());
        if (m) return m;
        lastError = GetLastError();
    }

    detail = narrow(javaHome + rel[0]);
    detail += "  (error " + std::to_string(lastError);
    if (lastError == 2)        detail += ": no such file -- is java= the JDK folder itself?";
    else if (lastError == 126) detail += ": a dependency of jvm.dll is missing";
    else if (lastError == 193) detail += ": that is a 32-bit JDK, the game is 64-bit";
    detail += ")";
    return nullptr;
}

/// Start the VM, load the classes, wire the natives, call start(). Returns false with a reason you can
/// show the user -- silent failure here is miserable to debug.
inline bool startJvm(const std::wstring& javaHome, const std::wstring& jarPath, std::string& err) {
    std::string detail;
    HMODULE jvmDll = loadJvmDll(javaHome, detail);
    if (!jvmDll) { err = "could not load " + detail; return false; }

    using CreateFn = jint(JNICALL*)(JavaVM**, void**, void*);
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(jvmDll, "JNI_CreateJavaVM"));
    if (!create) { err = "jvm.dll has no JNI_CreateJavaVM"; return false; }

    std::string cp = "-Djava.class.path=" + narrow(jarPath);

    // The control panel is a Swing window, so the VM must not come up headless. Some environments set
    // that by default and the failure is a confusing HeadlessException from inside a plugin.
    std::string headless = "-Djava.awt.headless=false";

    // NOTE: -Djava.security.egd was tried here and does nothing under Wine -- the JDK's entropy
    // collector calls NetworkInterface regardless of the source, and Wine's GetAdaptersAddresses
    // fails. The fix is on the Java side: kewl.WineRandomProvider, inserted before any plugin loads.

    JavaVMOption opt[2]{};
    opt[0].optionString = cp.data();
    opt[1].optionString = headless.data();

    JavaVMInitArgs args{};
    args.version = JNI_VERSION_1_8;
    args.nOptions = 2;
    args.options = opt;
    args.ignoreUnrecognized = JNI_FALSE;

    JNIEnv* env = nullptr;
    if (create(&g_vm, reinterpret_cast<void**>(&env), &args) != JNI_OK || !env) {
        err = "JNI_CreateJavaVM failed";
        return false;
    }

    jclass natLocal = env->FindClass("kewl/Natives");
    if (!natLocal) { err = "kewl/Natives not found -- is kewlklient.jar next to the DLL?"; return false; }
    g_nat = static_cast<jclass>(env->NewGlobalRef(natLocal));

    const JNINativeMethod natives[] = {
        { const_cast<char*>("ready"),       const_cast<char*>("()Z"),     reinterpret_cast<void*>(nReady) },
        { const_cast<char*>("entities"),    const_cast<char*>("()[I"),    reinterpret_cast<void*>(nEntities) },
        { const_cast<char*>("sceneBase"),   const_cast<char*>("()[I"),    reinterpret_cast<void*>(nSceneBase) },
        { const_cast<char*>("varp"),        const_cast<char*>("(I)I"),    reinterpret_cast<void*>(nVarp) },
        { const_cast<char*>("container"),   const_cast<char*>("(I)[I"),   reinterpret_cast<void*>(nContainer) },
        { const_cast<char*>("local"),       const_cast<char*>("()[I"),    reinterpret_cast<void*>(nLocal) },
        { const_cast<char*>("skills"),      const_cast<char*>("()[I"),    reinterpret_cast<void*>(nSkills) },
        { const_cast<char*>("project"),     const_cast<char*>("(III)J"),  reinterpret_cast<void*>(nProject) },
        { const_cast<char*>("doAction"),    const_cast<char*>("(IIII)Z"), reinterpret_cast<void*>(nDoAction) },
        { const_cast<char*>("interactNpc"), const_cast<char*>("(II)Z"),   reinterpret_cast<void*>(nInteractNpc) },
        { const_cast<char*>("viewport"),    const_cast<char*>("()[I"),    reinterpret_cast<void*>(nViewport) },
        { const_cast<char*>("input"),       const_cast<char*>("()[I"),    reinterpret_cast<void*>(nInput) },
        { const_cast<char*>("present"),     const_cast<char*>("([III)V"), reinterpret_cast<void*>(nPresent) },
        { const_cast<char*>("presentPanel"),const_cast<char*>("([III)V"), reinterpret_cast<void*>(nPresentPanel) },
        { const_cast<char*>("gameState"),   const_cast<char*>("()I"),     reinterpret_cast<void*>(nGameState) },
        { const_cast<char*>("entityName"),  const_cast<char*>("(I)Ljava/lang/String;"), reinterpret_cast<void*>(nEntityName) },
        { const_cast<char*>("widget"),      const_cast<char*>("(I)[I"),   reinterpret_cast<void*>(nWidget) },
        { const_cast<char*>("widgetText"),  const_cast<char*>("(I)Ljava/lang/String;"), reinterpret_cast<void*>(nWidgetText) },
        { const_cast<char*>("widgetChild"), const_cast<char*>("(II)[I"),  reinterpret_cast<void*>(nWidgetChild) },
        { const_cast<char*>("worldMap"),    const_cast<char*>("()[I"),    reinterpret_cast<void*>(nWorldMap) },
        { const_cast<char*>("loadedGroups"),const_cast<char*>("()[I"),    reinterpret_cast<void*>(nLoadedGroups) },
    };
    if (env->RegisterNatives(g_nat, natives, sizeof(natives) / sizeof(natives[0])) != JNI_OK) { err = "RegisterNatives failed"; return false; }

    jclass local = env->FindClass("kewl/KewlKlient");
    if (!local) { err = "kewl/KewlKlient not found"; return false; }
    g_api = static_cast<jclass>(env->NewGlobalRef(local));

    jmethodID start = env->GetStaticMethodID(g_api, "start", "()V");
    g_tick   = env->GetStaticMethodID(g_api, "tick", "(I)V");
    g_status = env->GetStaticMethodID(g_api, "status", "()Ljava/lang/String;");
    g_panelMouse = env->GetStaticMethodID(g_api, "panelMouse", "(IIIZ)V");
    if (!start || !g_tick) { err = "kewl.KewlKlient needs static start() and tick(int)"; return false; }

    env->CallStaticVoidMethod(g_api, start);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    return true;
}

/// The JNIEnv for this thread, attaching it the first time. Null if the VM is not up.
inline JNIEnv* env() {
    if (!g_vm) return nullptr;
    JNIEnv* e = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_8) != JNI_OK) {
        if (g_vm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr) != JNI_OK) return nullptr;
    }
    return e;
}

/// Run one frame: plugins tick, overlays draw, and Java calls present() before returning. `keys` is the
/// F-key edge mask.
inline void tickJvm(int keys) {
    JNIEnv* e = env();
    if (!e || !g_tick) return;
    e->CallStaticVoidMethod(g_api, g_tick, static_cast<jint>(keys));
    // A plugin throwing must never take the game down. Print it and carry on.
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); }
}

/// One mouse event on the panel window, up to Java. button: 0 move, 1 left, 2 middle, 3 right, 4 wheel
/// (down = wheel away from you). Coordinates are the panel window's own client space, which is exactly
/// SidePanel's coordinate system. No-op if the jar predates the panel.
inline void panelMouse(int x, int y, int button, bool down) {
    JNIEnv* e = env();
    if (!e || !g_panelMouse) return;
    e->CallStaticVoidMethod(g_api, g_panelMouse, static_cast<jint>(x), static_cast<jint>(y),
                            static_cast<jint>(button), down ? JNI_TRUE : JNI_FALSE);
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); }
}

// ---------------------------------------------------------------------------------------------------
// The panel bridge's JNI half (launcher mode). Every entry point here is written so that a jar
// without kewl.panel.PanelBridge is an EMPTY PANEL, never a crash: bridgeResolve() tries the class
// exactly once, clears whatever NotFound exception the attempt raised, and the callers then see
// bridgeAvailable() == false and fall back (see bridge.hpp). The same guards cover a jar that has the
// class but not a given method, which is the in-between state during development.
// ---------------------------------------------------------------------------------------------------

/// Find kewl.panel.PanelBridge and its statics, once. Cheap after the first call: two branch reads.
inline bool bridgeResolve() {
    if (g_bridgeTried) return g_bridgeRevision != nullptr && g_bridgeSnapshot != nullptr;
    g_bridgeTried = true;

    JNIEnv* e = env();
    if (!e) return false;
    jclass local = e->FindClass("kewl/panel/PanelBridge");
    if (!local) {                       // NoClassDefFoundError on an old jar -- cleared, not fatal
        if (e->ExceptionCheck()) e->ExceptionClear();
        return false;
    }
    g_bridgeCls = static_cast<jclass>(e->NewGlobalRef(local));

    g_bridgeRevision = e->GetStaticMethodID(g_bridgeCls, "modelRevision", "()J");
    g_bridgeSnapshot = e->GetStaticMethodID(g_bridgeCls, "snapshot", "()[I");
    g_bridgeSetBool  = e->GetStaticMethodID(g_bridgeCls, "setBool", "(ILjava/lang/String;Z)V");
    g_bridgeSetInt   = e->GetStaticMethodID(g_bridgeCls, "setInt", "(ILjava/lang/String;I)V");
    g_bridgeSetEnum  = e->GetStaticMethodID(g_bridgeCls, "setEnum", "(ILjava/lang/String;I)V");
    g_bridgeSetText  = e->GetStaticMethodID(g_bridgeCls, "setText", "(ILjava/lang/String;Ljava/lang/String;)V");
    if (e->ExceptionCheck()) e->ExceptionClear();     // a missing method above throws; that is fine

    if (!g_bridgeRevision || !g_bridgeSnapshot) {
        g_bridgeRevision = g_bridgeSnapshot = nullptr;
        return false;
    }
    return true;
}

/// True when the jar can answer both halves of the bridge: the model read and the edits.
inline bool bridgeAvailable() { return bridgeResolve(); }

/// Resolve the format-2 edit methods, once, on the first v2 edit. Same rules as bridgeResolve: every
/// method stands on its own, a miss throws NotFound which is cleared immediately, and the resulting
/// null jmethodID is handled by the dispatch in bridgeApply (which logs once and drops the edit).
/// Deliberately NOT part of bridgeResolve's success test: a jar that can publish a model and take
/// the four set* edits is worth running even if none of the v2 commands exist, and failing the whole
/// bridge over them would empty the panel of a jar that is merely older.
inline void bridgeResolveV2(JNIEnv* e) {
    g_bridgeV2Tried = true;
    g_bridgeResetSetting     = e->GetStaticMethodID(g_bridgeCls, "resetSetting",     "(ILjava/lang/String;)V");
    g_bridgeResetPlugin      = e->GetStaticMethodID(g_bridgeCls, "resetPlugin",      "(I)V");
    g_bridgeSetPinned        = e->GetStaticMethodID(g_bridgeCls, "setPinned",        "(IZ)V");
    g_bridgeHubInstall       = e->GetStaticMethodID(g_bridgeCls, "hubInstall",       "(Ljava/lang/String;)V");
    g_bridgeHubRemove        = e->GetStaticMethodID(g_bridgeCls, "hubRemove",        "(Ljava/lang/String;)V");
    g_bridgeHubRefresh       = e->GetStaticMethodID(g_bridgeCls, "hubRefresh",       "()V");
    g_bridgeProfileSwitch    = e->GetStaticMethodID(g_bridgeCls, "profileSwitch",    "(I)V");
    g_bridgeProfileCreate    = e->GetStaticMethodID(g_bridgeCls, "profileCreate",    "(Ljava/lang/String;)V");
    g_bridgeProfileDelete    = e->GetStaticMethodID(g_bridgeCls, "profileDelete",    "(I)V");
    g_bridgeProfileRename    = e->GetStaticMethodID(g_bridgeCls, "profileRename",    "(ILjava/lang/String;)V");
    g_bridgeProfileDuplicate = e->GetStaticMethodID(g_bridgeCls, "profileDuplicate", "(I)V");
    if (e->ExceptionCheck()) e->ExceptionClear();     // a missing method above throws; that is fine
}

/// Java's model revision, or -1 when there is no bridge to ask. The value itself is opaque (see
/// PanelBridge.modelRevision): it is only ever compared against the last one published.
inline std::int64_t bridgeModelRevision() {
    JNIEnv* e = env();
    if (!e || !bridgeResolve()) return -1;
    jlong rev = e->CallStaticLongMethod(g_bridgeCls, g_bridgeRevision);
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); return -1; }
    return static_cast<std::int64_t>(rev);
}

/// The packed panel model (PanelBridge.snapshot's format, parsed by bridge.hpp). Empty when there is
/// no bridge or the call failed -- both mean "publish nothing", not "publish an empty model", so the
/// launcher keeps showing the last good list.
inline std::vector<jint> bridgeSnapshot() {
    JNIEnv* e = env();
    if (!e || !bridgeResolve()) return {};
    auto arr = static_cast<jintArray>(e->CallStaticObjectMethod(g_bridgeCls, g_bridgeSnapshot));
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); return {}; }
    if (!arr) return {};
    jsize n = e->GetArrayLength(arr);
    std::vector<jint> out(static_cast<std::size_t>(n));
    if (n > 0) e->GetIntArrayRegion(arr, 0, n, out.data());
    e->DeleteLocalRef(arr);
    return out;
}

/// The class name of a pending exception, for one log line about it. ExceptionDescribe's stack trace
/// goes to stderr, which a launcher-spawned game does not even have (KEWL_LOG only redirects stdout),
/// and the edit ring can deliver a burst of records in one tick -- the name is what the line needs,
/// and Java's own code logs the detail around whatever it did not expect.
///
/// With an exception pending, JNI only guarantees the Exception* and Release* calls, so this clears
/// first, does its reflection in the clean window, and re-throws the ORIGINAL -- the caller clears
/// it once the line is out. A reflection that itself goes wrong just leaves "?" and is discarded.
inline std::string exceptionName(JNIEnv* e) {
    jthrowable t = e->ExceptionOccurred();            // peek: the exception stays pending
    if (!t) return "?";
    e->ExceptionClear();

    std::string out = "?";
    if (jclass cls = e->GetObjectClass(t)) {          // e.g. java/lang/IndexOutOfBoundsException
        // getName lives on java/lang/Class, so the methodID comes off the class OF the class.
        // Cached: the same methodID answers for every throwable there will ever be.
        if (jclass meta = e->GetObjectClass(cls)) {
            static jmethodID getName = e->GetMethodID(meta, "getName", "()Ljava/lang/String;");
            if (getName) {
                auto js = static_cast<jstring>(e->CallObjectMethod(cls, getName));
                if (js) {
                    if (const char* c = e->GetStringUTFChars(js, nullptr)) {
                        out = c;
                        e->ReleaseStringUTFChars(js, c);
                    }
                    e->DeleteLocalRef(js);
                }
            }
            e->DeleteLocalRef(meta);
        }
        e->DeleteLocalRef(cls);
    }
    if (e->ExceptionCheck()) e->ExceptionClear();     // a lookup failure must not shadow the original
    e->Throw(t);                                      // hand the original back; the caller clears it
    e->DeleteLocalRef(t);
    return out;
}

/// One edit record from the launcher, into Java. `kind` is bridge.hpp's EditKind (the numbers are
/// written out below because jvm.hpp cannot include bridge.hpp for the enum -- bridge.hpp includes
/// this file). Returns false only when the edit could not be DELIVERED (no bridge, string allocation
/// failed) -- a Java-level rejection (unknown plugin, unknown key, enum index out of range) is logged
/// by Java and counts as consumed, because re-delivering it forever would just burn the bridge's
/// edit ring.
inline bool bridgeApply(std::int32_t kind, std::int32_t pluginIdx, const char* key,
                        std::int64_t intVal, const char* text) {
    JNIEnv* e = env();
    if (!e || !bridgeResolve()) return false;

    // Kinds 4..14 need methods an old jar does not have. Resolve them on the first one we see, so a
    // session that never pins, never touches profiles and never opens the hub never asks.
    if (kind >= 4 && !g_bridgeV2Tried) bridgeResolveV2(e);

    // An edit this DLL has no method for (a kind from a newer launcher, or a jar that has the class
    // but not that method) is DROPPED, not retried: leaving it unconsumed would wedge the edit ring
    // on the first record the two sides disagree about, and every edit behind it with it. One log
    // line says what happened; the launcher's next model publish re-syncs what its widgets show.
    jmethodID method = nullptr;
    switch (kind) {
        case 0:  method = g_bridgeSetBool;          break;   // (int, String, boolean)
        case 1:  method = g_bridgeSetInt;           break;   // (int, String, int)
        case 2:  method = g_bridgeSetEnum;          break;   // (int, String, int)
        case 3:  method = g_bridgeSetText;          break;   // (int, String, String)
        case 4:  method = g_bridgeResetSetting;     break;   // (int, String)
        case 5:  method = g_bridgeResetPlugin;      break;   // (int)
        case 6:  method = g_bridgeSetPinned;        break;   // (int, boolean)
        case 7:  method = g_bridgeHubInstall;       break;   // (String)
        case 8:  method = g_bridgeHubRemove;        break;   // (String)
        case 9:  method = g_bridgeHubRefresh;       break;   // ()
        case 10: method = g_bridgeProfileSwitch;    break;   // (int)
        case 11: method = g_bridgeProfileCreate;    break;   // (String)
        case 12: method = g_bridgeProfileDelete;    break;   // (int)
        case 13: method = g_bridgeProfileRename;    break;   // (int, String)
        case 14: method = g_bridgeProfileDuplicate; break;   // (int)
        default: break;
    }
    if (!method) {
        const unsigned bit = (kind >= 0 && kind < 31) ? (1u << kind) : (1u << 31);
        if (!(g_bridgeKindsLogged & bit)) {
            g_bridgeKindsLogged |= bit;
            std::printf("[bridge] edit kind %d has no Java method on this jar -- dropped\n", kind);
            std::fflush(stdout);
        }
        return true;
    }

    // Build only the jstrings the callee's signature actually takes: `key` names a setting (kinds
    // 0-4), `text` carries the free-form argument (kind 3's value, a hub plugin id for 7/8, a
    // profile name for 11/13). The command kinds -- reset plugin, pin, profile by index, refresh --
    // carry no string at all, and allocating an empty one per edit would be waste for nothing.
    auto jstr = [&](const char* s) -> jstring {
        jstring j = e->NewStringUTF(s ? s : "");
        if (!j) e->ExceptionClear();               // out of memory: retry the edit later
        return j;
    };
    const bool needsKey  = kind <= 4;
    const bool needsText = (kind == 3 || kind == 7 || kind == 8 || kind == 11 || kind == 13);
    jstring jkey = nullptr, jtext = nullptr;
    if (needsKey)  { jkey = jstr(key);  if (!jkey) return false; }
    if (needsText) {
        jtext = jstr(text);
        if (!jtext) { if (jkey) e->DeleteLocalRef(jkey); return false; }
    }

    auto jbool = [](std::int64_t v) { return static_cast<jboolean>(v ? JNI_TRUE : JNI_FALSE); };

    bool delivered = true;
    switch (kind) {
        case 0:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx, jkey, jbool(intVal)); break;
        case 1:
        case 2:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx, jkey,
                                         static_cast<jint>(intVal)); break;
        case 3:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx, jkey, jtext); break;
        case 4:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx, jkey); break;
        case 5:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx); break;
        case 6:  e->CallStaticVoidMethod(g_bridgeCls, method, pluginIdx, jbool(intVal)); break;
        case 7:
        case 8:
        case 11: e->CallStaticVoidMethod(g_bridgeCls, method, jtext); break;
        case 9:  e->CallStaticVoidMethod(g_bridgeCls, method); break;
        case 10:
        case 12:
        case 14: e->CallStaticVoidMethod(g_bridgeCls, method, static_cast<jint>(intVal)); break;
        case 13: e->CallStaticVoidMethod(g_bridgeCls, method, static_cast<jint>(intVal), jtext); break;
        default: delivered = false; break;   // unreachable: the method lookup above already gated it
    }

    if (jkey)  e->DeleteLocalRef(jkey);
    if (jtext) e->DeleteLocalRef(jtext);
    // Java's edit handlers catch their own failures and log them (a bad plugin or profile index is a
    // log line there, not an exception). What still lands here is the case nobody expected -- one
    // line naming it, then clear, and the edit is CONSUMED either way: re-delivering a record Java
    // has rejected would wedge the ring on it and every edit behind it.
    if (e->ExceptionCheck()) {
        std::printf("[bridge] edit kind %d threw %s (cleared)\n", kind, exceptionName(e).c_str());
        std::fflush(stdout);
        e->ExceptionClear();
    }
    return delivered;
}

/// Tell Java which process owns the panel. Called once, right after the VM starts: true when the
/// launcher embedded this game (the panel is ImGui, drawn by the launcher, and Java must not draw
/// SidePanel or listen for panel mouse events), false for today's direct-inject behaviour. Guarded
/// like g_panelMouse, so an older jar without setPanelMode keeps its default (Java owns the panel).
inline void notifyPanelMode(bool imgui) {
    JNIEnv* e = env();
    if (!e || !g_api) return;
    jmethodID m = e->GetStaticMethodID(g_api, "setPanelMode", "(Z)V");
    if (!m) { e->ExceptionClear(); return; }
    e->CallStaticVoidMethod(g_api, m, imgui ? JNI_TRUE : JNI_FALSE);
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); }
}

/// One line per plugin, for when Java is up but nothing is drawing yet.
inline std::string jvmStatus() {
    JNIEnv* e = env();
    if (!e || !g_status) return {};
    auto js = static_cast<jstring>(e->CallStaticObjectMethod(g_api, g_status));
    if (e->ExceptionCheck()) { e->ExceptionDescribe(); e->ExceptionClear(); return {}; }
    if (!js) return {};
    const char* c = e->GetStringUTFChars(js, nullptr);
    std::string out = c ? c : "";
    if (c) e->ReleaseStringUTFChars(js, c);
    e->DeleteLocalRef(js);
    return out;
}

}  // namespace kk
