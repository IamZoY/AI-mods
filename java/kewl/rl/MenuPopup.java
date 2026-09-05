// The right-click menu, drawn by us.
//
// The plan's menu design injects entries into the game's own right-click menu through a menu native
// -- but the menu struct is still unread (DO_ACTION stays 0 until hook-and-log verifies it), so
// nothing can read where the game's menu is, never mind write into it. This is that design's
// documented fallback: we detect the right-click ourselves from the input() snapshot, fire the same
// MenuOpened/MenuEntryAdded events RuneLite would fire so plugins create their entries through the
// normal API, and then draw the RUNELITE-type entries ourselves. A click invokes the entry's onClick
// callback -- exactly the path the injected-menu design would take once it exists, so plugins cannot
// tell the difference.
//
// What this cannot do, and does not pretend to: show the game's own entries ("Examine", whatever the
// cursor is over) -- we never see them -- and block the game from also handling the right-click. The
// game draws its own menu underneath ours. Assumed, not verified: we produce no click records, but
// the game also receives these clicks, and how NXT selects its own menu row from real mouse messages
// -- including whether it can pick a row that is underneath ours -- is unverified.
//
// The one game entry we fake is "Walk here": the popup only opens when a plugin contributed rows, and
// a menu that says "Walk here / Set target" whose Walk here closes it and does nothing reads as
// broken. So our Walk here walks to the tile the right-click landed on, the same thing the game's
// own would have done. A plain right-click with no plugin rows still opens nothing of ours -- the
// game's menu is already there and already walks, and ours would only cover its Examine.
package kewl.rl;

import java.awt.Color;
import java.awt.Font;
import java.awt.Graphics2D;
import java.util.ArrayList;
import java.util.List;

import kewl.Natives;
import kewl.api.Actions;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.Menu;
import net.runelite.api.MenuAction;
import net.runelite.api.MenuEntry;
import net.runelite.api.Point;
import net.runelite.api.Tile;
import net.runelite.api.events.MenuEntryAdded;
import net.runelite.api.events.MenuOpened;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.util.Text;

public class MenuPopup
{
	private static final int ROW_H = 22;
	private static final int PAD_X = 10;
	private static final Color BACKGROUND = new Color(30, 30, 34);
	private static final Color BORDER = new Color(70, 70, 78);
	private static final Color HOVER = new Color(60, 60, 68);
	private static final Color TEXT = new Color(230, 230, 235);

	private final EventBus eventBus;

	private boolean open;
	private int x, y;                        // popup top-left in canvas coordinates
	private int hover = -1;
	private boolean prevRbutton, prevLbutton;
	/** Entries this popup shows: the RUNELITE-type entries created in response to MenuEntryAdded. */
	private List<MenuEntry> entries = List.of();
	/**
	 * The tile under the cursor when the menu opened, which is what "Walk here" means. Re-picking at
	 * click time would not do: a click on row N lands 20+ px below where the right-click did, and the
	 * tile picker's 100 px radius is happy to call that the neighbouring tile. A copy is parked in
	 * the ClientState when the menu opens (see open()), so WorldView.getSelectedSceneTile() answers
	 * with this tile for as long as the menu is up -- plugin row clicks resolve their world point
	 * through that same path.
	 */
	private Tile walkTile;

	public MenuPopup(EventBus eventBus)
	{
		this.eventBus = eventBus;
	}

