// The seam between the RuneLite shim and the memory offsets that do not exist yet.
//
// Every method here is either wired to kewl (nothing left to do) or holds an honest default with a
// comment naming the offset it is waiting for. Phase D of the port re-derives those offsets one at a
// time; each lands by filling in its method here plus a native in kewl.Natives -- nothing else in the
// shim needs to change.
package net.runelite.api;

import net.runelite.api.gameval.InterfaceID;
import net.runelite.api.gameval.VarbitID;
import net.runelite.api.widgets.Widget;
import java.awt.Rectangle;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

import net.runelite.api.worldmap.WorldMap;

public class ClientState
{
	// -- varps -------------------------------------------------------------------------------------
	//
	// LIVE as of client-240-6: the varp native reads the client's global varp array (offsets.hpp,
	// VARP_ARRAY_PTR, found via the client's own getVarp Lua binding). Varbit reads decompose on top of
	// it with the definition table below -- pure data from an OSRS cache dump, no memory reads of its
	// own -- so a varbit id we have no row for still reads 0 (fail closed), but every id the plugin and
	// the transport data reference is covered by VarbitTable.
	//
	// There is deliberately no setVarps caching layer any more: the native is a pass-through read, and
	// the shim asks for individual ids a handful of times per frame. A stale snapshot behind an API
	// that looks live is worse than a slightly slower live read.

	/** varbit id -> {varp index, low bit, bit count}. */
	private static final Map<Integer, int[]> VARBITS = loadVarbitTable();

	public int getVarpValue(int id)
	{
		if (id < 0 || !kewl.api.Game.ready())
		{
			return -1;
		}
		return kewl.Natives.varp(id);
	}

	/**
	 * Unknown or unset varbits read 0, not -1. Callers test these with {@code != 0} ("is the gate
	 * open?"), so an unknown sentinel of -1 would answer "open" to every gate -- PathfinderConfig's
	 * SAILING_BOARDED_BOAT check turned "no varp data at all" into "permanently on a boat" that way.
	 * Zero fails closed: gated transports are treated as unusable until their varbit is known.
	 */
	public int getVarbitValue(int id)
	{
		int[] def = VARBITS.get(id);
		if (def == null)
		{
			return 0; // unknown varbit id: fail closed
		}
		if (id < 0 || !kewl.api.Game.ready())
		{
			return 0; // varps not up yet: fail closed
		}
		int varp = kewl.Natives.varp(def[0]);
		return (varp >> def[1]) & ((1 << def[2]) - 1);
	}

	/**
	 * Two sources, merged. The built-in {@link VarbitTable} covers exactly the ids the plugin and the
	 * vendored transport data read, generated from the cache's own varbit archive; a full
	 * resources/varbits.csv dump (one id,varp,lowBit,bits row per line) is loaded over it when present,
	 * so a fresher dump can be dropped in without touching this class.
	 */
	private static Map<Integer, int[]> loadVarbitTable()
	{
		Map<Integer, int[]> out = new HashMap<>();
		for (int[] row : VarbitTable.ROWS)
		{
			out.put(row[0], new int[]{row[1], row[2], row[3]});
		}
		InputStream in = ClientState.class.getResourceAsStream("/varbits.csv");
		if (in == null)
		{
			return out;
		}
		try (BufferedReader r = new BufferedReader(new InputStreamReader(in)))
		{
			String line;
			while ((line = r.readLine()) != null)
			{
				if (line.startsWith("#") || line.isBlank())
				{
					continue;
				}
				String[] parts = line.split(",");
				if (parts.length < 4)
				{
					continue;
				}
				out.put(Integer.parseInt(parts[0].trim()),
					new int[]{Integer.parseInt(parts[1].trim()),
						Integer.parseInt(parts[2].trim()),
						Integer.parseInt(parts[3].trim())});
			}
		}
		catch (IOException e)
		{
			System.err.println("varbits.csv unreadable: " + e);
		}
		return out;
	}

	// -- game state ----------------------------------------------------------------------------------
	//
	// LIVE as of client-240-6: the client's own state field at client+0x2160 (offsets.hpp GAME_STATE,
	// found via the client's own isLoggedIn Lua leaf, which compares exactly this dword to 30). The
	// values are the same numbering the Java client uses, so GameState.of() maps them directly and the
	// LOGIN_SCREEN -> LOGGING_IN -> LOADING -> LOGGED_IN chain RuneLite code expects happens for real
	// instead of being replayed synthetically. 0 before the client object exists maps to STARTING,
	// which is the truthful answer.
	//
	// The current world id is still NOT derivable statically (every lead eliminated in the deob notes);
	// getWorldType stays empty and WorldChanged keeps its despawn-based trigger.

