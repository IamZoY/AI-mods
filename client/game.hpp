// game.hpp -- reading the game, and doing one thing to it.
//
// Everything here is either a guarded memory read or a call into a function the game already has. There
// is no packet building anywhere in KewlKlient, on purpose: we ask the client to perform a menu action
// and it builds and sends the packet itself. That means we never have to track the wire protocol, which
// is the part that changes most often and is hardest to get right.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include "offsets.hpp"

namespace kk {

// ---------------------------------------------------------------------------------------------------
// Guarded reads. Everything we touch is a pointer we derived, in a process we do not own, while the game
// is actively mutating it. A torn or stale read is NORMAL. Never let one crash the client -- return a
// zero and skip that frame instead.
// ---------------------------------------------------------------------------------------------------
inline bool readable(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000) return false;                       // null-ish / first page
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(p), &mbi, sizeof mbi)) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return p + n <= reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

template <class T>
T rd(std::uintptr_t addr, T fallback = T{}) {
    return readable(addr, sizeof(T)) ? *reinterpret_cast<T*>(addr) : fallback;
}

inline std::uintptr_t rdp(std::uintptr_t addr) { return rd<std::uintptr_t>(addr); }

// ---------------------------------------------------------------------------------------------------
// The roots
// ---------------------------------------------------------------------------------------------------
inline std::uintptr_t moduleBase() {
    static std::uintptr_t b = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return b;
}

/// The client object, or 0 if the game has not built it yet (it is null for the first few seconds).
inline std::uintptr_t clientObj() { return rdp(moduleBase() + off::CLIENT_OBJ_PTR); }

/// The scene object, or 0.
inline std::uintptr_t scene() {
    std::uintptr_t c = clientObj();
    return c ? rdp(c + off::SCENE) : 0;
}

struct Tile { int x = 0, y = 0; bool ok = false; };

/// The scene's south-west corner, in world tiles. Entities store SCENE coords; add this to get world.
inline Tile sceneBase() {
    std::uintptr_t s = scene();
    if (!s) return {};
    return { rd<std::int32_t>(s + off::SCENE_BASE_X), rd<std::int32_t>(s + off::SCENE_BASE_Y), true };
}

// ---------------------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------------------
struct Entity {
    int            uid = 0;
    std::uintptr_t addr = 0;
    int            sceneX = 0, sceneY = 0;
    int            plane = 0;
    int            animation = -1;
    int            orientation = 0;
};

/// What KIND of NPC this is. Meaningless for players -- they have no definition object -- so check
/// isPlayerUid first. Returns -1 when the extra pointer hop lands somewhere unreadable, which happens
/// for an entity being spawned or despawned as we walk the table.
inline int npcTypeId(std::uintptr_t entity) {
    if (!entity) return -1;
    std::uintptr_t def = rdp(entity + off::ENTITY_DEF_PTR);
    if (!def) return -1;
    return rd<std::int32_t>(def, -1);
}

/// A player's combat level. ALWAYS -1 on this build: the offset we had (PLAYER_COMBAT_LEVEL,
/// offsets.hpp) is wrong on client-240-6 -- it read a pointer fragment on NPCs and -1 on the local
/// player -- so reading it here shipped pointer garbage out as players' combat levels. Same
/// -1-means-unavailable convention as npcTypeId above; re-derive the offset (see its block in
/// offsets.hpp) before any read comes back.
inline int combatLevel(std::uintptr_t /*entity*/) {
    return -1;
}

