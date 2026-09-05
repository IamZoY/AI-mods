package kewl.api;

import kewl.Natives;

/**
 * You: where you are, what you are doing, and how much energy you have left.
 *
 * <p>Get it from {@link Game#me()}. It is never null -- before you are in the world it is a placeholder
 * whose {@link #exists()} returns false and whose numbers are all zero, so a plugin that forgets to
 * check gets harmless zeros instead of a {@code NullPointerException} thirty times a second.</p>
 */
public final class Local {

    public static final Local ABSENT = new Local(false, -1, 0, 0, 0, -1, 0, 0, 0);

    private final boolean exists;
    private final int uid, sceneX, sceneY, plane, animation, orientation, runEnergy, cycle;

    private Local(boolean exists, int uid, int sceneX, int sceneY, int plane,
                  int animation, int orientation, int runEnergy, int cycle) {
        this.exists = exists;
        this.uid = uid;
        this.sceneX = sceneX;
        this.sceneY = sceneY;
        this.plane = plane;
        this.animation = animation;
        this.orientation = orientation;
        this.runEnergy = runEnergy;
        this.cycle = cycle;
    }

    static Local read() {
        int[] v = Natives.local();
        if (v.length != 8) return ABSENT;
        return new Local(true, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    }

    /** False before you have spawned into the world. Everything else is zero when this is false. */
    public boolean exists() { return exists; }

    /** Your own entity handle. */
    public int uid() { return uid; }

    public int sceneX() { return sceneX; }

    public int sceneY() { return sceneY; }

    /** World position -- what your minimap shows. */
    public int worldX() { return Game.sceneBaseX() + sceneX; }

    /** World position -- what your minimap shows. */
    public int worldY() { return Game.sceneBaseY() + sceneY; }

    /** Where you are. */
    public WorldPoint location() { return new WorldPoint(worldX(), worldY()); }

    /** Which floor you are on: 0 is ground level. */
    public int plane() { return plane; }

    /** The animation you are playing, or -1 if you are standing still. */
    public int animation() { return animation; }

    /**
     * True when you are not animating.
     *
     * <p>This is how nearly every gathering bot decides it is time to click again, and it is also the
     * classic source of a bot that clicks twice: the animation does not start on the same tick as the
     * click, so a plugin that checks it immediately after acting will see idle and act again. Give it a
     * moment, the way the woodcutter example does.</p>
     */
    public boolean isIdle() { return animation == -1; }

    /** Facing, 0..2047, 0 = south, rising clockwise. */
    public int orientation() { return orientation; }

    /** Run energy as a percentage, 0..100. */
    public int runEnergy() { return runEnergy / 100; }

    /** The client's frame counter. Handy for "has anything happened since I last looked". */
    public int cycle() { return cycle; }

    /** Your current hitpoints. */
    public int health() { return Skills.effective(Skill.HITPOINTS); }

    /** Your maximum hitpoints. */
    public int maxHealth() { return Skills.level(Skill.HITPOINTS); }

    /** Health as a percentage, 0..100. Returns 100 rather than dividing by zero before you spawn. */
    public int healthPercent() {
        int max = maxHealth();
        return max <= 0 ? 100 : Math.max(0, Math.min(100, health() * 100 / max));
    }
}
