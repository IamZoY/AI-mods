// Shim of net.runelite.api.worldmap.WorldMap (BSD-2, RuneLite), adapted to KewlKlient.
//
// kewl.rl.Events pushes the map's centre here every frame, read from the native world-map object: the
// centre tile in world tiles is WM_ORIGIN + 48, which is the same quantity as 8 * WM_CENTRE (the
// scroll ints are in 8-tile units -- see the WM_ findings in client/offsets.hpp). isLive() says
// whether that data has actually arrived: the marker renderer and the plugin's map-click path gate on
// it, because acting on a stale centre turns a map click into a target somewhere the user did not
// click and auto-walk then walks there. The map overlays themselves run off the map widget being open;
// against a stale centre they would draw in the wrong place, and until the data is live nothing
// pushes a real one.
//
// The zoom is NOT derived. The client provably has no zoom field on the world-map object (no "zoom"
// Lua binding, and the object's trailing fields are fully accounted for by the centre/scroll data),
// so PLACEHOLDER_PIXELS_PER_TILE below is a placeholder, not a measurement: it only makes the plugin's own
// pixel<->tile maths self-consistent (a click converts to a tile and back to the same pixel). It does
// NOT match the client's real pixels-per-tile, so anything drawn is anchored at the right centre but
// at the plugin's own scale. Deriving the real scale needs a live measurement with the map open.
package net.runelite.api.worldmap;

import net.runelite.api.Point;
import net.runelite.api.coords.WorldPoint;

public class WorldMap
{
	public static final WorldMap INSTANCE = new WorldMap();

	/**
	 * Placeholder pixels-per-tile. Not derived from the client -- see the header. Kept as a constant
	 * only so the plugin's projection maths can run at all; do not trust the scale it produces.
	 */
	private static final float PLACEHOLDER_PIXELS_PER_TILE = 4.0f;

	private volatile Point position = new Point(0, 0);
	private volatile float zoom = PLACEHOLDER_PIXELS_PER_TILE;
	private volatile boolean live = false;

	public Point getWorldMapPosition()
	{
		return position;
	}

	public float getWorldMapZoom()
	{
		return zoom;
	}

	/** True once Events has pushed a centre read from the live world-map object; false until then. */
	public boolean isLive()
	{
		return live;
	}

	/**
	 * Called by Events each frame with the map's centre in world tiles. The zoom argument carries the
	 * placeholder through: there is nothing real to put in it yet.
	 */
	public void set(Point position, float zoom)
	{
		this.position = position;
		this.zoom = zoom;
		this.live = true;
	}

	/**
	 * The world-map object is gone (Events' native returned nothing), so the centre last pushed is
	 * stale. Overlays and map clicks gate on {@link #isLive()} and must not act on it.
	 */
	public void clear()
	{
		live = false;
	}
}