/// Walk every entity in the client's registry -- players AND NPCs, from their separate tables -- and
/// hand each to `cb`. The structure (map -> groups -> table pair -> nodes) is documented in
/// offsets.hpp. Bounded at every level so a resize happening under us cannot spin forever: the game
/// rehashes these tables on its own thread, and a chain observed mid-rehash can point at itself.
template <class F>
void forEachEntity(F&& cb) {
    std::uintptr_t c = clientObj();
    if (!c) return;
    std::uintptr_t groups = rdp(c + off::REGISTRY_GROUPS);
    std::uint64_t  gcount = rd<std::uint64_t>(c + off::REGISTRY_GROUP_COUNT);
    if (!groups || gcount == 0 || gcount > 0x1000) return;      // never a real registry this big

    auto walkTable = [&](std::uintptr_t pair, std::uintptr_t bucketsOff, std::uintptr_t countOff) {
        std::uintptr_t buckets = rdp(pair + bucketsOff);
        std::uint64_t  bcount  = rd<std::uint64_t>(pair + countOff);
        if (!buckets || bcount == 0 || bcount > 0x40000) return;  // never a real table this big
        for (std::uint64_t b = 0; b < bcount; ++b) {
            std::uintptr_t node = rdp(buckets + b * 8);
            for (int guard = 0; node && guard < 128; ++guard, node = rdp(node + off::NODE_NEXT)) {
                std::uintptr_t e = rdp(node + off::NODE_ENTITY);
                if (!e) continue;
                Entity ent;
                ent.uid    = static_cast<int>(rd<std::uint32_t>(node + off::NODE_UID));
                ent.addr   = e;
                ent.sceneX = rd<std::int32_t>(e + off::ENTITY_SCENE_X);
                ent.sceneY = rd<std::int32_t>(e + off::ENTITY_SCENE_Y);
                if (ent.sceneX < 0 || ent.sceneY < 0 || ent.sceneX > 104 || ent.sceneY > 104) continue;
                ent.plane       = rd<std::int32_t>(e + off::ENTITY_PLANE);
                // ENTITY_PLANE is SUSPECT on this build (offsets.hpp: the decompile points at 0x7CC
                // instead). An in-range read could still be wrong, but an out-of-range one is
                // certainly not a plane: mark it unknown (-1) rather than ship garbage. Java treats
                // -1 as "plane unavailable" (AutoWalk walks without the plane filter; overlay plane
                // comparisons simply never match, so nothing draws at a wrong height).
                if (ent.plane < 0 || ent.plane > 3) ent.plane = -1;
                ent.animation   = rd<std::int32_t>(e + off::ENTITY_ANIMATION, -1);
                ent.orientation = rd<std::int32_t>(e + off::ENTITY_ORIENTATION);
                cb(ent);
            }
        }
    };

    for (std::uint64_t g = 0; g < gcount; ++g) {
        std::uintptr_t grp = rdp(groups + g * 8);
        for (int gguard = 0; grp && gguard < 256; ++gguard, grp = rdp(grp + off::GROUP_NEXT)) {
            std::uintptr_t pair = rdp(grp + off::GROUP_TABLE);
            if (!pair) continue;
            walkTable(pair, off::PLAYER_BUCKETS, off::PLAYER_BUCKET_COUNT);
            walkTable(pair, off::NPC_BUCKETS,    off::NPC_BUCKET_COUNT);
        }
    }
}

/// True if this uid belongs to a player. The client keeps player handles in a flat array on the client
/// object; anything in the entity table that is not in that array is an NPC.
inline bool isPlayerUid(int uid) {
    std::uintptr_t c = clientObj();
    if (!c) return false;
    int n = rd<std::int32_t>(c + off::PLAYER_COUNT);
    if (n <= 0 || n > 4096) return false;
    for (int i = 0; i < n; ++i) {
        std::uint32_t h = rd<std::uint32_t>(c + off::PLAYER_IDS + static_cast<std::uintptr_t>(i) * 4);
        if (h != 0xFFFFFFFFu && static_cast<int>(h) == uid) return true;
    }
    return false;
}

/// Your own player handle, or -1.
inline int localPlayerUid() {
    std::uintptr_t c = clientObj();
    return c ? rd<std::int32_t>(c + off::LOCAL_PLAYER_IDX, -1) : -1;
}

/// You, as an entity. `found` is false before you are in the world.
inline Entity localPlayer(bool& found) {
    Entity me;
    found = false;
    int uid = localPlayerUid();
    if (uid < 0) return me;
    forEachEntity([&](const Entity& e) {
        if (found || e.uid != uid) return;
        me = e;
        found = true;
    });
    return me;
}

