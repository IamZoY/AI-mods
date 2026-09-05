// The auto-walk driver: turns the plugin's computed path into actual movement.
//
// Upstream Shortest Path is guidance-only -- it never issues a single step. Kewl's user asked for the
// client to walk the path, so this watches the pathfinder's output and, once per game tick, issues a
// kewl walk action to the farthest tile of the path that is (a) on the player's plane and (b) inside
// the loaded scene. The game client then walks there with its own router, which is safe: every tile we
// hand it came from the plugin's collision map, and the client re-validates against its own.
//
// Plane changes (boats, stairs, fairy rings) are never walked into -- the run stops there and waits
// for the player, so the driver can never strand someone on the wrong level.
//
// Until a second ported plugin produces paths, this is wired specifically to ShortestPathPlugin; it
// reads the plugin's own accessors rather than reimplementing any routing logic.
package kewl.rl;

import java.util.List;

import kewl.api.Actions;
import kewl.api.Game;
import kewl.api.Local;
import shortestpath.ShortestPathPlugin;
import shortestpath.WorldPointUtil;
import shortestpath.pathfinder.Pathfinder;
import shortestpath.pathfinder.PathStep;

class AutoWalk
{
	/** How far ahead of the cursor, in tiles, we are willing to aim in one walk action. */
	private static final int MAX_SCENE_REACH = 30;

	/** The client's cycle counter is a frame counter (+1 per frame callback); a game tick is ~600ms
	 *  of them (~20ms a frame, the OSRS frame pace -- not re-proven on this build), so ~30. */
	private static final int FRAMES_PER_TICK = 30;

	private final ShortestPathPlugin plugin;

	AutoWalk(ShortestPathPlugin plugin)
	{
		this.plugin = plugin;
	}

	/**
	 * Called every frame while the plugin is enabled and auto-walk is switched on.
	 *
	 * @return a one-line status for the control panel, or null when idle
	 */
	String tick()
	{
		if (plugin.getPathfinder() == null || plugin.getPathfinder().getPath() == null)
		{
			return null;
		}

		List<PathStep> path = plugin.getPathfinder().getPath();
		if (path.isEmpty())
		{
			return null;
		}

		Local me = Game.me();
		if (me == null || !me.exists() || !Game.ready())
		{
			return null;
		}

		// A new pathfinder instance means the plugin recomputed (or replaced) the path: drop the
		// cursor and the stale "already walking toward" target, which belong to the old path. Without
		// this, a teleport or a new destination leaves lastTarget pointing into the old path and the
		// guard below refuses to ever walk again.
		if (plugin.getPathfinder() != lastPathfinder)
		{
			lastPathfinder = plugin.getPathfinder();
			lastTarget = WorldPointUtil.UNDEFINED;
			cursor = 0;
			lastCursorTile = -1;
		}
		// The path can also be REPLACED under the same instance -- the search rebuilds its step list
		// when it finds a better route, and index cursor now denotes a different tile. Re-anchor near
		// the player instead of walking toward a tile the old route happened to touch.
		if (lastCursorTile != -1
			&& path.get(Math.min(cursor, path.size() - 1)).getPackedPosition() != lastCursorTile)
		{
			cursor = nearestIndexToPlayer(path, me);
			lastCursorTile = path.get(cursor).getPackedPosition();
			lastTarget = WorldPointUtil.UNDEFINED;
		}

		// One walk action per game tick, not per frame; the cycle counter counts 20ms frames.
		int tick = me.cycle() / FRAMES_PER_TICK;
		if (tick == lastWalkTick)
		{
			return status(path.size());
		}
		lastWalkTick = tick;

		// The client is already walking toward our last target; let it get there before re-aiming,
		// otherwise the walk action is re-issued every tick and the client never settles. The counter
		// is the escape hatch: a walk the client's own router refused (the live map and the ported
		// collision map can disagree by a tile) must not wedge here forever -- after a couple of
		// seconds of no progress, re-aim.
		int chebyshev = Math.max(Math.abs(me.worldX() - WorldPointUtil.unpackWorldX(lastTarget)),
			Math.abs(me.worldY() - WorldPointUtil.unpackWorldY(lastTarget)));
		if (lastTarget != WorldPointUtil.UNDEFINED && chebyshev > 2)
		{
			if (++stuckTicks > 10)
			{
				lastTarget = WorldPointUtil.UNDEFINED;
				stuckTicks = 0;
			}
			return status(path.size());
		}
		stuckTicks = 0;

		int plane = me.plane();
		int runEnd = furthestWalkableIndex(path, plane);
		if (runEnd < 0)
		{
			// Nothing on the player's plane within the window: mid-transport or off-path. Nothing to
			// walk this tick; the plane-skip in furthestWalkableIndex recovers once the crossing is
			// done, and a path replacement re-anchors the cursor.
			return status(path.size());
		}

		int target = path.get(runEnd).getPackedPosition();
		// walkTo returns false both for a tile outside the loaded scene and -- the case that matters
		// here -- when the client's action path is unavailable (DO_ACTION not derived for this build,
		// see client/offsets.hpp). The in-scene filter above rules the first case out, so a false is
		// reported to the panel as cannot-act, never as "walking".
		lastWalkIssued = Actions.walkTo(WorldPointUtil.unpackWorldX(target),
			WorldPointUtil.unpackWorldY(target));
		if (lastWalkIssued)
		{
			lastTarget = target;
			cursor = runEnd;
			lastCursorTile = target;
		}
		return status(path.size());
	}

