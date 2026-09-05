// Shim of net.runelite.api.Client (BSD-2, RuneLite) -- a concrete class backed by kewl.api and the
// natives, not an injected client.
//
// Everything kewl can already read (player, skills, tick count, scene) is wired through. Everything
// that needs a new memory offset (varps, item containers, widgets, world map, menu, game state,
// camera) delegates to net.runelite.api.ClientState, which holds honest defaults until each offset is
// re-derived; each method there names the offset it is waiting for.
package net.runelite.api;

import net.runelite.api.widgets.Widget;
import java.util.EnumSet;
import java.util.Set;

import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.client.callback.ClientThread;

public class Client
{
	public static final Client INSTANCE = new Client();

	private final ClientThread clientThread = new ClientThread();
	// Live view: the supplier re-reads kewl's per-frame Local, so getLocalPlayer() always answers with
	// THIS frame's position, not a snapshot taken before the game ever refreshed.
	private final Player player = new Player(() -> kewl.api.Game.me());
	private final WorldView topLevelWorldView = WorldView.TOP_LEVEL;
	private final ClientState state = new ClientState();

	Client()
	{
	}

	public static Client get()
	{
		return INSTANCE;
	}

	public ClientState state()
	{
		return state;
	}

	// -- world and self --------------------------------------------------------------------------

	/**
	 * Null before the player has spawned, exactly like upstream -- plugins null-check this before
	 * projecting from it, and a non-null Player with a garbage position would sail past those checks.
	 */
	public Player getLocalPlayer()
	{
		return kewl.api.Game.me().exists() ? player : null;
	}

	public WorldView getTopLevelWorldView()
	{
		return topLevelWorldView;
	}

	public WorldView getWorldView(int id)
	{
		return id == WorldView.TOPLEVEL ? topLevelWorldView : null;
	}

	public WorldView findWorldViewFromWorldPoint(WorldPoint point)
	{
		return topLevelWorldView;
	}

	public int getPlane()
	{
		return getTopLevelWorldView().getPlane();
	}

	// -- skills -----------------------------------------------------------------------------------

	public int getRealSkillLevel(Skill skill)
	{
		kewl.api.Skill k = toKewl(skill);
		return k == null ? 0 : kewl.api.Skills.level(k);
	}

	public int getBoostedSkillLevel(Skill skill)
	{
		kewl.api.Skill k = toKewl(skill);
		return k == null ? 0 : kewl.api.Skills.effective(k);
	}

	public int getTotalLevel()
	{
		return kewl.api.Skills.totalLevel();
	}

	// -- vars (ClientState holds the values; the varps native fills them) ---------------------------

	public int getVarbitValue(int id)
	{
		return state.getVarbitValue(id);
	}

	public int getVarpValue(int id)
	{
		return state.getVarpValue(id);
	}

	// -- state that needs new offsets ---------------------------------------------------------------

	public GameState getGameState()
	{
		return state.getGameState();
	}

	public EnumSet<WorldType> getWorldType()
	{
		return state.getWorldType();
	}

	/**
	 * The server tick count, in RuneLite's semantics: one per 600ms game tick. kewl's cycle counter is
	 * a frame counter (+1 per 20ms), so the conversion is a division; PendingTask's tick comparisons
	 * depend on this being a real tick count, not a frame count.
	 */
	public int getTickCount()
	{
		return kewl.api.Game.me().cycle() / 30;
	}

	public boolean isResized()
	{
		return state.isResized();
	}

	public double getMinimapZoom()
	{
		return state.getMinimapZoom();
	}

	public int getCameraYawTarget()
	{
		return state.getCameraYawTarget();
	}

	public Point getMouseCanvasPosition()
	{
		return state.getMouseCanvasPosition();
	}

	public boolean isKeyPressed(int keyCode)
	{
		return state.isKeyPressed(keyCode);
	}

	public boolean isMenuOpen()
	{
		return state.isMenuOpen();
	}

	public Widget getWidget(int... ids)
	{
		return state.getWidget(ids);
	}

	public ItemContainer getItemContainer(int id)
	{
		return state.getItemContainer(id);
	}

	public ItemDefinition getItemDefinition(int itemId)
	{
		return state.getItemDefinition(itemId);
	}

	public net.runelite.api.worldmap.WorldMap getWorldMap()
	{
		return state.getWorldMap();
	}

	public EnumComposition getEnum(int id)
	{
		return state.getEnum(id);
	}

	public Menu getMenu()
	{
		return state.getMenu();
	}

	/** The minimap draw area, resolved the same way the plugin resolves it (widget by interface id). */
	public Widget getMinimapDrawWidget()
	{
		return state.getMinimapDrawWidget();
	}

	// -- scripts ------------------------------------------------------------------------------------

	/**
	 * The client's script VM is not reachable; the only scripted call the plugin makes scrolls the
	 * fairy-ring panel, which the panel's own revalidation covers well enough.
	 */
	public Object runScript(int id, Object... args)
	{
		return null;
	}

	/**
	 * Empty: runScript is a stub, so nothing ever writes the script stacks. The only reader is
	 * Quest.getState, whose result the shim cannot know anyway.
	 */
	public int[] getIntStack()
	{
		return new int[0];
	}

	// -- plumbing ------------------------------------------------------------------------------------

	/**
	 * The thread the shim runs the game loop on, matching GameEngine's contract: PathfinderConfig
	 * compares Thread.currentThread() against this to decide whether a refresh may run inline.
	 */
	public Thread getClientThread()
	{
		return ClientThread.getGameThread();
	}

	/** Deferred-run accessor, mirroring the injected ClientThread plugins get. */
	public ClientThread clientThread()
	{
		return clientThread;
	}

	public boolean isGpu()
	{
		return false;
	}

	/** Convenience for the bridge: refresh the frame snapshot kewl already read this frame. */
	public static void newFrame()
	{
		kewl.api.Skills.newFrame();
	}

	/**
	 * The two Skill enums share their constant names; this is the whole mapping. Fail-soft on purpose:
	 * if the shim enum grows a constant the kewl enum has not caught up with, callers get a 0 rather
	 * than an IllegalArgumentException from the middle of a refresh sweep.
	 */
	private static kewl.api.Skill toKewl(Skill skill)
	{
		try
		{
			return kewl.api.Skill.valueOf(skill.name());
		}
		catch (IllegalArgumentException e)
		{
			return null;
		}
	}
}
