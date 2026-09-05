// Shim of net.runelite.api.Scene (BSD-2, RuneLite), cut to what LocalPoint and WorldArea call.
package net.runelite.api;

public class Scene
{
	static final Scene INSTANCE = new Scene();

	private static final Tile[][][] EMPTY_TILES = new Tile[Constants.MAX_Z][Constants.SCENE_SIZE][Constants.SCENE_SIZE];

	public int getBaseX()
	{
		return kewl.api.Game.sceneBaseX();
	}

	public int getBaseY()
	{
		return kewl.api.Game.sceneBaseY();
	}

	public boolean isInstance()
	{
		return false;
	}

	public int[][][] getInstanceTemplateChunks()
	{
		return new int[0][0][0];
	}

	public int getWorldViewId()
	{
		return WorldView.TOPLEVEL;
	}

	/** Tile objects are not readable yet; every slot is null, which callers already handle. */
	public Tile[][][] getTiles()
	{
		return EMPTY_TILES;
	}
}