	public GameState getGameState()
	{
		return GameState.of(kewl.Natives.gameState());
	}

	public EnumSet<WorldType> getWorldType()
	{
		return EnumSet.noneOf(WorldType.class); // world-type flags need the env() native
	}

	// -- window, camera, input -----------------------------------------------------------------------
	//
	// Waiting on: layout mode, minimap zoom, camera yaw (all near the camera/viewport code; anchor:
	// worldToScreenCoord's camera reads). The mouse position and modifier keys are pure Win32 in the
	// input() native and are pushed in here by the bridge every frame.

	private volatile Point mouseCanvasPosition = new Point(0, 0);
	private volatile boolean shiftDown;
	private volatile boolean ctrlDown;
	private volatile boolean altDown;
	private volatile boolean lbuttonDown;
	private volatile boolean rbuttonDown;
	private volatile boolean mbuttonDown;
	/**
	 * Set by kewl's menu popup for the frames it is on screen. The game's own menu-open flag lives in
	 * the menu struct that is not readable yet (Phase D), so "is a menu open" is answered by the menu
	 * we drew ourselves -- which is the only menu entries get created for right now anyway.
	 */
	private volatile boolean popupMenuOpen;
	/** Virtual-key codes that turned pressed this frame; replaced wholesale by setInputState. */
	private volatile int[] keyEdges = new int[0];
	private int lastInputFrame = -1;

	public void setMouseCanvasPosition(int x, int y)
	{
		mouseCanvasPosition = new Point(x, y);
	}

	/**
	 * Per-frame snapshot from the input() native: {mouseX, mouseY, shift, ctrl, alt, lbutton,
	 * rbutton, mbutton, then one entry per virtual-key code that turned pressed since the previous
	 * frame}. The bridge dispatches the edges as KeyEvents, so a Keybind hotkey works the way it does
	 * in RuneLite, and kewl's menu popup watches rbutton for the right-click that opens it.
	 *
	 * <p>The frame check makes this idempotent per frame: the native computes edges against the
	 * previous frame, so a second call in the same frame (two bridged plugins tick in sequence) must
	 * not consume them -- both read the same snapshot. The token is kewl's own frame counter, not
	 * anything read from game memory: a stale offset that returned a constant would otherwise collapse
	 * every frame into one and silently kill the whole input pipeline.</p>
	 */
	public void setInputState(int[] buttons)
	{
		int frame = kewl.KewlKlient.frame();
		if (frame == lastInputFrame)
		{
			return;
		}
		lastInputFrame = frame;
		if (buttons.length >= 3)
		{
			this.shiftDown = buttons[2] != 0;
			this.ctrlDown = buttons[3] != 0;
			this.altDown = buttons[4] != 0;
			this.mouseCanvasPosition = new Point(buttons[0], buttons[1]);
		}
		if (buttons.length >= 8)
		{
			this.lbuttonDown = buttons[5] != 0;
			this.rbuttonDown = buttons[6] != 0;
			this.mbuttonDown = buttons[7] != 0;
		}
		int n = Math.max(0, buttons.length - 8);
		int[] edges = new int[n];
		System.arraycopy(buttons, 8, edges, 0, n);
		this.keyEdges = edges;
	}

	/** Left button held, from the same snapshot as the rest of setInputState. */
	public boolean lbuttonDown()
	{
		return lbuttonDown;
	}

	/** Right button held, from the same snapshot as the rest of setInputState. */
	public boolean rbuttonDown()
	{
		return rbuttonDown;
	}

	/** Middle button held, from the same snapshot as the rest of setInputState. */
	public boolean mbuttonDown()
	{
		return mbuttonDown;
	}

	/** The key edges for the current frame, oldest first; the same array until the next snapshot. */
	public int[] keyEdges()
	{
		return keyEdges;
	}

	public Point getMouseCanvasPosition()
	{
		return mouseCanvasPosition;
	}

