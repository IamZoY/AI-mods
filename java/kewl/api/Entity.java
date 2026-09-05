package kewl.api;

import java.awt.Point;

import kewl.Natives;

/**
 * One NPC or player in the loaded scene, as it was at the start of this frame.
 *
 * <p>A snapshot, deliberately. The game mutates these objects continuously on its own thread, so a live
 * view would give you a position that changes halfway through your own drawing code. Everything here was
 * read once, at the top of the tick, and will not change under you.</p>
 */
public final class Entity {

    private final int uid, sceneX, sceneY, id, animation, orientation;
    private final boolean player;

    Entity(int uid, int sceneX, int sceneY, boolean player, int id, int animation, int orientation) {
        this.uid = uid;
        this.sceneX = sceneX;
        this.sceneY = sceneY;
        this.player = player;
        this.id = id;
        this.animation = animation;
        this.orientation = orientation;
    }

    /**
     * Names for the entities seen this frame, uid -> name. A uid names one entity for as long as it
     * is on screen (the game reuses handles only after a despawn), so a cache keyed by uid is exactly
     * right and keeps the name() call out of the hot snapshot path: the native walks the whole entity
     * registry to resolve a uid, which is far too slow to do per entity per frame.
     */
    private static final java.util.Map<Integer, String> NAMES = new java.util.HashMap<>();

    /**
     * Drop every cached name. Called when the scene base moves (a despawn storm walks with it), so a
     * uid the game reused for a different entity can never serve a stale name.
     */
    static void clearNameCache() { NAMES.clear(); }

    /** The game's own handle for this entity. Stable while it is on screen; reused after it despawns. */
    public int uid() { return uid; }

    /**
     * Its name, or "" when the client could not read one (mid-spawn, despawned this frame). Read
     * through the cache above, then the entityName native.
     */
    public String name() {
        String n = NAMES.get(uid);
        if (n != null) return n;
        n = Natives.entityName(uid);
        if (n == null || n.isEmpty()) return "";
        if (NAMES.size() > 1024) NAMES.clear();     // despawn storm safety valve
        NAMES.put(uid, n);
        return n;
    }

    /** True for another player, false for an NPC. */
    public boolean isPlayer() { return player; }

    /** True for an NPC. */
    public boolean isNpc() { return !player; }

    /**
     * For an NPC, its type id -- what kind of creature it is, and the thing you filter on. For a
     * player this is {@code -1}: the combat-level offset the shim had is wrong on this build and has
     * been gated off pending re-derivation (see {@code PLAYER_COMBAT_LEVEL} in {@code
     * client/offsets.hpp}), so no player carries a combat level here yet.
     */
    public int id() { return id; }

    /** The animation it is playing, or -1 when it is standing still. */
    public int animation() { return animation; }

    /** True when it is not animating. The usual "has my bot finished doing the thing" check. */
    public boolean isIdle() { return animation == -1; }

    /** Facing, 0..2047, where 0 is south and the number rises clockwise. */
    public int orientation() { return orientation; }

    /** Position within the loaded scene, 0..104. This is what the game's click function wants. */
    public int sceneX() { return sceneX; }

    /** Position within the loaded scene, 0..104. */
    public int sceneY() { return sceneY; }

    /** World position -- the coordinates on your minimap. */
    public int worldX() { return Game.sceneBaseX() + sceneX; }

    /** World position -- the coordinates on your minimap. */
    public int worldY() { return Game.sceneBaseY() + sceneY; }

    /** Where it is, in world coordinates. */
    public WorldPoint location() { return new WorldPoint(worldX(), worldY()); }

    /** Tiles from you, measured diagonally (a chebyshev distance, like the game's own ranges). */
    public int distance() { return Game.distanceTo(sceneX, sceneY); }

    /** Where to draw a marker for it, or null when it is off screen or behind the camera. */
    public Point screen() { return Game.projectTile(sceneX, sceneY); }

    @Override
    public String toString() {
        return (player ? "player" : "npc") + "#" + id + " uid=" + uid + " @" + worldX() + "," + worldY();
    }
}
