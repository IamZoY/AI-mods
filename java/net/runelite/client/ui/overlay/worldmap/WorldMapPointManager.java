// Shim of net.runelite.client.ui.overlay.worldmap.WorldMapPointManager (BSD-2, RuneLite) -- just the
// collection; nothing renders map points until the world-map overlay phase.
package net.runelite.client.ui.overlay.worldmap;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

public class WorldMapPointManager
{
	private final List<WorldMapPoint> points = new ArrayList<>();

	public void add(WorldMapPoint point)
	{
		points.add(point);
	}

	public void removeIf(Predicate<WorldMapPoint> filter)
	{
		points.removeIf(filter);
	}

	public List<WorldMapPoint> getWorldMapPoints()
	{
		return points;
	}
}