	/** One frame of input edge detection and click dispatch. Called once per plugin per frame. */
	public void tick()
	{
		var state = Client.get().state();
		// isMenuOpen() for this frame: the game's own menu-open flag is unread, and the only menu
		// with entries in it right now is this one. Only an OPEN popup may raise the flag -- the
		// registry can hold several RlitePlugins, each with its own MenuPopup, and a closed one must
		// not clear the flag another plugin's open popup raised this frame. close() lowers it, and
		// only when it was actually open (an onDisable force-close of a not-open popup must not
		// kick the flag out from under a popup that belongs to a different plugin).
		if (open)
		{
			state.setPopupMenuOpen(true);
		}
		boolean rbutton = state.rbuttonDown();
		boolean lbutton = state.lbuttonDown();

		if (state.isMenuOpen() && !open)
		{
			// Another plugin's popup owns the open menu. Stand down for this frame -- opening a
			// second menu over it (and re-posting MenuOpened with our entries) would duplicate
			// rows -- but keep the edge trackers current so a fresh right-click after it closes
			// still opens ours.
			prevRbutton = rbutton;
			prevLbutton = lbutton;
			return;
		}

		if (open)
		{
			Point mouse = state.getMouseCanvasPosition();
			hover = rowAt(mouse);

			if (lbutton && !prevLbutton)
			{
				MenuEntry clicked = (hover >= 0 && hover < entries.size()) ? entries.get(hover) : null;
				if (clicked != null)
				{
					try
					{
						// Invoke while the menu is still marked open and the captured tile is still
						// parked: the plugin resolves its world point through
						// getSelectedSceneTile(), which answers with the tile from open() for
						// exactly this reason -- at click time the cursor sits on row N, tens of
						// pixels below where the right-click did.
						if (clicked.getOnClick() != null)
						{
							clicked.getOnClick().accept(clicked);
						}
					}
					catch (Throwable t)
					{
						System.out.println("[menu] onClick threw: " + t);
					}
					close();
				}
				else
				{
					close();                     // a click anywhere else dismisses, like the game's menu
				}
			}
			else if (rbutton && !prevRbutton)
			{
				close();                         // a second right-click closes it; opening again waits for a fresh press after release
			}
			else
			{
				for (int vk : state.keyEdges())
				{
					if (vk == 27)                // VK_ESCAPE
					{
						close();
						break;
					}
				}
			}
		}
		else if (rbutton && !prevRbutton && overCanvas(state))
		{
			open(state);
		}

		prevRbutton = rbutton;
		prevLbutton = lbutton;
	}

	private void open(net.runelite.api.ClientState state)
	{
		Menu menu = Client.get().getMenu();
		entries = List.of();
		// Capture the tile here, while the right-click coordinates are still the true ones -- the
		// menu-open flag is not up yet, so the picker still scans. By the time a row click
		// dispatches, the cursor is on the row, and the picker would name a neighbour tile or
		// nothing. The same tile goes to the ClientState, where WorldView.getSelectedSceneTile()
		// reads it back from for every frame the menu is open (it also runs the plugin's own world
		// point resolution off that path).
		walkTile = Client.get().getTopLevelWorldView().getSelectedSceneTile();
		state.setMenuOpenedTile(walkTile);

		// A fresh menu, then the same events the game's menu opening would fire. Both posts are
		// synchronous, so by the time open() returns, plugin-created entries are in pending.
		menu.setMenuEntries(new MenuEntry[0]);
		eventBus.post(new MenuOpened());

		// The plugin keys its "Set target" off a WALK-type entry, so the event carries one; the entry
		// itself also gets an onClick, because when it is shown as a row it has to walk, not dismiss.
		MenuEntry walk = new MenuEntry()
			.setOption("Walk here")
			.setType(MenuAction.WALK)
			.onClick(this::walkHere);
		eventBus.post(new MenuEntryAdded(walk));

		List<MenuEntry> ours = new ArrayList<>();
		for (MenuEntry e : menu.getPending())
		{
			if (e.getOnClick() != null)
			{
				ours.add(e);
			}
		}
		if (ours.isEmpty())
		{
			// Nothing to show: undo the capture above, or a Tile from this scene stays parked in the
			// ClientState across scene reloads and world hops. close() is the normal un-parker, but
			// the popup is about to not open at all, so nothing else would clear it.
			state.setMenuOpenedTile(null);
			walkTile = null;
			return;                              // nothing to show; stay closed
		}
		// Game-menu order: Walk here above whatever the plugins added.
		ours.add(0, walk);
		entries = ours;

		Point mouse = state.getMouseCanvasPosition();
		x = mouse.getX();
		y = mouse.getY();
		open = true;
		hover = -1;
	}

