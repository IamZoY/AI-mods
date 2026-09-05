// Shim of net.runelite.api.WorldView (BSD-2, RuneLite), cut to what the vendored coord classes and
// the ported plugin call.
//
// There is one world view here -- the loaded scene around the player, via kewl.api.Game. Instance
// views (raids, boats) are not readable yet, so isInstance() is false and the template chunks are
// empty: WorldPointUtil.fromLocalInstance collapses to plain world coordinates.
package net.runelite.api;

import kewl.api.Game;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;

public class WorldView
{
	public static final int TOPLEVEL = -1;

	private static final int[][][] NO_TEMPLATE_CHUNKS = new int[0][0][0];

	private final int id;

	WorldView(int id)
	{
		this.id = id;
	}

	static final WorldView TOP_LEVEL = new WorldView(TOPLEVEL);

	public int getId()
	{
		return id;
	}

	public int getBaseX()
	{
		return Game.sceneBaseX();
	}

	public int getBaseY()
	{
		return Game.sceneBaseY();
	}

	public int getPlane()
	{
		return Game.me().plane();
	}

	public int getSizeX()
	{
		return Constants.SCENE_SIZE;
	}

	public int getSizeY()
	{
		return Constants.SCENE_SIZE;
	}

	public boolean isInstance()
	{
		return false;
	}

	public int[][][] getInstanceTemplateChunks()
	{
		return NO_TEMPLATE_CHUNKS;
	}

	public boolean contains(WorldPoint point)
	{
		if (point == null)
		{
			return false;
		}
		return Game.toScene(point.getX(), point.getY()) != null;
	}

	public boolean contains(LocalPoint point)
	{
		return point != null;
	}

	/**
	 * The tile the mouse is over, computed rather than read: the game's selected-tile field sits
	 * behind the scene-click code that is not RE'd yet (the menu native, Phase D). The projection
	 * native only goes world->screen, so this walks the scene, projects every tile centre at ground
	 * height, and keeps the one nearest the cursor -- within a tile-sized radius, so a cursor over
	 * UI or the sky yields null rather than the closest tile anyway.
	 *
	 * <p>Honest degradations, both shared with the overlays: no heightmap (Perspective.getTileHeight
	 * is 0) so on slopes and upper floors the picked tile is the one whose ground projection is
	 * nearest, and the plane is the local player's, not the cursor's. Cached per frame because the
	 * plugin asks for it from both the menu-entry path and the click path.</p>
	 */
	public Tile getSelectedSceneTile()
	{
		int frame = kewl.KewlKlient.frame();
		if (cachedTileFrame == frame)
		{
			return cachedTile;
		}

		cachedTileFrame = frame;
		cachedTile = null;

		Point mouse = Client.get().state().getMouseCanvasPosition();
		if (mouse == null)
		{
			return null;
		}

		int plane = getPlane();
		int size = Constants.SCENE_SIZE;
		Tile best = null;
		long bestDist = Long.MAX_VALUE;
		for (int x = 0; x < size; x++)
		{
			for (int y = 0; y < size; y++)
			{
				java.awt.Point p = Game.projectTile(x, y);
				if (p == null)
				{
					continue;
				}
				long dx = p.x - mouse.getX();
				long dy = p.y - mouse.getY();
				long dist = dx * dx + dy * dy;
				if (dist < bestDist)
				{
					bestDist = dist;
					best = new Tile(x, y, plane);
				}
			}
		}

		// Tiles project 30-100 px apart depending on zoom; half a max tile on screen is generous
		// without letting a cursor parked on the minimap pick a scene tile.
		if (best != null && bestDist <= 100L * 100L)
		{
			cachedTile = best;
		}
		return cachedTile;
	}

	/** Frame token and result of the last tile-under-cursor scan; kewl's frame counter, not game state. */
	private int cachedTileFrame = -1;
	private Tile cachedTile;

	/** Waiting on: boat/passed-world entities. Empty means "no boat offset", paths walk from the shore. */
	public WorldEntities worldEntities()
	{
		return WorldEntities.EMPTY;
	}

	/**
	 * Collision flags per tile. Not readable from the game yet; zeros mean "no walls", which is the
	 * honest default: everything looks walkable to WorldArea's line-of-sight helpers, and real
	 * walkability comes from the plugin's own bundled collision map instead.
	 */
	public CollisionData[] getCollisionMaps()
	{
		return CollisionData.EMPTY;
	}

	public Scene getScene()
	{
		return Scene.INSTANCE;
	}
}
