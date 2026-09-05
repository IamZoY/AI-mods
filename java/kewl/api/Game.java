package kewl.api;

import java.awt.Point;
import java.awt.Polygon;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import kewl.Natives;

/**
 * The world, as of the start of this frame.
 *
 * <p>Read it from anywhere; it is refreshed once per tick before any plugin runs, so every plugin and
 * every overlay in a single frame sees exactly the same world. That matters more than it sounds: if
 * entities were re-read on demand, a plugin could pick a target in {@code tick()} and then draw a box
 * somewhere else in {@code render()} because the NPC moved in between.</p>
 */
public final class Game {

    private Game() {}

    private static final int FINE = 128;          // fine units per tile
    private static final int HALF = FINE / 2;

    private static List<Entity> entities = Collections.emptyList();
    private static List<Entity> npcs = Collections.emptyList();
    private static List<Entity> players = Collections.emptyList();
    private static Local local = Local.ABSENT;
    private static int baseX, baseY;
    private static boolean loaded;

    /** Called once per frame by the client, before plugins run. You should not need to call it. */
    public static void refresh() {
        int[] base = Natives.sceneBase();
        loaded = base.length == 2;
        int nbx = loaded ? base[0] : 0;
        int nby = loaded ? base[1] : 0;
        if (nbx != baseX || nby != baseY) {
            Entity.clearNameCache();      // scene moved: uids about to churn, names with them
        }
        baseX = nbx;
        baseY = nby;

        local = Local.read();

        int[] flat = Natives.entities();
        List<Entity> all = new ArrayList<>(flat.length / 7);
        List<Entity> n = new ArrayList<>();
        List<Entity> p = new ArrayList<>();
        int me = local.uid();
        for (int i = 0; i + 6 < flat.length; i += 7) {
            int uid = flat[i];
            if (uid == me) continue;                     // you are Local, not one of the crowd
            Entity e = new Entity(uid, flat[i + 1], flat[i + 2], flat[i + 3] != 0,
                                  flat[i + 4], flat[i + 5], flat[i + 6]);
            all.add(e);
            (e.isPlayer() ? p : n).add(e);
        }
        entities = Collections.unmodifiableList(all);
        npcs = Collections.unmodifiableList(n);
        players = Collections.unmodifiableList(p);
    }

    /** True once you are actually in the world -- not at the login screen, not still loading. */
    public static boolean ready() { return Natives.ready() && loaded && local.exists(); }

    /** You. Never null; ask {@link Local#exists()} before trusting it. */
    public static Local me() { return local; }

    /** Every visible NPC and player except you. */
    public static List<Entity> entities() { return entities; }

    /** Every visible NPC. */
    public static List<Entity> npcs() { return npcs; }

    /** Every visible player except you. */
    public static List<Entity> players() { return players; }

    /** World x of the loaded scene's south-west corner. */
    public static int sceneBaseX() { return baseX; }

    /** World y of the loaded scene's south-west corner. */
    public static int sceneBaseY() { return baseY; }

    // -----------------------------------------------------------------------------------------------
    // Coordinates
    // -----------------------------------------------------------------------------------------------

    /**
     * World to scene. Returns null when that tile is not in the loaded chunk -- which is a real answer,
     * not an error: you cannot click a tile the client has not loaded.
     */
    public static Point toScene(int worldX, int worldY) {
        if (!loaded) return null;
        int sx = worldX - baseX, sy = worldY - baseY;
        if (sx < 0 || sy < 0 || sx > 104 || sy > 104) return null;
        return new Point(sx, sy);
    }

    /** Diagonal distance from you to a scene tile. */
    public static int distanceTo(int sceneX, int sceneY) {
        if (!local.exists()) return Integer.MAX_VALUE;
        return Math.max(Math.abs(sceneX - local.sceneX()), Math.abs(sceneY - local.sceneY()));
    }

    // -----------------------------------------------------------------------------------------------
    // Projection -- turning world positions into places on screen
    // -----------------------------------------------------------------------------------------------

    /**
     * Project a fine-coordinate point. Fine units are tiles &times; 128, and {@code height} is the
     * vertical axis with 0 at ground level.
     *
     * @return where to draw, or null if it is behind the camera or off in the distance
     */
    public static Point projectFine(int fineX, int height, int fineY) {
        long packed = Natives.project(fineX, height, fineY);
        if (packed == Long.MIN_VALUE) return null;
        return new Point((int) (packed >> 32), (int) packed);
    }

    /** The centre of a scene tile, at ground level. Null when it is not on screen. */
    public static Point projectTile(int sceneX, int sceneY) {
        return projectFine((sceneX << 7) + HALF, 0, (sceneY << 7) + HALF);
    }

    /** The centre of a world tile. Null when it is not loaded or not on screen. */
    public static Point projectWorld(int worldX, int worldY) {
        Point scene = toScene(worldX, worldY);
        return scene == null ? null : projectTile(scene.x, scene.y);
    }

    /**
     * The four corners of a scene tile as a polygon you can draw or fill.
     *
     * <p>This is what makes a tile marker look like it is lying on the ground rather than stuck to your
     * screen: each corner goes through the game's own projection, so the shape gets the perspective
     * right by construction and stays right while the camera turns.</p>
     *
     * @return the outline, or null if any corner is off screen
     */
    public static Polygon tileOutline(int sceneX, int sceneY) {
        int x0 = sceneX << 7, y0 = sceneY << 7;
        Point a = projectFine(x0, 0, y0);
        Point b = projectFine(x0 + FINE, 0, y0);
        Point c = projectFine(x0 + FINE, 0, y0 + FINE);
        Point d = projectFine(x0, 0, y0 + FINE);
        if (a == null || b == null || c == null || d == null) return null;

        Polygon poly = new Polygon();
        poly.addPoint(a.x, a.y);
        poly.addPoint(b.x, b.y);
        poly.addPoint(c.x, c.y);
        poly.addPoint(d.x, d.y);
        return poly;
    }

    /** The tile outline for a world tile, or null. */
    public static Polygon tileOutlineWorld(int worldX, int worldY) {
        Point scene = toScene(worldX, worldY);
        return scene == null ? null : tileOutline(scene.x, scene.y);
    }
}