	public boolean isKeyPressed(int keyCode)
	{
		if (keyCode == KeyCode.KC_SHIFT)
		{
			return shiftDown;
		}
		if (keyCode == KeyCode.KC_CONTROL)
		{
			return ctrlDown;
		}
		if (keyCode == KeyCode.KC_ALT)
		{
			return altDown;
		}
		return false;
	}

	public boolean isResized()
	{
		return true; // modern client default; the layout-mode offset would say for sure
	}

	public double getMinimapZoom()
	{
		return 4.0; // TODO: minimap zoom offset (Phase E, minimap overlay)
	}

	public int getCameraYawTarget()
	{
		return 0; // TODO: camera yaw offset (Phase E); minimap rotation stays north-up until then
	}

	// -- containers ------------------------------------------------------------------------------------
	//
	// LIVE as of client-240-6: the container native walks the client's global container table
	// (offsets.hpp, CONTAINER_BUCKETS, found via the client's own invGetObjId/invGetNum bindings) and
	// returns one snapshot. Waiting on in-game verification of the ids used below.

	public ItemContainer getItemContainer(int id)
	{
		if (id < 0 || !kewl.api.Game.ready())
		{
			return null;
		}
		int[] flat = kewl.Natives.container(id);
		if (flat == null || flat.length == 0)
		{
			return null;
		}
		Item[] items = new Item[flat.length / 2];
		for (int i = 0; i < items.length; i++)
		{
			items[i] = new Item(flat[2 * i], flat[2 * i + 1]);
		}
		return new ItemContainer(id, items);
	}

	// -- widgets -----------------------------------------------------------------------------------------
	//
	// LIVE as of client-240-6: the widget tree is the classic rs2lib IfType, looked up by the client's
	// own (group << 16) | component id encoding through the interface manager (offsets.hpp, IFACE_*,
	// found via the client's own ifType Lua binding and verified live -- real text and canvas-sized
	// bounds came back for the loaded groups). Children walk the same IfType records through the
	// widgetChild native; they are filled lazily, see fillChildren.

	public Widget getWidget(int... ids)
	{
		if (ids == null || ids.length == 0 || !kewl.api.Game.ready())
		{
			return null;
		}
		// RuneLite's convention: one argument is a packed id, two or more are groupId, componentId,
		// then nested child indices.
		int packed = ids.length == 1 ? ids[0] : (ids[0] << 16) | ids[1];
		int[] root = kewl.Natives.widget(packed);
		if (root.length == 0)
		{
			return null;
		}
		Widget w = fillWidget(packed, root);
		// Nested children: descend and accumulate the parents' x/y, because the widget stores its
		// position relative to its parent and RuneLite's getCanvasLocation is absolute.
		for (int i = 2; i < ids.length && w != null; i++)
		{
			Rectangle pb = w.getBounds();
			int[] child = kewl.Natives.widgetChild(packed, ids[i]);
			if (child.length == 0)
			{
				return null;
			}
			packed = childIndexId(packed, ids[i]);
			w = fillWidgetOffset(packed, child, pb.x, pb.y);
		}
		return w;
	}

	/** A component id under `parent` for a child found at childIndex. */
	private static int childIndexId(int parentGroupPacked, int childIndex)
	{
		return (parentGroupPacked & 0xFFFF0000) | childIndex;
	}

	private static Widget fillWidget(int id, int[] v)
	{
		return fillWidgetOffset(id, v, 0, 0);
	}

	private static Widget fillWidgetOffset(int id, int[] v, int offX, int offY)
	{
		Widget w = new Widget(id);
		w.setBounds(offX + v[1], offY + v[2], v[3], v[4]);
		w.setHidden(v[5] != 0);
		w.setRelativeY(v[2]);
		w.setText(kewl.Natives.widgetText(id));
		// Children are the caller's problem: enumerating them costs one native call per child, which
		// getWidget has no reason to pay for a widget the caller only wanted the bounds of.
		w.setChildLoader(() -> fillChildren(w, id, w.getBounds().x, w.getBounds().y));
		return w;
	}

