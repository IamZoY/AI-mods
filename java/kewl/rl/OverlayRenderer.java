// Draws a registered RuneLite overlay set into kewl's single Graphics2D.
//
// Layering collapses: the layered window is above everything the game draws, so UNDER_WIDGETS and
// ABOVE_SCENE end up in the same pass no matter what we do. The values still order overlays relative
// to each other, and OverlayPanel positions pile up per corner so two panels do not overlap.
package kewl.rl;

import java.awt.Dimension;
import java.awt.Graphics2D;
import java.awt.Point;
import java.awt.Rectangle;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.EnumMap;
import java.util.List;
import java.util.Map;

import net.runelite.client.ui.overlay.Overlay;
import net.runelite.client.ui.overlay.OverlayLayer;
import net.runelite.client.ui.overlay.OverlayManager;
import net.runelite.client.ui.overlay.OverlayPanel;
import net.runelite.client.ui.overlay.OverlayPosition;

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
