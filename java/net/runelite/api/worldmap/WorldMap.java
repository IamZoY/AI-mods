// Shim of net.runelite.api.worldmap.WorldMap (BSD-2, RuneLite).
//
// The world map's centre position and zoom come from the worldMap() native (Phase D). Until then the
// position is (0,0) and the zoom is meaningless, so the map overlay stays disabled rather than draw
// in the wrong place.
package net.runelite.api.worldmap;

import net.runelite.api.Point;
import net.runelite.api.coords.WorldPoint;

public class WorldMap
{
	public static final WorldMap INSTANCE = new WorldMap();

	private volatile Point position = new Point(0, 0);
	private volatile float zoom = 4.0f;

	public Point getWorldMapPosition()
	{
		return position;
	}

	public float getWorldMapZoom()
	{
		return zoom;
	}

	/** Called by the bridge once the worldMap() native exists. */
	public void set(Point position, float zoom)
	{
		this.position = position;
		this.zoom = zoom;
	}
}