/// Look an entity up by uid. `found` is false when it despawned between frames -- which is a normal
/// answer, not an error; callers show nothing and move on.
inline Entity findEntity(int uid, bool& found) {
    Entity hit;
    found = false;
    if (uid < 0) return hit;
    forEachEntity([&](const Entity& e) {
        if (found || e.uid != uid) return;
        hit = e;
        found = true;
    });
    return hit;
}

// ---------------------------------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------------------------------
inline constexpr int SKILL_COUNT = 25;

/// One skill's effective (boosted) level, base level, or total xp. `which` is 0..24 -- see Skill.java.
inline int skillEffective(int which) {
    std::uintptr_t c = clientObj();
    if (!c || which < 0 || which >= SKILL_COUNT) return 0;
    return rd<std::int32_t>(c + off::SKILL_EFFECTIVE + static_cast<std::uintptr_t>(which) * 4);
}
inline int skillBase(int which) {
    std::uintptr_t c = clientObj();
    if (!c || which < 0 || which >= SKILL_COUNT) return 0;
    return rd<std::int32_t>(c + off::SKILL_BASE + static_cast<std::uintptr_t>(which) * 4);
}
inline int skillXp(int which) {
    std::uintptr_t c = clientObj();
    if (!c || which < 0 || which >= SKILL_COUNT) return 0;
    return rd<std::int32_t>(c + off::SKILL_XP + static_cast<std::uintptr_t>(which) * 4);
}

/// Run energy, 0..10000 (so 10000 is a full bar). SUSPECT: the offset behind this (RUN_ENERGY,
/// offsets.hpp) could not be re-derived on client-240-6 and nothing here re-checks it, so treat the
/// number as unverified until that happens.
inline int runEnergy() {
    std::uintptr_t c = clientObj();
    return c ? rd<std::int32_t>(c + off::RUN_ENERGY) : 0;
}

/// The client's frame counter. Useful as a cheap "is the game actually running" check.
inline int cycle() {
    std::uintptr_t c = clientObj();
    return c ? rd<std::int32_t>(c + off::CYCLE) : 0;
}

// ---------------------------------------------------------------------------------------------------
// Game state, names, world map, widgets
// ---------------------------------------------------------------------------------------------------
/// The client's own state machine: 10 title, 20 logging in, 25 loading, 30 logged in. 0 until the
/// client object exists. The panel's "waiting for the game" gate is this being 30, not a guess.
inline int gameState() {
    std::uintptr_t c = clientObj();
    return c ? rd<std::int32_t>(c + off::GAME_STATE) : 0;
}

/// Read a client NxtString (inline-buffer-or-heap, heap flag bit7 at `flagOff`) into a std::string.
/// Falls back to "" for every torn/unreadable case -- a name is cosmetic, it never blocks a frame.
inline std::string nxtString(std::uintptr_t fieldAddr, std::uintptr_t flagOff) {
    std::uint8_t flag = rd<std::uint8_t>(fieldAddr + flagOff);
    std::uintptr_t p;
    std::uint64_t len;
    if (flag & 0x80) {
        p   = rdp(fieldAddr);
        len = rd<std::uint64_t>(fieldAddr + 8);
    } else {
        p   = fieldAddr;
        len = 0x17 - flag;
    }
    if (!p || len == 0 || len > 200 || !readable(p, static_cast<std::size_t>(len))) return {};
    const char* s = reinterpret_cast<const char*>(p);
    return std::string(s, s + len);
}

/// An NPC's name: its own +0x710 override first (normally empty), else the definition's +0x8.
inline std::string npcName(std::uintptr_t entity) {
    if (!entity) return {};
    std::string own = nxtString(entity + off::ENTITY_NAME_OVERRIDE, off::ENTITY_NAME_OVERRIDE + 0x17);
    if (!own.empty()) return own;
    std::uintptr_t def = rdp(entity + off::ENTITY_DEF_PTR);
    return def ? nxtString(def + off::DEF_NAME, off::DEF_NAME + 0x17) : std::string{};
}