	/**
	 * Index of the farthest path tile that is on {@code plane} and within the loaded scene, scanning
	 * at most {@link #MAX_SCENE_REACH} tiles ahead of the cursor. -1 when there is none.
	 *
	 * <p>The scan is anchored at the cursor -- the index we last walked toward -- not at index 0. The
	 * path never shrinks as it is consumed, so anchoring at 0 meant the 30-tile window always covered
	 * the first 30 tiles of the path and auto-walk simply stopped after them.</p>
	 *
	 * <p>Leading tiles on OTHER planes are skipped, not treated as a stop: after the player climbs the
	 * stairs (which is the designed flow -- we never walk into a plane change ourselves), the cursor
	 * sits on the old plane and a stop there would wedge the walk for the rest of the path.</p>
	 */
	private int furthestWalkableIndex(List<PathStep> path, int plane)
	{
		// plane < 0 = the client could not read a plane (ENTITY_PLANE is SUSPECT this build, see
		// client/offsets.hpp; the native range-guards obvious garbage to -1). With no plane to match,
		// the filter would reject every tile and wedge the walk at "nothing on this plane" forever,
		// so it is skipped entirely: walk the farthest in-scene tile and let the scene bound be the
		// only filter.
		boolean filterPlane = plane >= 0;
		int start = cursor;
		while (start < path.size() && filterPlane
			&& WorldPointUtil.unpackWorldPlane(path.get(start).getPackedPosition()) != plane)
		{
			start++;
		}
		int best = -1;
		int baseX = Game.sceneBaseX(), baseY = Game.sceneBaseY();
		for (int i = start; i < path.size() && i < start + MAX_SCENE_REACH; i++)
		{
			int packed = path.get(i).getPackedPosition();
			int x = WorldPointUtil.unpackWorldX(packed);
			int y = WorldPointUtil.unpackWorldY(packed);
			if (filterPlane && WorldPointUtil.unpackWorldPlane(packed) != plane)
			{
				break; // next plane change: stop here, the player crosses it themselves
			}
			if (x >= baseX && x < baseX + 104 && y >= baseY && y < baseY + 104)
			{
				best = i;
			}
		}
		return best;
	}

	/** Path index of the tile nearest the player, scanning the first 100 steps. */
	private int nearestIndexToPlayer(List<PathStep> path, Local me)
	{
		int best = 0;
		int bestDist = Integer.MAX_VALUE;
		for (int i = 0; i < path.size() && i < 100; i++)
		{
			int packed = path.get(i).getPackedPosition();
			int d = Math.max(Math.abs(me.worldX() - WorldPointUtil.unpackWorldX(packed)),
				Math.abs(me.worldY() - WorldPointUtil.unpackWorldY(packed)));
			if (d < bestDist)
			{
				bestDist = d;
				best = i;
			}
		}
		return best;
	}

	private String status(int remaining)
	{
		// The last walk attempt was refused: the client has no usable action path, so saying
		// "walking" here would report movement that is not happening.
		if (!lastWalkIssued)
		{
			return "cannot act: actions unavailable on this client build";
		}
		return remaining > 1 ? "walking, " + remaining + " tiles left" : "at destination";
	}

	/** Forget the walk in progress: new path, or the plugin was disabled. */
	void reset()
	{
		lastTarget = WorldPointUtil.UNDEFINED;
		lastWalkTick = -1;
		cursor = 0;
		lastCursorTile = -1;
		stuckTicks = 0;
		lastWalkIssued = true;
	}

	/** Packed position of the tile the last walk action aimed at; UNDEFINED before the first one. */
	private int lastTarget = WorldPointUtil.UNDEFINED;
	private int lastWalkTick = -1;
	/** Path index we last walked toward; the next aim scans from here, not from the start. */
	private int cursor;
	/** The packed tile that was at {@code cursor} when we last aimed; changes when the path is replaced. */
	private int lastCursorTile = -1;
	/** Ticks spent more than 2 tiles from lastTarget without closing in; a refused walk must re-aim. */
	private int stuckTicks;
	/** Result of the last walk attempt; false once the client refuses actions, which status() reports. */
	private boolean lastWalkIssued = true;
	/** Identity of the pathfinder the cursor belongs to; a new instance resets it. */
	private Pathfinder lastPathfinder;
}