	/**
	 * Dismiss the popup and clear everything that outlives a frame of it: the entries, the captured
	 * tile, and the ClientState flag and tile. Public because it is also called from outside the
	 * tick loop -- RlitePlugin.onDisable must force-close, or a popup left up when the plugin is
	 * switched off ticks no more and holds isMenuOpen() (and the stale captured tile) forever.
	 */
	public void close()
	{
		boolean wasOpen = open;
		open = false;
		entries = List.of();
		hover = -1;
		walkTile = null;
		// Lower the flag only if this popup is the one that raised it: with several RlitePlugins
		// alive, a force-close from onDisable of a NOT-open popup must not clear another plugin's
		// open popup's flag (its own next tick re-raises it, but the frame in between renders with
		// the tile-parking bypass silently off).
		if (wasOpen)
		{
			Client.get().state().setPopupMenuOpen(false);
			Client.get().state().setMenuOpenedTile(null);
		}
	}

	/**
	 * The synthetic "Walk here": walk to the tile the right-click landed on, which is what the game's
	 * own entry would have done. Guards, each a real dead end rather than an error: not logged in, or
	 * the cursor was over UI/sky so no scene tile is under it (then there was nothing to walk to and
	 * the game's own menu would have been equally empty of options).
	 */
	private void walkHere(MenuEntry entry)
	{
		if (Client.get().getGameState() != GameState.LOGGED_IN || walkTile == null)
		{
			return;
		}
		Point scene = walkTile.getSceneLocation();
		// Scene tiles are relative to the loaded scene's corner; Actions.walkTo wants world tiles and
		// answers false itself when the tile has scrolled out of the scene since the menu opened.
		Actions.walkTo(kewl.api.Game.sceneBaseX() + scene.getX(),
			kewl.api.Game.sceneBaseY() + scene.getY());
	}

	/** Draw the popup. Runs inside the plugin's render pass, in canvas coordinates. */
	public void render(Graphics2D g)
	{
		if (!open || entries.isEmpty())
		{
			return;
		}

		int[] view = Natives.viewport();
		if (view.length == 4)
		{
			// Keep the menu on the canvas: a menu hanging off the edge is unusable half off-screen.
			int w = entryWidth(g) + PAD_X * 2;
			int h = entries.size() * ROW_H + 4;
			if (x + w > view[2])
			{
				x = Math.max(0, view[2] - w);
			}
			if (y + h > view[3])
			{
				y = Math.max(0, view[3] - h);
			}
		}

		int w = entryWidth(g) + PAD_X * 2;
		int h = entries.size() * ROW_H + 4;

		g.setFont(g.getFont().deriveFont(Font.PLAIN, 13f));
		g.setColor(BACKGROUND);
		g.fillRect(x, y, w, h);
		g.setColor(BORDER);
		g.drawRect(x, y, w, h);

		if (hover >= 0 && hover < entries.size())
		{
			g.setColor(HOVER);
			g.fillRect(x + 1, y + 2 + hover * ROW_H, w - 2, ROW_H);
		}

		int ty = y + 2;
		for (MenuEntry e : entries)
		{
			// "option target", minus the colour tags the game's menu would render as colour -- our
			// plain popup has no coloured spans, so they would show as raw markup.
			String line = Text.removeTags(e.getOption() + " " + e.getTarget()).trim();
			g.setColor(TEXT);
			g.drawString(line, x + PAD_X, ty + ROW_H - 7);
			ty += ROW_H;
		}
	}

	private int entryWidth(Graphics2D g)
	{
		int w = 40;
		for (MenuEntry e : entries)
		{
			String line = Text.removeTags(e.getOption() + " " + e.getTarget()).trim();
			int tw = g.getFontMetrics(g.getFont().deriveFont(Font.PLAIN, 13f)).stringWidth(line);
			if (tw > w)
			{
				w = tw;
			}
		}
		return w;
	}

	private int rowAt(Point mouse)
	{
		if (mouse == null)
		{
			return -1;
		}
		int my = mouse.getY() - y - 2;
		if (mouse.getX() < x || my < 0)
		{
			return -1;
		}
		int row = my / ROW_H;
		return row < entries.size() ? row : -1;
	}

	/** The click must be inside the game canvas -- over the side panel it is somebody else's click. */
	private boolean overCanvas(net.runelite.api.ClientState state)
	{
		Point mouse = state.getMouseCanvasPosition();
		if (mouse == null)
		{
			return false;
		}
		int[] view = Natives.viewport();
		return view.length == 4
			&& mouse.getX() >= 0 && mouse.getX() < view[2]
			&& mouse.getY() >= 0 && mouse.getY() < view[3];
	}
}