/// A player's name. Players keep a pointer to a heap NxtString at +0x718 (no definition object).
inline std::string playerName(std::uintptr_t entity) {
    if (!entity) return {};
    std::uintptr_t sp = rdp(entity + off::PLAYER_NAME_PTR);
    return sp ? nxtString(sp, 0x17) : std::string{};
}

/// The world map object, or 0. Its origin (WM_ORIGIN_*) is a MapCoord {level, x, z} in world tiles --
/// the client adds it to map coords before splitting into 64x64 map squares.
inline std::uintptr_t worldMap() {
    std::uintptr_t c = clientObj();
    return c ? rdp(c + off::WORLD_MAP) : 0;
}

// A widget, as the shim's getWidget wants it. `id` is (groupId << 16) | componentId -- the client's own
// encoding, unchanged from the Java client. `ok` is false when the group is not loaded (empty sentinel
// or null component data), which happens constantly for background groups; callers must treat every
// field as garbage unless it is set.
struct Widget {
    int         x = 0, y = 0, width = 0, height = 0;
    bool        hidden = false;
    std::string text;
    bool        ok = false;
};

/// Resolve a widget id to its IfType object, or 0. Bounded at both levels like forEachEntity: the
/// interface manager rebuilds these arrays while we read them.
inline std::uintptr_t widgetObj(int id) {
    std::uintptr_t c = clientObj();
    if (!c) return 0;
    std::uintptr_t mgr = rdp(c + off::IFACE_MANAGER);
    if (!mgr) return 0;
    int g = id >> 16;
    std::uint64_t gcount = rd<std::uint64_t>(mgr + off::IFACE_GROUP_COUNT);
    if (g < 0 || static_cast<std::uint64_t>(g) >= gcount || gcount > 0x1000) return 0;
    std::uintptr_t garr = rdp(mgr + off::IFACE_GROUP_ARRAY);
    if (!garr) return 0;
    std::uint64_t ccount = rd<std::uint64_t>(garr + static_cast<std::uintptr_t>(g) * off::IFACE_GROUP_ENTRY_STRIDE + off::IFACE_GROUP_ENTRY_COUNT);
    std::uintptr_t cdata = rdp(garr + static_cast<std::uintptr_t>(g) * off::IFACE_GROUP_ENTRY_STRIDE + off::IFACE_GROUP_ENTRY_DATA);
    int comp = id & 0xFFFF;
    if (!cdata || static_cast<std::uint64_t>(comp) >= ccount) return 0;
    std::uintptr_t w = rdp(cdata + static_cast<std::uintptr_t>(comp) * 16 + 8);
    // The empty group's slot holds a shared static empty object; comparing the pointee against the
    // sentinel's control field is exactly what the client's own null check does.
    if (!w || w == rdp(moduleBase() + off::IFACE_EMPTY_SENTINEL + 8)) return 0;
    return w;
}

/// One widget's bounds/state/text. `ok` false when the id is not loaded right now.
inline Widget widget(int id) {
    Widget wgt;
    std::uintptr_t w = widgetObj(id);
    if (!w) return wgt;
    wgt.x      = rd<std::int32_t>(w + off::IFTYPE_X);
    wgt.y      = rd<std::int32_t>(w + off::IFTYPE_Y);
    wgt.width  = rd<std::int32_t>(w + off::IFTYPE_WIDTH);
    wgt.height = rd<std::int32_t>(w + off::IFTYPE_HEIGHT);
    wgt.hidden = rd<std::uint8_t>(w + off::IFTYPE_HIDDEN) != 0;
    wgt.text   = nxtString(w + off::IFTYPE_TEXT, off::IFTYPE_TEXT_FLAG);
    wgt.ok     = true;
    return wgt;
}

