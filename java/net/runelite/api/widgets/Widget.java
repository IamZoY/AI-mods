// Shim of net.runelite.api.Widget (BSD-2, RuneLite), cut to what the ported plugin reads: text,
// bounds, visibility, children, and the scroll fields of the fairy-ring panel.
//
// Values come from the widget() and widgetChild() natives. Children are filled lazily, one level per
// getDynamicChildren()/getStaticChildren() call, so a per-frame getWidget() of a leaf costs no child
// enumeration at all.
package net.runelite.api.widgets;

import java.awt.Rectangle;

import net.runelite.api.Point;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class Widget
{
	public static final int MAX_CHILDREN = 0x10000;

	private final int id;
	private String text = "";
	private final Rectangle bounds = new Rectangle();
	private boolean hidden;
	/** Y as the widget stores it -- relative to its parent -- filled by ClientState. */
	private int relativeY;
	private int scrollY;
	private int scrollHeight;

	private final List<Widget> children = new ArrayList<>();
	private boolean childrenLoaded;
	/** Set by ClientState; enumerates this widget's children through the widgetChild native. */
	private Runnable childLoader;

	public Widget(int id)
	{
		this.id = id;
	}

	public int getId()
	{
		return id;
	}

	public String getText()
	{
		return text;
	}

	public void setText(String text)
	{
		this.text = text;
	}

	public Rectangle getBounds()
	{
		return bounds;
	}

	/** Filled in by ClientState from the widget() native; not a RuneLite method. */
	public void setBounds(int x, int y, int width, int height)
	{
		bounds.setBounds(x, y, width, height);
	}

	public boolean isHidden()
	{
		return hidden;
	}

	public void setHidden(boolean hidden)
	{
		this.hidden = hidden;
	}

	public int getWidth()
	{
		return bounds.width;
	}

	public int getHeight()
	{
		return bounds.height;
	}

	public Point getCanvasLocation()
	{
		return new Point(bounds.x, bounds.y);
	}

	public Point getCanvasLocation(int plane)
	{
		return getCanvasLocation();
	}

	/**
	 * The shim's one child list, seen through both RuneLite accessors. The widgetChild native exposes
	 * a single child array per widget and cannot say which of its entries the cache defined and which
	 * the game spawned at runtime, so static and dynamic callers get the same enumeration. The
	 * plugin's two consumers look for a specific text match in one list or the other, so the
	 * overlap only ever re-tests a widget, never mislabels one.
	 */
	public Widget[] getDynamicChildren()
	{
		ensureChildren();
		return children.toArray(new Widget[0]);
	}

	public Collection<Widget> getChildren()
	{
		ensureChildren();
		return children;
	}

	public Widget[] getStaticChildren()
	{
		return getDynamicChildren();
	}

	public int getScrollY()
	{
		return scrollY;
	}

	public void setScrollY(int scrollY)
	{
		this.scrollY = scrollY;
	}

	/**
	 * Derived, not read: the widget native reports geometry but no scroll state, so the content height
	 * is the furthest child edge. Good enough for "is this row scrolled out of view"; a real
	 * scrollHeight needs the same offset as setScrollY below.
	 */
	public int getScrollHeight()
	{
		ensureChildren();
		return scrollHeight;
	}

	/** Y within the parent, as the widget stores it. */
	public int getRelativeY()
	{
		return relativeY;
	}

	/** Stored and ignored: nothing in the shim draws widget text, the game does. */
	public void setTextColor(int color)
	{
		this.textColor = color;
	}

	public int getTextColor()
	{
		return textColor;
	}

	private int textColor;

	/**
	 * Stored, not applied. Writing the game's scroll position needs the client's script VM (the
	 * plugin drives it with the UPDATE_SCROLLBAR script, which runScript cannot run yet), so the
	 * stored value is what the shim believes the scroll is and the panel does not actually move.
	 */
	public void revalidateScroll()
	{
	}

	// -- shim plumbing (not RuneLite API) -----------------------------------------------------------

	/** Filled in by ClientState from the widget() native; not a RuneLite method. */
	public void setRelativeY(int y)
	{
		this.relativeY = y;
	}

	/** Derived by ClientState from the children's extents; not a RuneLite method. */
	public void setScrollHeight(int scrollHeight)
	{
		this.scrollHeight = scrollHeight;
	}

	/** Filled in by ClientState; not a RuneLite method. */
	public void setChildLoader(Runnable loader)
	{
		this.childLoader = loader;
	}

	private void ensureChildren()
	{
		if (!childrenLoaded)
		{
			childrenLoaded = true; // set first: a self-referential child must not re-enter
			if (childLoader != null)
			{
				childLoader.run();
			}
		}
	}
}
