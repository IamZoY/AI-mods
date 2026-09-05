// Shim of net.runelite.api.Player (BSD-2, RuneLite), cut to what the ported plugin reads off the
// local player: local location, world location, world view.
package net.runelite.api;

import java.util.function.Supplier;

import kewl.api.Game;
import kewl.api.Local;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;

public class Player
{
	// A supplier, not a captured Local: kewl.api.Game.refresh() REPLACES its Local each frame, so a
	// Player built once at class-load with `new Player(Game.me())` would be a frozen snapshot of the
	// player before the game ever refreshed -- every caller would see tile (0,0) forever.
	private final Supplier<Local> local;

	Player(Local local)
	{
		this(() -> local);
	}

	Player(Supplier<Local> local)
	{
		this.local = local;
	}

	private Local now()
	{
		Local l = local.get();
		return l == null ? Local.ABSENT : l;
	}

	/**
	 * The tile CENTRE, not the south-west corner: the fine-coordinate position RuneLite callers anchor
	 * labels and projections on. Shifting by half a tile (64 fine units) is the difference between a
	 * label sitting on the player and one sitting on the tile's corner.
	 */
	public LocalPoint getLocalLocation()
	{
		Local l = now();
		return new LocalPoint((l.sceneX() << 7) + 64, (l.sceneY() << 7) + 64);
	}

	public WorldPoint getWorldLocation()
	{
		Local l = now();
		return new WorldPoint(l.worldX(), l.worldY(), l.plane());
	}

	public WorldView getWorldView()
	{
		return WorldView.TOP_LEVEL;
	}

	public String getName()
	{
		// Live as of client-240-6: the local player's heap NxtString at entity+0x718. May contain
		// U+00A0 where the game pads; Text.removeNames comparisons should fold that to a space.
		return now().exists() ? kewl.Natives.entityName(now().uid()) : "";
	}

	Local local()
	{
		return now();
	}

	static final Player ABSENT = new Player(Local.ABSENT)
	{
		@Override
		public LocalPoint getLocalLocation()
		{
			return new LocalPoint(0, 0);
		}

		@Override
		public WorldPoint getWorldLocation()
		{
			return new WorldPoint(0, 0, 0);
		}
	};
}