/// A widget's Nth dynamic child -- the client's own child-index addressing (the same index shape
/// FUN_1405B65E0 bounds-checks against IfType+0xB50). 0 when out of range or not loaded.
inline std::uintptr_t widgetChildObj(int id, int childIndex) {
    std::uintptr_t w = widgetObj(id);
    if (!w || childIndex < 0) return 0;
    std::uint64_t cnt = rd<std::uint64_t>(w + off::IFTYPE_CHILDREN_COUNT);
    std::uintptr_t data = rdp(w + off::IFTYPE_CHILDREN_DATA);
    if (!data || static_cast<std::uint64_t>(childIndex) >= cnt) return 0;
    return rdp(data + static_cast<std::uintptr_t>(childIndex) * 16 + 8);
}

// ---------------------------------------------------------------------------------------------------
// Varps and containers
// ---------------------------------------------------------------------------------------------------
// Both are GLOBALS (image-base relative), not fields on the client object, so these readers do not go
// through clientObj(). Both fail closed: a varp we cannot read is 0, a container we cannot find is
// empty. Java applies its own meaning on top (see ClientState.getVarbitValue for why 0 and not -1).

/// One varp value by id. 0 when the array is not up yet or the id is out of range.
inline int varp(int id) {
    if (id < 0) return 0;
    std::uintptr_t arr = rdp(moduleBase() + off::VARP_ARRAY_PTR);
    if (!arr) return 0;
    return rd<std::int32_t>(arr + static_cast<std::uintptr_t>(id) * 4);
}

/// The container node for `containerId`, or 0. Bounded walk: the sentinel sits one bucket past the end.
inline std::uintptr_t containerNode(int containerId) {
    std::int32_t mask = rd<std::int32_t>(moduleBase() + off::CONTAINER_MASK);
    if (mask <= 0 || mask > 0x10000) return 0;               // never a real bucket count this big
    std::uintptr_t buckets = moduleBase() + off::CONTAINER_BUCKETS;
    std::uintptr_t node = rdp(buckets + static_cast<std::uintptr_t>(static_cast<std::uint32_t>(containerId) % mask) * 8);
    std::uintptr_t sentinel = rdp(buckets + static_cast<std::uintptr_t>(mask) * 8);
    for (int guard = 0; node && node != sentinel && guard < 512; ++guard) {
        if (rd<std::int32_t>(node) == containerId) return node;
        node = rdp(node + off::CONTAINER_NODE_NEXT);
    }
    return 0;
}

/// How many slots a container holds, or -1 if the container does not exist right now.
inline int containerSize(int containerId) {
    std::uintptr_t n = containerNode(containerId);
    if (!n) return -1;
    auto start = rd<std::uintptr_t>(n + off::CONTAINER_NODE_IDS);
    auto end   = rd<std::uintptr_t>(n + off::CONTAINER_NODE_IDS_END);
    if (!start || end < start || end - start > 0x100000) return -1;   // torn update mid-resize
    return static_cast<int>((end - start) >> 2);
}

/// One container entry. Item id comes back -1 and quantity 0 past the end (the client's own
/// invGetObjId/invGetNum conventions), which callers read as "no item in this slot".
inline int containerItem(int containerId, int slot) {
    std::uintptr_t n = containerNode(containerId);
    int size = containerSize(containerId);
    if (!n || slot < 0 || slot >= size) return -1;
    return rd<std::int32_t>(rd<std::uintptr_t>(n + off::CONTAINER_NODE_IDS) + static_cast<std::uintptr_t>(slot) * 4, -1);
}

inline int containerQty(int containerId, int slot) {
    std::uintptr_t n = containerNode(containerId);
    int size = containerSize(containerId);
    if (!n || slot < 0 || slot >= size) return 0;
    return rd<std::int32_t>(rd<std::uintptr_t>(n + off::CONTAINER_NODE_QTYS) + static_cast<std::uintptr_t>(slot) * 4);
}

