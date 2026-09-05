// Shim of net.runelite.api.Perspective (BSD-2, RuneLite), adapted to KewlKlient.
//
// The math that is ours (minimap projection) is ported verbatim. The part that is the game's (screen
// projection) goes through kewl's native projectFine, which calls the game's own worldToScreen -- so
// the game's camera maths stays the single source of truth. There is no heightmap yet, so
// getTileHeight is always 0 and upper floors draw at ground height.
package net.runelite.api;

import net.runelite.api.widgets.Widget;
import java.awt.Polygon;

import javax.annotation.Nullable;

import net.runelite.api.coords.LocalPoint;

public class Perspective
{
	public static final double UNIT = 0.0030679615d; // ~pi/1024
	public static final double UNIT14 = 3.834951969714103E-4D; // ~pi/8192

	public static final int LOCAL_COORD_BITS = 7;
	public static final int LOCAL_TILE_SIZE = 1 << LOCAL_COORD_BITS;
	public static final int LOCAL_HALF_TILE_SIZE = LOCAL_TILE_SIZE / 2;

	public static final int SCENE_SIZE = Constants.SCENE_SIZE;

	private static final int[] SINE14 = new int[16384];
	private static final int[] COSINE14 = new int[16384];

	static
	{
		for (int i = 0; i < SINE14.length; i++)
		{
			SINE14[i] = (int) Math.round(65536d * Math.sin(UNIT14 * i));
			COSINE14[i] = (int) Math.round(65536d * Math.cos(UNIT14 * i));
		}
	}

	private Perspective()
	{
	}

	@Nullable
	public static Point localToCanvas(Client client, LocalPoint point, int plane)
	{
		return localToCanvas(client, point, plane, 0);
	}

	/**
	 * No heightmap yet, so {@code plane} is ignored and every level projects at ground height.
	 */
	@Nullable
	public static Point localToCanvas(Client client, LocalPoint point, int plane, int heightOffset)
	{
		int tileHeight = getTileHeight(client, point, plane);
		return localToCanvas(client, point.getX(), point.getY(), tileHeight - heightOffset);
	}

	/**
	 * The fine coordinate units of LocalPoint (1/128 tile) are exactly what kewl's projection native
	 * takes, so this is a direct hand-off.
	 */
	@Nullable
	public static Point localToCanvas(Client client, int x, int y, int z)
	{
		java.awt.Point fine = kewl.api.Game.projectFine(x, z, y);
		return fine == null ? null : new Point(fine.x, fine.y);
	}

	public static int getTileHeight(Client client, LocalPoint point, int plane)
	{
		return 0; // no heightmap until the scene tile heights are readable
	}

	@Nullable
	public static Polygon getCanvasTilePoly(Client client, LocalPoint localLocation)
	{
		return getCanvasTilePoly(client, localLocation, 0);
	}

	@Nullable
	public static Polygon getCanvasTilePoly(Client client, LocalPoint localLocation, int plane)
	{
		int sceneX = localLocation.getSceneX();
		int sceneY = localLocation.getSceneY();
		return kewl.api.Game.tileOutline(sceneX, sceneY);
	}

	@Nullable
	public static Point localToMinimap(Client client, LocalPoint point)
	{
		final int r = 20 << LOCAL_COORD_BITS;
		final double s = 4d / client.getMinimapZoom();
		return localToMinimap(client, point, (int) (r * s));
	}

	/**
	 * Ported from RuneLite, with the camera focus pinned to the local player (the camera-focus entity
	 * is not readable yet) and the minimap draw widget resolved the same way the plugin does it.
	 */
	@Nullable
	public static Point localToMinimap(Client client, LocalPoint point, int distance)
	{
		LocalPoint focus = client.getLocalPlayer().getLocalLocation();

		final int dx = point.getX() - focus.getX();
		final int dy = point.getY() - focus.getY();
		if (dx * dx + dy * dy >= distance * distance)
		{
			return null;
		}

		Widget minimapDrawWidget = client.getMinimapDrawWidget();
		if (minimapDrawWidget == null || minimapDrawWidget.isHidden())
		{
			return null;
		}

		final double zoom = client.getMinimapZoom() / LOCAL_TILE_SIZE;
		final int x = (int) (dx * zoom);
		final int y = (int) (dy * zoom);

		final int angle = client.getCameraYawTarget() & 0x3fff;

		final int sin = SINE14[angle];
		final int cos = COSINE14[angle];

		final int rx = cos * x + sin * y >> 16;
		final int ry = sin * x - cos * y >> 16;

		Point loc = minimapDrawWidget.getCanvasLocation();
		int miniMapX = loc.getX() + minimapDrawWidget.getWidth() / 2 + rx;
		int miniMapY = loc.getY() + minimapDrawWidget.getHeight() / 2 + ry;
		return new Point(miniMapX, miniMapY);
	}
}
