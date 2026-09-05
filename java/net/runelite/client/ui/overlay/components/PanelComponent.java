// Shim of net.runelite.client.ui.overlay.components.PanelComponent (BSD-2, RuneLite).
//
// Children are laid out by translating the Graphics2D down the column, so any child implementation
// (ours, or a plugin's minimal one that ignores setPreferredLocation) draws where it should. The
// background is drawn first using the height measured last frame -- one frame late on the very first
// render, exact after that.
package net.runelite.client.ui.overlay.components;

import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics2D;
import java.awt.Rectangle;
import java.util.ArrayList;
import java.util.List;

import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class PanelComponent implements LayoutableRenderableEntity
{
	private final List<LayoutableRenderableEntity> children = new ArrayList<>();

	@Getter
	private Dimension preferredSize = new Dimension(ComponentConstants.STANDARD_WIDTH, 0);

	@Getter
	private Color backgroundColor = new Color(0, 0, 0, 150);

	@Getter
	private Color borderColor = new Color(255, 255, 255, 60);

	private final Rectangle bounds = new Rectangle();

	private int lastHeight = 0;

	private boolean wipeChildren = true;

	@Override
	public Dimension render(Graphics2D graphics)
	{
		final int width = preferredSize == null ? ComponentConstants.STANDARD_WIDTH
			: preferredSize.width;

		if (lastHeight > 0)
		{
			final Color old = graphics.getColor();
			graphics.setColor(backgroundColor);
			graphics.fillRect(bounds.x, bounds.y, width, lastHeight);
			graphics.setColor(borderColor);
			graphics.drawRect(bounds.x, bounds.y, width, lastHeight);
			graphics.setColor(old);
		}

		int y = bounds.y;
		int widest = 0;

		for (LayoutableRenderableEntity child : children)
		{
			child.setPreferredSize(new Dimension(width, 0));
			final Graphics2D childG = (Graphics2D) graphics.create();
			childG.translate(bounds.x, y);
			final Dimension dim = child.render(childG);
			childG.dispose();
			y += dim == null ? 0 : dim.height;
			widest = Math.max(widest, dim == null ? 0 : dim.width);
		}

		final Dimension dim = new Dimension(Math.max(widest, width), y - bounds.y);
		bounds.setSize(dim);
		lastHeight = dim.height;
		if (wipeChildren)
		{
			children.clear();
		}
		return dim;
	}

	@Override
	public Rectangle getBounds()
	{
		return bounds;
	}

	/** RuneLite also allows setting just the width; the height stays measured from the children. */
	public void setPreferredSize(int width)
	{
		this.preferredSize = new Dimension(width, 0);
	}
}
