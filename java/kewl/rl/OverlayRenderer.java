// Draws a registered RuneLite overlay set into kewl's single Graphics2D.
//
// Layering collapses: the layered window is above everything the game draws, so UNDER_WIDGETS and
// ABOVE_SCENE end up in the same pass no matter what we do. The values still order overlays relative
// to each other, and OverlayPanel positions pile up per corner so two panels do not overlap.
//
// After the plugin overlay pass a WorldMapPoint marker pass runs: plugins push markers into a
// WorldMapPointManager (Shortest Path's target marker is the only user), and nothing else renders
// them. See renderWorldMapPoints for what that pass does and does not promise.
package kewl.rl;

import java.awt.Dimension;
import java.awt.Graphics2D;
import java.awt.Point;
import java.awt.Rectangle;
import java.awt.geom.Ellipse2D;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;

import net.runelite.api.Client;
import net.runelite.api.Perspective;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.gameval.InterfaceID;
import net.runelite.api.widgets.Widget;
import net.runelite.client.ui.overlay.Overlay;
import net.runelite.client.ui.overlay.OverlayLayer;
import net.runelite.client.ui.overlay.OverlayManager;
import net.runelite.client.ui.overlay.OverlayPanel;
import net.runelite.client.ui.overlay.OverlayPosition;
import net.runelite.client.ui.overlay.worldmap.WorldMapPoint;
import net.runelite.client.ui.overlay.worldmap.WorldMapPointManager;

final class OverlayRenderer
{
	private static final Comparator<Overlay> ORDER = Comparator
		.comparingInt(OverlayRenderer::layerRank)
		.thenComparing(o -> -o.getPriority());

	private OverlayRenderer()
	{
	}

	static void render(Graphics2D g, OverlayManager manager)
	{
		List<Overlay> overlays = new ArrayList<>(manager.getOverlays());
		overlays.sort(ORDER);

		Rectangle clipped = g.getClipBounds();
		final Rectangle bounds = clipped == null ? new Rectangle(0, 0, 765, 503) : clipped;

		// Per-position running offset so panels stacking in one corner do not overlap.
		Map<OverlayPosition, Point> cursors = new EnumMap<>(OverlayPosition.class);

		for (Overlay o : overlays)
		{
			Graphics2D og = (Graphics2D) g.create();
			try
			{
				if (o instanceof OverlayPanel)
				{
					Point at = cursors.computeIfAbsent(o.getPosition(), p -> baseAt(p, bounds));
					og.translate(at.x, at.y);
					Dimension d = o.render(og);
					if (d != null)
					{
						at.y += d.height + 6;
					}
				}
				else
				{
					o.render(og);
				}
			}
			catch (Throwable t)
			{
				System.out.println("[overlay:" + o.getName() + "] render threw: " + t);
			}
			finally
			{
				og.dispose();
			}
		}

		renderWorldMapPoints(g);
	}