	/**
	 * Fills `parent`'s child list by walking the widgetChild native until it comes back empty. The
	 * native exposes one flat child array per widget (cache-defined static children and runtime-spawned
	 * dynamic children alike), so this is the same list behind both accessors -- see Widget.
	 *
	 * <p>The child's own packed id assumes the client addresses children the way it addresses
	 * everything else, as {@code (group << 16) | index} into the group's component array -- the same
	 * encoding doAction targets use. A child that is not in that array resolves to no text rather than
	 * to its parent's, which is the honest failure.</p>
	 */
	private static void fillChildren(Widget parent, int id, int offX, int offY)
	{
		int contentBottom = 0;
		for (int i = 0; i < Widget.MAX_CHILDREN; i++)
		{
			int[] child = kewl.Natives.widgetChild(id, i);
			if (child.length == 0)
			{
				break; // past the last child: the whole list has been read
			}
			Widget w = fillWidgetOffset(childIndexId(id, i), child, offX, offY);
			parent.getChildren().add(w);
			contentBottom = Math.max(contentBottom, w.getRelativeY() + w.getHeight());
		}
		if (contentBottom > 0)
		{
			parent.setScrollHeight(contentBottom);
		}
	}

	/**
	 * The same widget the plugin itself resolves for its minimap clip area
	 * (ShortestPathPlugin.getMinimapDrawWidget), reached the same way -- the stone-arrangement varbit
	 * (live via VarbitTable) says which resizable skin the player runs. isResized() above is still the
	 * hardcoded modern default, so the fixed-mode branch stays unreachable until the layout-mode
	 * offset lands; that is exactly the information the plugin has, no more.
	 */
	public Widget getMinimapDrawWidget()
	{
		if (isResized())
		{
			return getWidget(getVarbitValue(VarbitID.RESIZABLE_STONE_ARRANGEMENT) == 1
				? InterfaceID.ToplevelPreEoc.MINIMAP
				: InterfaceID.ToplevelOsrsStretch.MINIMAP);
		}
		return getWidget(InterfaceID.Toplevel.MINIMAP);
	}

	// -- definitions, map, menu --------------------------------------------------------------------------

	/**
	 * True while kewl's own menu popup is on screen. The game's menu-open flag lives in the menu
	 * struct that is not readable yet (the menu native, Phase D) -- until then the only menu entries
	 * that exist are the ones the popup shows, so its state IS the menu state.
	 */
	public boolean isMenuOpen()
	{
		return popupMenuOpen;
	}

	/** Called by kewl's menu popup for the frames it is drawn; not game state. */
	public void setPopupMenuOpen(boolean open)
	{
		popupMenuOpen = open;
	}

	public ItemDefinition getItemDefinition(int itemId)
	{
		// No item-name table yet: the definition is the id, and the name says so.
		return new ItemDefinition(itemId);
	}

	public WorldMap getWorldMap()
	{
		return WorldMap.INSTANCE;
	}

	public EnumComposition getEnum(int id)
	{
		// The rune-pouch enum is the only one the plugin reads; its bundled table lives in EnumTable.
		// Every other enum degrades to empty: quantities read -1 and gated teleports stay unusable.
		if (id == EnumID.RUNEPOUCH_RUNE)
		{
			final int[][] rows = EnumTable.RUNEPOUCH_RUNE;
			final int[] keys = new int[rows.length];
			final int[] vals = new int[rows.length];
			for (int i = 0; i < rows.length; i++)
			{
				keys[i] = rows[i][0];
				vals[i] = rows[i][1];
			}
			return new EnumComposition()
			{
				@Override public int size() { return rows.length; }
				@Override public int[] getKeys() { return keys; }
				@Override public int[] getIntVals() { return vals; }
				@Override public long[] getLongVals() { return new long[0]; }
				@Override public String[] getStringVals() { return new String[0]; }
				@Override public int getIntValue(int key)
				{
					for (int i = 0; i < keys.length; i++)
					{
						if (keys[i] == key)
						{
							return vals[i];
						}
					}
					return -1;
				}
				@Override public String getStringValue(int key) { return ""; }
				@Override public long getLongValue(int key) { return -1; }
			};
		}
		return new EnumComposition()
		{
			@Override public int size() { return 0; }
			@Override public int[] getKeys() { return new int[0]; }
			@Override public int[] getIntVals() { return new int[0]; }
			@Override public long[] getLongVals() { return new long[0]; }
			@Override public String[] getStringVals() { return new String[0]; }
			@Override public int getIntValue(int key) { return -1; }
			@Override public String getStringValue(int key) { return ""; }
			@Override public long getLongValue(int key) { return -1; }
		};
	}

	public Menu getMenu()
	{
		return Menu.INSTANCE;
	}
}
