// offsets.hpp -- every game-specific number KewlKlient depends on, in one file.
//
// READ THIS BEFORE CHANGING ANYTHING HERE.
//
// These are offsets into a program we do not control, and Jagex rebuilds it roughly weekly. Two kinds of
// number live here and they rot at very different speeds:
//
//   FUNCTION RVAs   (DO_ACTION, WORLD_TO_SCREEN)  move on almost every update. Assume they are wrong
//                                                 after any patch until you re-derive them.
//   STRUCT OFFSETS  (ENTITY_SCENE_X, ENTITY_TABLE...) are stabler, but they DO move. ENTITY_TABLE moved
//                                                 by 0x10 between two builds a few weeks apart.
//
// BUILD_ID below is the sanity check. It is the RVA of a function we use as a fingerprint for "which
// build is this". If it does not match, every other number in this file is suspect and the client
// refuses to start rather than reading garbage out of a stranger's address space.
//
// HOW TO RE-DERIVE THESE: see README.md, section "When the game updates". Short version: run the Ghidra
// headless script in tools/, or open the exe in IDA and use the anchors named in the comments below.
// Every one of these was found by anchoring on something the client itself names -- a string, a Lua
// binding, a distinctive constant -- never by scanning for a byte pattern and hoping.
#pragma once
#include <cstdint>

