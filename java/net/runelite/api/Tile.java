// Shim of net.runelite.api.Tile (BSD-2, RuneLite), cut to what WorldArea's line-of-sight code reads.
package net.runelite.api;

import net.runelite.api.coords.LocalPoint;

public class Tile
{
	private final int sceneX, sceneY, plane;

	Tile(int sceneX, int sceneY, int plane)
	{
		this.sceneX = sceneX;
		this.sceneY = sceneY;
		this.plane = plane;
	}

	public int getPlane()
	{
		return plane;
	}

	public Point getSceneLocation()
	{
		return new Point(sceneX, sceneY);
	}

	public LocalPoint getLocalLocation()
	{
		return new LocalPoint(sceneX << 7, sceneY << 7);
	}
}