// ---------------------------------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------------------------------
/// Project a point in FINE coordinates to screen pixels, using the game's own projection -- so it is
/// always exactly right, including while the camera is moving, which is the whole reason we call the
/// game's function instead of reimplementing the maths.
///
/// Fine coordinates are tiles << 7: 128 units per tile, so the centre of scene tile (x, y) is
/// ((x << 7) + 64, ((y << 7) + 64). `fineHeight` is the vertical axis -- 0 is ground level.
///
/// Returns false when the point is behind the camera or otherwise off in the weeds. Do not draw it.
inline bool projectFine(int fineX, int fineHeight, int fineY, float& outX, float& outY) {
    using Fn = float* (__fastcall*)(void*, float*, int*);
    auto fn = reinterpret_cast<Fn>(moduleBase() + off::WORLD_TO_SCREEN);

    float out[2] = { 0.f, 0.f };
    int   fine[3] = { fineX, fineHeight, fineY };
    fn(nullptr, out, fine);

    outX = out[0];
    outY = out[1];
    if (!std::isfinite(outX) || !std::isfinite(outY)) return false;
    return !(outX < -10000.f || outX > 10000.f || outY < -10000.f || outY > 10000.f);
}

/// The centre of a SCENE tile, at ground level.
inline bool project(int sceneX, int sceneY, float& outX, float& outY) {
    return projectFine((sceneX << 7) + 64, 0, (sceneY << 7) + 64, outX, outY);
}

// ---------------------------------------------------------------------------------------------------
// Doing something
// ---------------------------------------------------------------------------------------------------
/// Perform a menu action, exactly as if you had clicked it. The client builds and sends the packet.
///
/// `sceneX`/`sceneY` are SCENE coordinates (0..104), not world ones. `opcode` is a menu action number
/// from offsets.hpp. `targetId` is whatever that action targets -- for scenery it is the object id.
///
/// Returns true when the action was handed to the client, false when it was DROPPED: either the client
/// object is not up yet, or DO_ACTION is 0 for this build (the address was never derived, and calling a
/// guessed address crashes the game). The boolean is the only way a caller can tell an issued action
/// from a silent no-op -- plugins must not report success on a false.
///
/// MUST be called from the game thread. Calling it from our own thread works most of the time and then
/// crashes at the worst moment, so the overlay queues actions and the plugin tick runs them on a timer
/// that is slow enough not to matter. If you make KewlKlient do anything fancier than this, hook a
/// per-frame function and run actions from there.
inline bool doAction(int sceneX, int sceneY, int opcode, int targetId) {
    std::uintptr_t c = clientObj();
    if (!c) return false;
    if (off::DO_ACTION == 0) {   // DO_ACTION 0 = not derived this build; acting would crash
        // One line, ever: plugins tick many times a second and this drop is a build problem, not a
        // per-call event. Goes to stdout, which KEWL_LOG redirects to a file (dllmain.cpp).
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::printf("[kewl] doAction dropped: DO_ACTION not derived for this build\n");
            std::fflush(stdout);
        }
        return false;
    }
    using Fn = void(__fastcall*)(void*, int, int, int, int, int, long long, int, int, long long);
    auto fn = reinterpret_cast<Fn>(moduleBase() + off::DO_ACTION);
    fn(reinterpret_cast<void*>(c), sceneX, sceneY, opcode, targetId, 0, 0, 0, 0, 0);
    return true;
}

/// Walk to a SCENE tile. The game pathfinds and sends the movement itself; we only say where. Returns
/// false when the action was dropped (see doAction) -- do not treat that as "walking".
inline bool walkTo(int sceneX, int sceneY) {
    return doAction(sceneX, sceneY, off::OP_WALK, 0);
}

/// Interact with an NPC by uid -- attack it, talk to it, pickpocket it, whatever `opcode` selects.
///
/// The uid IS the target: the client looks the NPC up in the same hashtable we walked to find it, so we
/// do not have to care where it has moved to since. We still pass its tile because that is the shape
/// doAction wants, and we look it up here so callers only need the uid. Returns false when the uid did
/// not resolve (it despawned this frame) or the action was dropped (see doAction).
inline bool interactNpc(int uid, int opcode) {
    bool found = false;
    Entity target;
    forEachEntity([&](const Entity& e) {
        if (found || e.uid != uid) return;
        target = e;
        found = true;
    });
    if (!found) return false;
    return doAction(target.sceneX, target.sceneY, opcode, uid);
}

}  // namespace kk
