// The shim's event source: turns kewl's frame loop into the RuneLite events the plugin subscribes to.
//
// GameTick and PostClientTick fire on a real game-tick boundary -- the client's cycle counter counts
// 20ms frames, so a tick is thirty of them. GameStateChanged comes from the client's own state field
// (client+0x2160, LIVE as of client-240-6), WorldChanged from the despawn/respawn transition, and the
// container/widget events from diffs of the natives that Phase D landed. The menu events still wait on
// a hook-and-log of the minimenu path -- see offsets.hpp DO_ACTION for why that one is call-protected.
package kewl.rl;

import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.ItemContainerChanged;
import net.runelite.api.events.PostClientTick;
import net.runelite.api.events.WidgetClosed;
import net.runelite.api.events.WidgetLoaded;
import net.runelite.api.events.WorldChanged;
import net.runelite.api.worldmap.WorldMap;
import net.runelite.client.eventbus.EventBus;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

final class Events
{
	/** The client's cycle counter is a frame counter (+1 per 20ms); a game tick is 600ms of them. */
	private static final int FRAMES_PER_TICK = 30;

	/** The containers whose changes the ported plugin actually reacts to. */
	private static final int[] WATCHED_CONTAINERS = { 93, 95 };  // InventoryID.INV, InventoryID.BANK

	private final EventBus eventBus;
	private int lastTick = Integer.MIN_VALUE;
	private int lastBaseX = Integer.MIN_VALUE, lastBaseY = Integer.MIN_VALUE;
	private boolean playerWasPresent;
	private GameState lastState;
	private final int[][] lastContainers = new int[WATCHED_CONTAINERS.length][];
	private int[] lastLoadedGroups = new int[0];

	Events(EventBus eventBus)
	{
		this.eventBus = eventBus;
	}

	/** Called once per frame, before the plugin's own tick. */
	void fire()
	{
		GameState state = Client.get().getGameState();
		if (state != lastState)
		{
			GameStateChanged ev = new GameStateChanged();
			ev.setGameState(state);
			eventBus.post(ev);
			lastState = state;
		}

		// WorldChanged means "the world changed" -- a hop, a logout, a respawn. The respawn half of the
		// despawn/respawn transition is the trigger: it does NOT happen on the scene re-centres that
		// fire constantly while walking, and a stationary world hop (same coordinates, scene reloaded)
		// still goes through it. (The scene base moving is useless here: walking re-centres it every
		// few dozen tiles.) The first frame never fires -- there is no "was" to compare against.
		boolean playerPresent = kewl.api.Game.me().exists();
		if (lastBaseX != Integer.MIN_VALUE && !playerWasPresent && playerPresent)
		{
			eventBus.post(new WorldChanged());
		}
		lastBaseX = kewl.api.Game.sceneBaseX();
		lastBaseY = kewl.api.Game.sceneBaseY();
		playerWasPresent = playerPresent;

		fireContainerChanges();
		fireWidgetChanges();
		pushWorldMap();

		int tick = kewl.api.Game.me().cycle() / FRAMES_PER_TICK;
		if (tick != lastTick)
		{
			eventBus.post(new GameTick());
			eventBus.post(new PostClientTick());
			lastTick = tick;
		}
	}

	/**
	 * ItemContainerChanged, by diffing each watched container's flat snapshot. A container read is one
	 * native call, so this is two per frame; a deep-equals on the arrays is the change test, and the
	 * same snapshot is what the event carries (as fresh Item objects -- the arrays must not be shared
	 * or a listener mutating one would corrupt the next diff).
	 */
	private void fireContainerChanges()
	{
		for (int i = 0; i < WATCHED_CONTAINERS.length; i++)
		{
			int id = WATCHED_CONTAINERS[i];
			int[] flat = kewl.Natives.container(id);
			if (Arrays.equals(flat, lastContainers[i]))
			{
				continue;
			}
			lastContainers[i] = flat;
			eventBus.post(new ItemContainerChanged(id, Client.get().getItemContainer(id)));
		}
	}

	/** WidgetLoaded/WidgetClosed, by diffing the set of groups whose component data is loaded. */
	private void fireWidgetChanges()
	{
		int[] groups = kewl.Natives.loadedGroups();
		Set<Integer> now = new HashSet<>();
		for (int g : groups)
		{
			now.add(g);
		}
		Set<Integer> before = new HashSet<>();
		for (int g : lastLoadedGroups)
		{
			before.add(g);
		}
		for (int g : groups)
		{
			if (!before.contains(g))
			{
				WidgetLoaded ev = new WidgetLoaded();
				ev.setGroupId(g);
				eventBus.post(ev);
			}
		}
		for (int g : lastLoadedGroups)
		{
			if (!now.contains(g))
			{
				// modalMode 0 / unload true: the shim never builds modal widgets, and the group's data
				// going away is exactly what RuneLite's "unload" closed event means.
				eventBus.post(new WidgetClosed(g, 0, true));
			}
		}
		lastLoadedGroups = groups;
	}

	/**
	 * Push the world map's centre into the shim's WorldMap each frame, in world tiles.
	 *
	 * The native array is {level, originX, originZ, centreX, centreZ}. The centre ints are the map's
	 * scroll position in units of 8 tiles, and the origin is that same centre shifted -48 tiles (the
	 * corner of the map-square load window -- derived from FUN_1401ce8b0/FUN_1401cefe0, which write
	 * origin = 8*centre - 48 and load squares over centre-6..centre+6). So the centre tile is
	 * 8*centreScroll = origin + 48; both encodings were checked live at the GE (scroll 398,429 ->
	 * origin 3136,3384 = 8*398-48, 8*429-48), and the origin read is used here because it is the one
	 * verified across sessions. One caveat, stated where the value is consumed: the +48 (centre vs
	 * load-window corner) follows from the symmetric +-6 load window, not from a live "which tile is
	 * under the widget centre" measurement -- worth one probe with the map open before trusting
	 * on-map click targets to the exact tile.
	 *
	 * There is still no zoom: the client provably has no zoom field on the world-map object, so the
	 * placeholder WorldMap already carries is passed straight back through. See WorldMap's header for
	 * what that placeholder does and does not buy the overlays.
	 */
	private void pushWorldMap()
	{
		int[] wm = kewl.Natives.worldMap();
		if (wm.length == 5)
		{
			// wm[1]/wm[2] are WM_ORIGIN_X/Z; +48 turns the load-window corner into the centre tile.
			WorldMap.INSTANCE.set(new net.runelite.api.Point(wm[1] + 48, wm[2] + 48),
				WorldMap.INSTANCE.getWorldMapZoom());
		}
		else
		{
			// No world-map object (pre-login or the client dropped it): whatever was pushed before is
			// stale, so drop liveness rather than let overlays and map clicks act on dead data.
			WorldMap.INSTANCE.clear();
		}
	}
}
