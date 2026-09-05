package kewl;

/**
 * Every native method in KewlKlient, in one class, so the whole unsafe surface is one screen of code.
 *
 * <p>These are registered from C++ at startup (see {@code client/jvm.hpp}). If you add one here you must
 * add it there too, with a matching JNI signature, or the class will fail to link at the first call with
 * an {@code UnsatisfiedLinkError} naming the method.</p>
 *
 * <p><b>You almost certainly want {@link kewl.api.Game} instead.</b> This class returns flat int arrays
 * with no bounds checks and no meaning attached; the api package turns them into things with names. The
 * only reason to call anything here directly is that you are adding a capability the api does not have
 * yet.</p>
 */
public final class Natives {

    private Natives() {}

    /** True once the game has built its client object -- i.e. you are in-game, not at the login screen. */
    public static native boolean ready();

    /**
     * Every visible entity, seven ints each, flattened:
     * {@code uid, sceneX, sceneY, isPlayer, id, animation, orientation}.
     *
     * <p>{@code id} is the NPC type for NPCs. For players it is always {@code -1} on this build: the
     * combat-level offset we had is wrong on client-240-6 (it read pointer garbage, not a level) and
     * has been gated off pending re-derivation -- see {@code PLAYER_COMBAT_LEVEL} in
     * {@code client/offsets.hpp}. Treat -1 as "combat level unavailable", never as a level.</p>
     */
    public static native int[] entities();

    /** {@code {worldX, worldY}} of the loaded scene's south-west corner. Empty when nothing is loaded. */
    public static native int[] sceneBase();

    /**
     * One varp (client config variable) by id. 0 before the game's varp array exists -- callers treat
     * 0 as "unknown", never as a real value.
     */
    public static native int varp(int id);

    /**
     * An item container snapshot (inventory, bank, worn equipment...), flattened
     * {@code {slot0Id, slot0Qty, slot1Id, slot1Qty, ...}} -- slot {@code i} is {@code [2i]} / {@code [2i+1]}.
     *
     * <p>Empty when the container does not exist right now (bank closed, pre-login). A missing item is
     * id {@code -1}, quantity 0. The snapshot is a moment in time: the game can resize or reorder a
     * container while you hold it.</p>
     */
    public static native int[] container(int containerId);

    /** You: {@code {uid, sceneX, sceneY, plane, animation, orientation, runEnergy, cycle}}, or empty. */
    public static native int[] local();

    /** All 25 skills: {@code effective[25]}, then {@code base[25]}, then {@code xp[25]}. */
    public static native int[] skills();

    /**
     * Project a point in fine coordinates (tiles &lt;&lt; 7) to screen pixels, using the game's own
     * projection so it stays correct while the camera moves.
     *
     * @return screen x in the high 32 bits, y in the low 32, or {@link Long#MIN_VALUE} if off-screen
     */
    public static native long project(int fineX, int fineHeight, int fineY);

    /**
     * Perform a menu action, in SCENE coordinates. This is the only way KewlKlient acts on the game.
     *
     * @return true when the action was handed to the client; false when it was NOT issued -- either
     *     the client object is not up yet, or the client's action function could not be derived for
     *     this build ({@code DO_ACTION == 0} in {@code client/offsets.hpp}), in which case this call
     *     is a silent no-op on the game side and this boolean is the only signal that nothing
     *     happened. Callers must not report success on a false.
     */
    public static native boolean doAction(int sceneX, int sceneY, int opcode, int targetId);

    /**
     * Interact with an NPC by uid; the native side looks its tile up.
     *
     * @return false when the uid did not resolve (the NPC despawned this frame) or the action was not
     *     issued -- see {@link #doAction} for what that means.
     */
    public static native boolean interactNpc(int uid, int opcode);

    /** The game's client area on screen: {@code {x, y, width, height}}. */
    public static native int[] viewport();

    /**
     * Mouse, modifier keys and keyboard edges, read from Windows rather than game memory (no offset
     * needed). The array is EIGHT fixed ints followed by up to SIXTEEN key-edge entries:
     * {@code {mouseX, mouseY, shift, ctrl, alt, leftButton, rightButton, middleButton, vk1, vk2, ...}}.
     *
     * <p>Mouse coordinates are in the game window's client area -- the same space the projection
     * natives produce screen points in. A held modifier or button is 1, a released one 0. The trailing
     * entries are KEY EDGES: the virtual-key codes that went from up to down since the previous call,
     * so plugin hotkeys can dispatch real KeyEvents. There are never more than 16 of them per call and
     * the total length therefore varies -- index the first eight blindly, everything else by length.</p>
     *
     * <p>{@code net.runelite.api.ClientState.setInputState} is the intended parser: it unpacks the
     * fixed fields and turns the key edges into this frame's events.</p>
     */
    public static native int[] input();

    /**
     * Put a finished frame on the overlay. {@code px} must be {@code w*h} <b>premultiplied</b> ARGB
     * pixels, top row first -- which is exactly what a {@code BufferedImage.TYPE_INT_ARGB_PRE} holds.
     */
    public static native void present(int[] px, int w, int h);

    /**
     * The control panel's frame, same pixel contract as {@link #present}. Lands on the panel's own
     * window, pinned to the game's right edge -- {@code w} is the panel's width and {@code h} the
     * game client's height, which SidePanel chooses.
     */
    public static native void presentPanel(int[] px, int w, int h);

    /**
     * The client's own game-state field: 10 title, 20 logging in, 25 loading, 30 logged in.
     * 0 before the client object exists.
     */
    public static native int gameState();

    /**
     * An entity's name by uid -- players off entity+0x718, NPCs off their definition's +0x8.
     * Empty when it despawned or the name could not be read. Names may contain U+00A0 where the
     * game pads; callers that compare against typed text should fold that to a space.
     */
    public static native String entityName(int uid);

    /**
     * One widget's state by its client id ({@code (group << 16) | component}):
     * {@code {ok, x, y, width, height, hidden}}, or empty when the group is not loaded right now.
     * x/y are as the widget stores them -- relative to its parent for nested widgets.
     */
    public static native int[] widget(int id);

    /** A widget's primary text line, colour tags included. Empty when not loaded. */
    public static native String widgetText(int id);

    /**
     * A widget's dynamic child by index: {@code {ok, x, y, width, height, hidden}}, or empty when the
     * index is out of range. x/y are relative to the parent widget.
     */
    public static native int[] widgetChild(int id, int childIndex);

    /**
     * The world map's state: {@code {level, originX, originZ, centreX, centreZ}}, or empty before the
     * map object exists. The origin is the map's coordinate base in world tiles; the centre ints are
     * the map centre in 8-tile units (centre tile = {@code 8 * centre = origin + 48}), passed through
     * raw and unused by the Java side right now. There is no zoom here on purpose: the client has no
     * zoom field in this object or its view (verified in the binary), so any number we returned would
     * be invented.
     */
    public static native int[] worldMap();

    /** Ids of every widget group whose component data is currently loaded, ascending. */
    public static native int[] loadedGroups();
}