	/**
	 * The WorldMapPoint markers plugins added. Two surfaces:
	 *
	 * World map: drawn only when the map centre data is live (WorldMap.isLive) -- a gate the plugin's
	 * own map overlays do NOT share (they draw whenever the map widget is open, per WorldMap's
	 * header), so a marker can be absent while the path drawing is up. Drawing a marker against a
	 * stale centre would put the target somewhere the user did not pick. The pixel mapping mirrors
	 * ShortestPathPlugin.mapWorldPointToGraphicsPointX/Y (RuneLite's own world-map-overlay maths,
	 * anchored on the map centre and the MAP_CONTAINER bounds).
	 *
	 * Minimap: the same Perspective.localToMinimap projection PathMinimapOverlay uses, clipped to the
	 * minimap draw widget's ellipse.
	 *
	 * The images come off the points themselves, which the plugins load once at startup -- nothing is
	 * loaded here per frame. Points with a null image are skipped.
	 */
	private static void renderWorldMapPoints(Graphics2D g)
	{
		WorldMapPointManager manager = WorldMapPointManager.get();
		if (manager == null)
		{
			return; // no plugin built yet, so no markers exist
		}

		Client client = Client.get();
		List<WorldMapPoint> points = manager.getWorldMapPoints();
		if (points.isEmpty())
		{
			return;
		}

		Widget map = client.getWidget(InterfaceID.Worldmap.MAP_CONTAINER);
		if (map != null && client.getWorldMap().isLive())
		{
			Rectangle rect = map.getBounds();
			net.runelite.api.Point centre = client.getWorldMap().getWorldMapPosition();
			float pixelsPerTile = client.getWorldMap().getWorldMapZoom();
			int widthInTiles = (int) Math.ceil(rect.getWidth() / pixelsPerTile);
			int heightInTiles = (int) Math.ceil(rect.getHeight() / pixelsPerTile);

			Graphics2D mg = (Graphics2D) g.create();
			mg.setClip(rect);
			for (WorldMapPoint p : points)
			{
				BufferedImage image = p.getImage();
				WorldPoint wp = p.getWorldPoint();
				if (image == null || wp == null)
				{
					continue;
				}

				// Same conversion as ShortestPathPlugin.mapWorldPointToGraphicsPointX/Y: the map
				// centre sits at the middle of MAP_CONTAINER, one zoom pixel per tile.
				int xTileOffset = wp.getX() + widthInTiles / 2 - centre.getX();
				int x = (int) (xTileOffset * pixelsPerTile)
					+ (int) (pixelsPerTile - Math.ceil(pixelsPerTile / 2))
					+ (int) rect.getX();
				int yTileOffset = (centre.getY() - heightInTiles / 2 - wp.getY() - 1) * -1;
				int y = rect.height - (int) (yTileOffset * pixelsPerTile)
					+ (int) (pixelsPerTile - Math.ceil(pixelsPerTile / 2))
					+ (int) rect.getY();

				mg.drawImage(image, x - image.getWidth() / 2, y - image.getHeight() / 2, null);
			}
			mg.dispose();
		}

		// Minimap: the same projection PathMinimapOverlay uses (which rotates with the camera and
		// approximates the game's own minimap rotation -- accepted there, so accepted here too).
		Widget minimap = client.getMinimapDrawWidget();
		if (minimap == null || minimap.isHidden())
		{
			return;
		}
		int plane = client.getTopLevelWorldView().getPlane();
		Graphics2D mng = (Graphics2D) g.create();
		// The clip is the widget's bounding ellipse, PathMinimapOverlay's fallback shape: the mask
		// sprites it prefers are not reachable from here.
		mng.setClip(new Ellipse2D.Double(minimap.getBounds().getX(), minimap.getBounds().getY(),
			minimap.getBounds().getWidth(), minimap.getBounds().getHeight()));
		for (WorldMapPoint p : points)
		{
			BufferedImage image = p.getImage();
			WorldPoint wp = p.getWorldPoint();
			if (image == null || wp == null)
			{
				continue;
			}
			LocalPoint lp = LocalPoint.fromWorld(client, wp);
			if (lp == null || wp.getPlane() != plane)
			{
				continue;
			}
			net.runelite.api.Point pos = Perspective.localToMinimap(client, lp);
			if (pos == null)
			{
				continue;
			}
			mng.drawImage(image, pos.getX() - image.getWidth() / 2, pos.getY() - image.getHeight() / 2, null);
		}
		mng.dispose();
	}

	/** Where a panel in {@code position} starts drawing, and in which direction the stack grows. */
	private static Point baseAt(OverlayPosition position, Rectangle bounds)
	{
		switch (position)
		{
			case TOP_LEFT: return new Point(8, 8);
			case TOP_CENTER: return new Point(bounds.x + (bounds.width - 145) / 2, 8);
			case TOP_RIGHT: return new Point(bounds.x + bounds.width - 160, 8);
			case BOTTOM_LEFT: return new Point(8, bounds.y + bounds.height - 200);
			case BOTTOM_RIGHT: return new Point(bounds.x + bounds.width - 160, bounds.y + bounds.height - 200);
			case ABOVE_CHATBOX_RIGHT: return new Point(bounds.x + bounds.width - 160, 8);
			default: return new Point(8, 8);
		}
	}

	private static int layerRank(Overlay o)
	{
		switch (o.getLayer())
		{
			case UNDER_WIDGETS: return 0;
			case ABOVE_SCENE: return 1;
			case ABOVE_WIDGETS: return 2;
			case MANUAL: return 3;
			case ALWAYS_ON_TOP:
			default: return 4;
		}
	}
}