namespace kk::off {

// ---------------------------------------------------------------------------------------------------
// BUILD FINGERPRINT
// ---------------------------------------------------------------------------------------------------
// The ClientState Lua-binding registration function's RVA. We do not call it; we only use its address
// as a build id. It is found by anchoring on the binding-name string "getVarp" (see the deob skill):
// the function that references that string IS this registration, so it is as easy to re-derive as it
// is unique per build. If your client does not match, DO NOT just bump this number: re-derive the
// whole file, because everything below was measured on this exact build.
//
// Derived statically from osclient.exe client-240-6,
//   sha256 d6a43c08fc8d2c7934b081545a6b26023830c55ccd11cd86549056dd343471e1,
// fetched from Jagex's own CDN (see .claude/skills/deob/SKILL.md). The registry/scene offsets below
// were subsequently VERIFIED LIVE against that exact binary running under Wine (see their comments);
// items marked NOT (re-)VERIFIED were read out of the binary but not confirmed against a running game.
inline constexpr std::uintptr_t BUILD_ID = 0xF6140;

// ---------------------------------------------------------------------------------------------------
// THE ROOT POINTER
// ---------------------------------------------------------------------------------------------------
// *(imageBase + CLIENT_OBJ_PTR) is "the client object" -- the god-object almost everything hangs off.
//
// HOW FOUND (client-240-6): the client's own getStatEffectiveLevel Lua binding reads it. That leaf is
// a `mov rcx, [rip+X]` followed by reads of the skill arrays off rcx -- and those three arrays land
// exactly on the SKILL_* offsets below, which is what makes this the client object, not a neighbour.
// VERIFIED LIVE under Wine: *(base + 0xE95668) is the pointer every live read in this file went through.
inline constexpr std::uintptr_t CLIENT_OBJ_PTR = 0xE95668;

// ---------------------------------------------------------------------------------------------------
// FUNCTIONS WE CALL (RVAs from the module base)
// ---------------------------------------------------------------------------------------------------
// The client's own "do a menu action" entry point. We call it instead of building network packets by
// hand: it takes the same arguments the real menu does, and the client builds and sends the packet for
// us. This is why KewlKlient does not need to know the wire protocol at all.
//
// Signature (as we use it):
//   void doAction(void* clientObj, int sceneX, int sceneY, int opcode, int targetId,
//                 int a6, long long a7, int itemId, int flags, long long a10)
//
// 0 is the "not derived for this build" sentinel, and game.hpp refuses to act while it is 0. The old
// value was measured on a build months dead and nothing here re-derives it statically: the reliable
// method is hook-and-log (hook the candidate, click in game, read the arguments back), which needs a
// running client. Until someone does that, every action call is a no-op by design -- reading memory
// wrong shows you a wrong number, but CALLING the wrong address crashes the game.
//
// The 240-6 candidate, for whoever does that hook run: FUN_140373F80 (rva 0x373F80) -- it has the old
// DoAction argument SHAPE (obj, a, b, target, flags) and stores into the PENDING_ACTION_* record on the
// client object. But the adversarial pass showed its +0x90/+0x94 args are NOT scene x/y: +0x90 is a
// packed widget id and +0x94 a component index, so this is the widget-menu action path, and the tile
// actions may go through the minimenu entry exec (FUN_14037E990, vtable 0x140BC91B0 slot 1) instead.
// Neither is confirmed against a real click. Hook first, call second.
inline constexpr std::uintptr_t DO_ACTION = 0;

// The client's world->screen projection leaf. Takes {fineX, fineY, fineZ} and writes {screenX, screenY}.
// "Fine" coordinates are tiles << 7 (i.e. 128 units per tile). It reads the camera out of the client
// object itself, so the first argument is ignored -- pass nullptr.
//
//   float* worldToScreen(void* ignored, float out[2], int fine[3])
//
// HOW FOUND (client-240-6): the Graphics usertype's worldToScreenCoord method. Its closure body is
// this function, and its shape -- same argument order, same out-array write -- matches the leaf the
// previous build used, just with the projection maths now split between two callees it invokes
// internally. NOT VERIFIED in-game: draw a tile outline and see whether it sits on the tile.
inline constexpr std::uintptr_t WORLD_TO_SCREEN = 0x2202A0;

// The projection's camera position and canvas-scale pair, read by the leaf above and by its last
// step. HOW FOUND (client-240-6): FUN_1402202a0 (= WORLD_TO_SCREEN) subtracts three ints at
// client+0x895d8 / +0x895dc / +0x895e0 from the fine input before projecting -- the camera position
// in the same fine axis order we pass ({x, height, y}; the axis order is pinned by FUN_1406a8130,
// where the second coordinate only ever reaches the depth and vertical terms). Its final step
// FUN_140618ce0 then rescales the result by (*(client+0x90)+0x10+0x5c) / (+0x20) for x and
// (*(client+0x90)+0x10+0x60) / (+0x24) for y -- a divide/multiply pair nobody has identified, which
// is why WORLD_TO_SCREEN above is still marked NOT VERIFIED. These exist here only so nProject's
// once-a-second probe (jvm.hpp) can print them next to a projected point: standing still, the probe's
// own tile must land on your character, and the printed view=(n/d, n/d) ratios say whether the leaf
// returns a scaled space instead of canvas pixels. NOT VERIFIED in-game -- that probe is the test.
inline constexpr std::uintptr_t CAMERA_FINE_X = 0x895D8;
inline constexpr std::uintptr_t CAMERA_FINE_H = 0x895DC;
inline constexpr std::uintptr_t CAMERA_FINE_Y = 0x895E0;
inline constexpr std::uintptr_t VIEW_OBJ      = 0x90;    // -> view object; scales live at +0x10+...
inline constexpr std::uintptr_t VIEW_BASE_W   = 0x5C;    // x rescale numerator
inline constexpr std::uintptr_t VIEW_BASE_H   = 0x20;    // x rescale denominator
inline constexpr std::uintptr_t VIEW_CANVAS_W = 0x60;    // y rescale numerator
inline constexpr std::uintptr_t VIEW_CANVAS_H = 0x24;    // y rescale denominator

// ---------------------------------------------------------------------------------------------------
// FIELDS ON THE CLIENT OBJECT
// ---------------------------------------------------------------------------------------------------
inline constexpr std::uintptr_t SCENE            = 0xCA90;  // -> the scene/world object (see below)
inline constexpr std::uintptr_t LOCAL_PLAYER_IDX = 0xCC5C;  // your own player handle
inline constexpr std::uintptr_t PLAYER_COUNT     = 0xCCE0;  // how many entries in PLAYER_IDS
inline constexpr std::uintptr_t PLAYER_IDS       = 0xCCE4;  // int[] of player handles, 0xFFFFFFFF = empty

// Your stats. Three parallel int arrays of 25, one entry per skill, in the game's own skill order (see
// Skill.java). "Effective" is the boosted/drained number you see in the top of the skill tab; "base" is
// what your xp actually earns you. Hitpoints is index 3, so eff[3] is your current HP and base[3] your
// maximum -- that is the whole health system, and it is why there is no separate HP offset.
//
// The three are 0x64 apart (25 ints), which is a useful check: if you re-derive one, the other two
// should land exactly 100 bytes later.
//
// CONFIRMED UNCHANGED on client-240-6, statically: the client's own getStatEffectiveLevel /
// getStatBaseLevel / getStatXP Lua bindings read exactly these three offsets (each also gates on a
// nonzero dword at client+0x413F34 -- likely "stats loaded"; we do not depend on it).
inline constexpr std::uintptr_t SKILL_EFFECTIVE = 0x3360;
inline constexpr std::uintptr_t SKILL_BASE      = 0x33C4;
inline constexpr std::uintptr_t SKILL_XP        = 0x3428;

inline constexpr std::uintptr_t RUN_ENERGY = 0x34D0;  // 0..10000, so divide by 100 for the percentage
inline constexpr std::uintptr_t CYCLE      = 0x2164;  // client tick counter, +1 per 20ms frame

// The client's own state machine, next to CYCLE. HOW FOUND (client-240-6): the isLoggedIn Lua leaf is
// literally "load client global; cmpq $-1, 0x3328(%rax); cmpl $0x1e, 0x2160(%rax)" -- it compares this
// dword to 30. There is a dedicated setter at RVA 0x5DB60 (writes +0x2160, switches on the new value).
// Values seen compared against it: 1,2,5,6,10,11,20,25,30,40,45,1000; the Java client's numbering
// (10=title, 20=logging-in, 25=loading, 30=logged-in) fits and 30 is what the field held while we stood
// in the GE. VERIFIED LIVE: read 30 while logged in, and the login form held something else before.
inline constexpr std::uintptr_t GAME_STATE = 0x2160;  // int32; 30 = logged in

// The pending menu-action record -- what the game fills when you click a menu entry, and what its
// packet sender reads back. Three ints on the client object plus a small state tail:
//   +0x90 packed widget id (groupId<<16 | componentId -- the same split FUN_1405B64D0 does)
//   +0x94 component index / selector (-1 = "the tile itself, no sub-object")
//   +0x98 target id (npc/player/loc index, as read back by the packet writer)
//   +0x9C u16 sequence, +1 per queued action
//   +0x9E u8 flags,  +0x9F u8 "action pending" (1 when queued)
//
// HOW FOUND (client-240-6): FUN_140373F80 stores its args at exactly these offsets and bumps the seq;
// the packet sender FUN_14038A790 reads the same fields back (+0x90 as 4-byte BE, +0x94 as tri-byte,
// +0x98 as 2-byte BE) and enqueues opcode 0x26 via FUN_140203D30 when connection state == 3. NOTE the
// tempting "scene x / scene y" reading of +0x90/+0x94 is WRONG -- +0x90 is a packed widget id (its high
// half bounds-checks against the interface manager's group count) and +0x94 is an index, not an axis.
// NOT VERIFIED LIVE (needs a hook-and-log against a real click before anything calls into it).
inline constexpr std::uintptr_t PENDING_ACTION_PACKED_ID = 0x90;
inline constexpr std::uintptr_t PENDING_ACTION_INDEX     = 0x94;
inline constexpr std::uintptr_t PENDING_ACTION_TARGET    = 0x98;
inline constexpr std::uintptr_t PENDING_ACTION_SEQ       = 0x9C;  // u16
inline constexpr std::uintptr_t PENDING_ACTION_PENDING   = 0x9F;  // u8, 1 = action queued

// The world map object. HOW FOUND (client-240-6): the client's own getMapOrigin Lua leaf is
// "mov rax,[rip+X] (the client global); mov rcx,[rax+0x49B8]; movsd xmm0,[rcx+0x54B8]". The object is
// 0x5520 bytes with vtable 0x140BAAD50. Its origin is a MapCoord {int level, int x, int z} at wm+0x54B8
// -- the base the client adds to map coords before splitting into 64x64 map squares (getMapTile leaf:
// (mc.x+origin.x)>>6 and &0x3F). While standing in the GE (not looking at the map) it read (3136,3384),
// matching the scene base, with the centre ints at wm+0x54C4/0x54C8 at (398,429).
// Map zoom is NOT DERIVED -- there is no "zoom" string anywhere in the binary; do not guess one.
// VERIFIED LIVE: wm non-null and origin sane while logged in.
inline constexpr std::uintptr_t WORLD_MAP              = 0x49B8;  // on the client object
inline constexpr std::uintptr_t WM_ORIGIN_LEVEL        = 0x54B8;  // MapCoord on the world map object:
inline constexpr std::uintptr_t WM_ORIGIN_X            = 0x54BC;  //   {level, x, z}
inline constexpr std::uintptr_t WM_ORIGIN_Z            = 0x54C0;
inline constexpr std::uintptr_t WM_CENTRE_X            = 0x54C4;  // int, scroll position (init -1)
inline constexpr std::uintptr_t WM_CENTRE_Z            = 0x54C8;  // int, scroll position (init -1)

// ---------------------------------------------------------------------------------------------------
// VARPS (client config variables) -- a global, not a client-object field
// ---------------------------------------------------------------------------------------------------
// *(imageBase + VARP_ARRAY_PTR) is the int[] of varp values, indexed by varp id.
//
// HOW FOUND (client-240-6): the ClientState usertype's getVarp binding is three instructions --
// `mov rax, [rip+X]`, `movsxd rcx, edx`, `mov eax, [rax+rcx*4]`. That pointer cell is this offset.
// The varbit decoder confirms it from the other side: it reads the same array (as a direct address,
// imageBase+0x155C520 -- the cell statically points there) when shifting and masking.
inline constexpr std::uintptr_t VARP_ARRAY_PTR = 0x155C508;

// The client's own varbit decoder, callable as `unsigned int getVarbit(int varbitId)`. It looks the
// varbit definition up by id, then reads and shifts the varp array -- so it is ground truth and needs
// no definition table on our side. We do NOT call it: its unknown-id path reads an uninitialised
// definition struct, and one garbage mask index is one crash we do not get to debug. The shim decodes
// varbits itself from the varp array plus resources/varbits.csv. Recorded so the option stays visible.
inline constexpr std::uintptr_t GET_VARBIT = 0x5B51F0;

// ---------------------------------------------------------------------------------------------------
// ITEM CONTAINERS (inventory, bank, worn...) -- a global open-addressing-ish table
// ---------------------------------------------------------------------------------------------------
// HOW FOUND (client-240-6): the invGetObjId/invGetNum bindings. Both walk the same table: bucket
// `containerId % bucketCount` holds a linked list of container nodes, and the entry at index
// bucketCount is a sentinel the walk stops at.
//
// A node (bytes, from the two bindings' decompilations):
//   +0x00 int    containerId
//   +0x08 ptr    itemIds, first entry      ) (end - start) >> 2 = slot count,
//   +0x10 ptr    itemIds, one past last    )  4 bytes per entry
//   +0x20 ptr    quantities, first entry   ) same shape, parallel array
//   +0x28 ptr    quantities, one past last )
//   +0x38 ptr    next node in the bucket (0 ends the chain)
inline constexpr std::uintptr_t CONTAINER_BUCKETS = 0x154C500;  // array of ptr, AT imageBase (not a ptr cell)
inline constexpr std::uintptr_t CONTAINER_MASK    = 0x154C508;  // int bucket count; sentinel sits one past it

// ---------------------------------------------------------------------------------------------------
// THE ENTITY REGISTRY  (fields on the client object; players and NPCs live here, in separate tables)
// ---------------------------------------------------------------------------------------------------
// On this build the scene object holds TILES ONLY -- the old scene+0xB8/0xC0 entity hashtable is gone.
// Entities hang off a map object at client+REGISTRY_MAP, in two levels:
//
//   map+0x20  group heads  (= client+REGISTRY_GROUPS)      -- array of 8-byte heads
//   map+0x28  group count  (= client+REGISTRY_GROUP_COUNT, u64)
//   client+REGISTRY_GROUP_SEL                             -- int key naming the CURRENT group
//
// A group node (walk the chain hanging off a head slot):
//   +0x00 u32  group key (compared against REGISTRY_GROUP_SEL)
//   +0x10 ptr  the group's table pair (the PLAYER_/NPC_ offsets below are fields on it)
//   +0x18 ptr  next group node, 0 ends the chain
//
// The pair holds TWO hash tables with identical node layout, one per entity kind:
//   +0x68 ptr / +0x70 u64   PLAYERS: bucket array, bucket count
//   +0x98 ptr / +0xA0 u64   NPCS:    bucket array, bucket count
// A node lives in bucket (uid % count); the slot at buckets[count] is a 0xFFFF... sentinel, but real
// chains also end in a plain 0, so stopping at 0 is enough.
//
// A node (same shape in both tables):
//   +0x00 u32  uid
//   +0x10 ptr  the ENTITY. NOT +0x08: that is a refcounted wrapper (vtable 0x140b93f28) with no
//              coords -- reading it as the entity was exactly the bug in the first live walk.
//   +0x18 ptr  next node in the bucket, 0 ends it
//
// HOW FOUND (client-240-6): from the client's own accessors, not by shape-guessing. playerFindSelf
// (FUN_1403a9150) gates on client+0xCC5C (the local player index), then FUN_1400a0060 ->
// FUN_14009ffe0 -> FUN_1400ef5e0 resolves map/group, and FUN_1400edb90 walks a table at group+0x68
// counting uids and returning node+0x10 -- the PLAYER table. getNpcIdAll (FUN_1403ae380) reads the
// NPC uid array at scene+0xD0/0xD8, and npcCoord (FUN_1403af380) returns {*(scene+0x18),
// *(entity+0x3F0), *(entity+0x418)}. VERIFIED LIVE under Wine via /proc/pid/mem at the Grand
// Exchange: the player table enumerated exactly the uids in PLAYER_IDS (local player included), the
// NPC table exactly the 12 uids of scene+0xD0's array -- with sane scene coords, idle animations
// (-1) and orientations in 256-step cardinal values.
inline constexpr std::uintptr_t REGISTRY_MAP          = 0xC9C8;  // on the client object: the map object
inline constexpr std::uintptr_t REGISTRY_GROUPS       = 0xC9E8;  // = map+0x20: group head array
inline constexpr std::uintptr_t REGISTRY_GROUP_COUNT  = 0xC9F0;  // = map+0x28: group count (u64)
inline constexpr std::uintptr_t REGISTRY_GROUP_SEL    = 0xCC60;  // on the client object: current group key

inline constexpr std::uintptr_t GROUP_TABLE        = 0x10;  // field on a group node: the table pair
inline constexpr std::uintptr_t GROUP_NEXT         = 0x18;  // field on a group node
inline constexpr std::uintptr_t PLAYER_BUCKETS     = 0x68;  // field on the table pair
inline constexpr std::uintptr_t PLAYER_BUCKET_COUNT = 0x70;  // field on the table pair (u64)
inline constexpr std::uintptr_t NPC_BUCKETS        = 0x98;  // field on the table pair
inline constexpr std::uintptr_t NPC_BUCKET_COUNT   = 0xA0;  // field on the table pair (u64)
inline constexpr std::uintptr_t NODE_UID           = 0x00;  // field on an entity node (u32)
inline constexpr std::uintptr_t NODE_ENTITY        = 0x10;  // field on an entity node
inline constexpr std::uintptr_t NODE_NEXT          = 0x18;  // field on an entity node

// The uid array of the NPCs in the current scene (count at SCENE_NPC_UID_COUNT), read by the client's
// own getNpcIdAll binding. Kept as a cross-check of the registry walk, not used for enumeration.
inline constexpr std::uintptr_t SCENE_NPC_UIDS      = 0xD0;  // field on the scene object: int[] of uids
inline constexpr std::uintptr_t SCENE_NPC_UID_COUNT = 0xD8;  // field on the scene object

// ---------------------------------------------------------------------------------------------------
// FIELDS ON THE SCENE OBJECT  ( *(clientObj + SCENE) )
// ---------------------------------------------------------------------------------------------------
// The scene's south-west corner in WORLD tiles. Entities carry SCENE coordinates (0..104), so:
//     worldX = SCENE_BASE_X + entity.sceneX
//
// HOW FOUND (client-240-6): the previous build kept these at scene+0x48/0x4C; on this build those
// addresses hold something else entirely -- reading them is what made the panel sit on "waiting for
// the game". Pinned LIVE under Wine while standing at the Grand Exchange: +0x1C/+0x20 hold the scene
// size (104, 104) and +0x24/+0x28 hold (3112, 3440), the GE's world coordinates -- exactly what a
// south-west corner in world tiles should read. VERIFIED LIVE.
inline constexpr std::uintptr_t SCENE_BASE_X = 0x24;
inline constexpr std::uintptr_t SCENE_BASE_Y = 0x28;

// ---------------------------------------------------------------------------------------------------
// FIELDS ON AN ENTITY (a player or an NPC)
// ---------------------------------------------------------------------------------------------------
inline constexpr std::uintptr_t ENTITY_SCENE_X = 0x3F0;
inline constexpr std::uintptr_t ENTITY_SCENE_Y = 0x418;
inline constexpr std::uintptr_t ENTITY_PLANE   = 0x420;  // 0..3, which floor it is standing on (NOT re-verified on this build)

// The NPC's type id -- what KIND of monster it is, which is what you filter on. It is behind ONE more
// pointer than everything else here:
//
//     typeId = *(int*)( *(void**)(entity + ENTITY_DEF_PTR) )
//
// That extra hop is why a plain memory scan will never find it: the id is not stored in the entity at
// all, only a pointer to the shared definition every NPC of that kind shares. Players have no
// definition here, so this reads as garbage for them -- check isPlayer first.
// NOT re-verified on client-240-6 (the live walk confirmed coords/animation/orientation, not this).
inline constexpr std::uintptr_t ENTITY_DEF_PTR = 0x730;

// Names. The client's string type ("NxtString", 24 bytes, used for every name) is:
//   +0x00 char* heap data -- OR the first byte of a 23-byte inline buffer (SSO)
//   +0x08 u64   heap length
//   +0x17 u8    flag: bit7 set = heap (use +0x00 as pointer, +0x08 as length);
//                     clear = inline (length is 0x17 - byte, data inline at +0x00)
// Always NUL-terminated. HOW FOUND: FUN_14004d0e0 (reserve) writes exactly those fields and
// FUN_14004d140 (assign) NUL-terminates both branches.
//
// NPCs: entity+0x710 is an INLINE NxtString holding the NPC's own name override (normally empty), and
// entity+0x730 is the definition whose +0x8 holds the real name (see FUN_1400a2b30, reached from the
// npcName Lua binding). If *(def+0x138) != 0 the name comes from an alternate config instead -- rare,
// we fall back to "" rather than walking that chain. PLAYERS: different layout -- *(entity+0x718) is a
// POINTER to a heap NxtString, pushed directly by the playerName binding; players have no +0x730 def.
// Note the client pads names with U+00A0 (non-breaking space) where the Java client shows ' '.
//
// VERIFIED LIVE in the GE: local player's name came back through +0x718, and every nearby NPC
// ("Banker" x5, "Guard" x2, "Master smithing tutor") through def+0x8.
inline constexpr std::uintptr_t ENTITY_NAME_OVERRIDE = 0x710;  // inline NxtString on the entity
inline constexpr std::uintptr_t PLAYER_NAME_PTR      = 0x718;  // -> NxtString (players only)
inline constexpr std::uintptr_t DEF_NAME             = 0x8;    // NxtString on the NPC definition

// The widget/interface system. It is the classic rs2lib IfType (ctti string in the binary names
// "jag::oldscape::rs2lib::IfType"), NOT NXT's lui system, and the classic OSRS id encoding survives:
// id = (groupId << 16) | componentId. Lookup chain (FUN_1405B64D0 / FUN_1405B65E0, reached from the
// client's own ifType Lua binding -- every step below is quoted instruction-for-instruction in the
// decompile of that chain):
//
//   mgr      = *(client + IFACE_MANAGER)         -- one-instruction getter FUN_1400871B0
//   groupCount = *(u64*)(mgr + IFACE_GROUP_COUNT)
//   groupArray = *(void**)(mgr + IFACE_GROUP_ARRAY)   -- 24-byte group entries:
//                 +0x8 u64 componentCount, +0x10 void* componentData (may be null until lazy-loaded)
//   entry    = componentData + componentId*16       -- 16-byte jag::shared_ptr entries
//   ifType   = *(void**)(entry + 8)                 -- pointee at +8, control block at +0
//   invalid  = ifType == *(imageBase + IFACE_EMPTY_SENTINEL)  (static empty object)
//
// Sub-children of a widget: count at IfType+0xB50, data at IfType+0xB58, same 16-byte entries.
// Text: IfType+0x158 (and +0x170 for the second line) as NxtString-shaped inline-or-pointer with the
// heap flag's bit7 at IfType+0x16F (and +0x187) -- the same read rule as the NxtString above.
//
// VERIFIED LIVE: 970 groups loaded in the GE, sane bounds (canvas-sized 1054x784 on the top-level
// groups), and real strings came back through the text rule ("<col=808080>Chat-channel</col>",
// "Membership: <col=ff0000>None</col>"). The colour fields were derived too but REFUTED in
// adversarial review (registration carries no offsets for them) -- do not add them without re-deriving.
inline constexpr std::uintptr_t IFACE_MANAGER       = 0x413BE8;  // on the client object
inline constexpr std::uintptr_t IFACE_GROUP_COUNT   = 0x6600;    // u64, on the interface manager
inline constexpr std::uintptr_t IFACE_GROUP_ARRAY   = 0x6608;    // -> 24-byte group entries
inline constexpr std::uintptr_t IFACE_EMPTY_SENTINEL = 0x155C5F0;  // global; compare entry+8's
                                                                  // pointee against *(base+this+8)
inline constexpr std::uintptr_t IFTYPE_X            = 0x5C;   // int, relative to the parent
inline constexpr std::uintptr_t IFTYPE_Y            = 0x60;
inline constexpr std::uintptr_t IFTYPE_WIDTH        = 0x64;
inline constexpr std::uintptr_t IFTYPE_HEIGHT       = 0x68;
inline constexpr std::uintptr_t IFTYPE_HIDDEN       = 0x78;   // bool
inline constexpr std::uintptr_t IFTYPE_CHILDREN_COUNT = 0xB50;  // u64
inline constexpr std::uintptr_t IFTYPE_CHILDREN_DATA  = 0xB58;  // -> 16-byte shared_ptr entries
inline constexpr std::uintptr_t IFTYPE_TEXT         = 0x158;  // inline-or-char* string; flag:
inline constexpr std::uintptr_t IFTYPE_TEXT_FLAG    = 0x16F;  //   bit7 = heap, SSO len = 0x17-flag
inline constexpr std::uintptr_t IFTYPE_TEXT2        = 0x170;
inline constexpr std::uintptr_t IFTYPE_TEXT2_FLAG   = 0x187;

// Both VERIFIED LIVE on client-240-6: every enumerated GE NPC read -1 at 0x4D8 while standing still
// and a cardinal 0/512/1024/1536 at 0x3E0; coords at 0x3F0/0x418 match npcCoord's disasm exactly.
inline constexpr std::uintptr_t ENTITY_ANIMATION   = 0x4D8;  // current animation id, -1 when idle
inline constexpr std::uintptr_t ENTITY_ORIENTATION = 0x3E0;  // 0..2047, 0 = south, rising clockwise

// Combat level, on a PLAYER entity. NPCs keep theirs on the shared definition instead -- reading this
// on an NPC live returned a pointer fragment (the high half of a heap address), never a level. On the
// local player it read -1, so this offset is simply WRONG on client-240-6. NOT VERIFIED -- re-derive
// from the client's combat-level Lua binding before trusting any number that comes out of here.
inline constexpr std::uintptr_t PLAYER_COMBAT_LEVEL = 0x734;

// ---------------------------------------------------------------------------------------------------
// MENU OPCODES
// ---------------------------------------------------------------------------------------------------
// These are the client's INTERNAL menu action numbers, not network opcodes. They are what the game puts
// in its own right-click menu, and handing one to DO_ACTION is exactly equivalent to clicking it.
//
// HOW THESE WERE FOUND, and how to find another: hook DO_ACTION so it logs its arguments, perform the
// action by hand in game, and read the opcode out of the log. Every number below came from a real click.
// Do not guess them from a list you found somewhere -- they are per-build and they do get shuffled.
inline constexpr int OPLOC1 = 3;   // scenery, first option: Chop down / Mine / Open / Climb...

// NPC options one through five. Attack is normally the first, but not always -- Talk-to is first on a
// shopkeeper -- so a bot that always sends OPNPC1 will happily talk to a cow.
inline constexpr int OPNPC1 = 9;
inline constexpr int OPNPC2 = 10;
inline constexpr int OPNPC3 = 11;
inline constexpr int OPNPC4 = 12;
inline constexpr int OPNPC5 = 13;

// Walk to a tile. THIRTY-ONE, not twenty-three.
//
// There is a neighbouring opcode that also looks like a walk and is not: it scales the coordinates you
// give it through the viewport ratio before storing them, so feeding it scene tiles walks you to a
// place that has nothing to do with where you asked. This one takes scene tiles directly. If your
// character walks somewhere baffling, this is the first thing to check.
inline constexpr int OP_WALK = 31;

}  // namespace kk::off
