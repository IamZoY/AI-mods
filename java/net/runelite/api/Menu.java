// Shim of net.runelite.api.Menu (BSD-2, RuneLite).
//
// Entries created here are held by the shim; the menu() native (Phase D) injects them into the
// client's own right-click menu and reports clicks back, at which point the stored onClick callbacks
// fire. Until that native exists the entries simply queue -- creating one is harmless.
package net.runelite.api;

import java.util.ArrayList;
import java.util.List;

public class Menu
{
	public static final Menu INSTANCE = new Menu();

	private final List<MenuEntry> pending = new ArrayList<>();

	/** Entries waiting to be injected into the client's menu (or never, pre-Phase D). */
	public List<MenuEntry> getPending()
	{
		return pending;
	}

	public MenuEntry[] getMenuEntries()
	{
		return pending.toArray(new MenuEntry[0]);
	}

	public void setMenuEntries(MenuEntry[] entries)
	{
		pending.clear();
		for (MenuEntry e : entries)
		{
			pending.add(e);
		}
	}

	public MenuEntry createMenuEntry(int index)
	{
		MenuEntry entry = new MenuEntry();
		if (index >= 0 && index <= pending.size())
		{
			pending.add(index, entry);
		}
		else
		{
			pending.add(entry);
		}
		return entry;
	}

	public MenuEntry createMenuEntry(MenuAction action)
	{
		return createMenuEntry(-1).setType(action);
	}

	public void removeMenuEntry(MenuEntry entry)
	{
		pending.remove(entry);
	}

	public void clear()
	{
		pending.clear();
	}
}
