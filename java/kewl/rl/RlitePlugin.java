// Adapter that runs a RuneLite-style plugin inside kewl.
//
// One RlitePlugin wraps one net.runelite.client.plugins.Plugin. The object graph is built eagerly in
// the constructor -- that is when the config proxy declares its settings, so the control panel can
// show them before the plugin is ever enabled -- but startUp() waits for the user to switch it on,
// matching RuneLite's own lifecycle.
//
// Threading: everything here runs on kewl's overlay frame thread. That thread is registered as the
// shim's "client thread", so PathfinderConfig.refresh()'s thread guard passes and ClientThread
// callbacks from the pathfinder worker queue up and drain at the top of the next frame.
package kewl.rl;

import java.awt.Graphics2D;
import java.util.HashSet;
import java.util.Set;
import java.util.function.Supplier;

import kewl.Plugin;
import net.runelite.api.Client;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.game.SpriteManager;
import net.runelite.client.input.KeyManager;
import net.runelite.client.ui.overlay.Overlay;
import net.runelite.client.ui.overlay.OverlayManager;

public class RlitePlugin extends Plugin
{
	private final String displayName;
	private final String displayDescription;
	private final Supplier<net.runelite.client.plugins.Plugin> factory;

	private EventBus eventBus;
	private OverlayManager overlayManager;
	private net.runelite.client.plugins.Plugin plugin;
	private Events events;
	/** The right-click popup menu: the fallback for the not-yet-readable game menu (see MenuPopup). */
	private MenuPopup menuPopup;
	/** Overlays startUp added, so onDisable can take exactly those back out. */
	private Set<Overlay> ownedOverlays = Set.of();
	/** The auto-walk driver, wired in build() when the wrapped plugin produces paths. */
	private AutoWalk autoWalk;
	/** Kept from build() so key edges can be dispatched to the plugin's registered hotkeys. */
	private KeyManager keyManager;
	/** Synthetic source for dispatched KeyEvents; a KeyEvent refuses a null source. */
	private static final java.awt.Component KEY_EVENT_SOURCE = new java.awt.Panel();

	/**
	 * @param name        shown in the control panel
	 * @param description one line under the name
	 * @param factory     builds the RuneLite-style plugin; called once, in the constructor
	 */
	public RlitePlugin(String name, String description, Supplier<net.runelite.client.plugins.Plugin> factory)
	{
		this.displayName = name;
		this.displayDescription = description;
		this.factory = factory;
		// Default ON, unlike upstream Shortest Path, which never issues a single step: its path is
		// guidance, and the user walks it. Kewl's user asked for the client to walk the path, and a
		// "Set target" that draws tiles but never moves reads as the plugin being broken -- the first
		// run should end with the character arriving. The toggle stays, right here in the panel.
		config.bool("autoWalk", "Auto-walk",
			"Walk the computed path automatically. Stops at plane changes (boats, stairs, teleports).",
			true);
		build();
	}

	@Override
	public String name()
	{
		return displayName;
	}

	@Override
	public String description()
	{
		return displayDescription;
	}

	/** Build the object graph now so the panel sees the settings; startUp waits for enable. */
	private void build()
	{
		eventBus = new EventBus();
		ConfigManager configManager = new ConfigManager(eventBus, config);
		overlayManager = new OverlayManager();

		Injector injector = new Injector(eventBus, configManager, config);
		injector.bind(Client.class, Client.get());
		injector.bind(EventBus.class, eventBus);
		injector.bind(OverlayManager.class, overlayManager);
		injector.bind(ConfigManager.class, configManager);
		injector.bind(ClientThread.class, new ClientThread());
		keyManager = new KeyManager();
		injector.bind(KeyManager.class, keyManager);
		injector.bind(SpriteManager.class, new SpriteManager());

		try
		{
			plugin = injector.build(factory.get());
		}
		catch (Throwable t)
		{
			System.out.println("[" + displayName + "] construction failed: " + t);
			t.printStackTrace();
		}

		events = new Events(eventBus);
		menuPopup = new MenuPopup(eventBus);

		// The auto-walk driver needs the plugin's path. Until a second ported plugin produces paths,
		// this is wired straight to ShortestPathPlugin -- see AutoWalk.
		if (plugin instanceof shortestpath.ShortestPathPlugin shortestPath)
		{
			autoWalk = new AutoWalk(shortestPath);
		}
	}

	@Override
	protected void onEnable()
	{
		if (plugin == null)
		{
			return;
		}
		// The overlay frame thread is the shim's client thread for the lifetime of the client.
		ClientThread.setGameThread(Thread.currentThread());

		plugin.setEventBus(eventBus);
		// Normalize to exactly one subscription: Injector registered the plugin at construction, and
		// an enable/disable/enable cycle must not stack a second copy of every subscriber.
		eventBus.unregister(plugin);
		eventBus.register(plugin);
		Set<Overlay> before = new HashSet<>(overlayManager.getOverlays());
		invokeLifecycle("startUp");
		Set<Overlay> after = new HashSet<>(overlayManager.getOverlays());
		after.removeAll(before);
		ownedOverlays = after;
	}

	@Override
	protected void onDisable()
	{
		if (plugin == null)
		{
			return;
		}
		invokeLifecycle("shutDown");
		// RuneLite's plugin manager only delivers events to enabled plugins; without this a disabled
		// plugin keeps receiving GameTick and ConfigChanged and quietly keeps acting on them.
		eventBus.unregister(plugin);
		if (autoWalk != null)
		{
			autoWalk.reset();
		}
		// tick() only runs the popup while enabled, so a menu up when the user switches the plugin
		// off would never see another tick: force-close it, or isMenuOpen() stays true (and the
		// captured tile stays parked) until the plugin is enabled and ticked again.
		menuPopup.close();
		autoWalkStatus = null;
		// remove(), not getOverlays().removeAll(): getOverlays() hands back an unmodifiable view, and
		// removeAll on it throws -- the overlays would only ever leave by the plugin's own shutDown
		// happening to remove them.
		overlayManager.remove(ownedOverlays);
		ownedOverlays = Set.of();
	}

	/**
	 * startUp/shutDown are protected on net.runelite.client.plugins.Plugin, so plugins can override
	 * them but kewl.rl cannot call them directly. Reflection is the bridge -- and the lookup must walk
	 * the hierarchy with getDeclaredMethod, because getMethod only finds PUBLIC methods and would
	 * NoSuchMethodException on every plugin whose lifecycle is the upstream protected one.
	 */
	private void invokeLifecycle(String method)
	{
		try
		{
			java.lang.reflect.Method m = null;
			for (Class<?> c = plugin.getClass(); c != null && c != Object.class; c = c.getSuperclass())
			{
				try
				{
					m = c.getDeclaredMethod(method);
					break;
				}
				catch (NoSuchMethodException e)
				{
					// keep walking up
				}
			}
			if (m == null)
			{
				System.out.println("[" + displayName + "] has no " + method + "() to call");
				return;
			}
			m.setAccessible(true);
			m.invoke(plugin);
		}
		catch (Throwable t)
		{
			System.out.println("[" + displayName + "] " + method + " threw: " + t);
			t.printStackTrace();
		}
	}

	@Override
	public void tick()
	{
		// Push the raw Win32 input snapshot before anything reads it this frame -- getMouseCanvasPosition
		// and isKeyPressed(KC_SHIFT) both come off this, and plugins want the state of THIS frame.
		Client.get().state().setInputState(kewl.Natives.input());

		if (plugin == null)
		{
			return;
		}
		ClientThread.drain();
		dispatchKeyEdges();
		events.fire();
		// After events: a right-click fires MenuOpened/MenuEntryAdded synchronously, and the plugin's
		// handlers (which read this frame's input state) must see the same snapshot events.fire()
		// would have left.
		if (isEnabled())
		{
			menuPopup.tick();
		}

		if (autoWalk != null && config.bool("autoWalk"))
		{
			autoWalkStatus = autoWalk.tick();
		}
	}

	/**
	 * Hand this frame's key edges to the plugin's registered hotkey listeners as ordinary KeyEvents --
	 * the same shape Keybind.matches consumes in RuneLite. Sources from a dummy component; KeyEvent
	 * refuses a null one.
	 */
	private void dispatchKeyEdges()
	{
		if (keyManager == null)
		{
			return;
		}
		int[] edges = Client.get().state().keyEdges();
		if (edges.length == 0)
		{
			return;
		}
		net.runelite.api.ClientState state = Client.get().state();
		int mods = 0;
		if (state.isKeyPressed(net.runelite.api.KeyCode.KC_SHIFT))
		{
			mods |= java.awt.event.InputEvent.SHIFT_DOWN_MASK;
		}
		if (state.isKeyPressed(net.runelite.api.KeyCode.KC_CONTROL))
		{
			mods |= java.awt.event.InputEvent.CTRL_DOWN_MASK;
		}
		if (state.isKeyPressed(net.runelite.api.KeyCode.KC_ALT))
		{
			mods |= java.awt.event.InputEvent.ALT_DOWN_MASK;
		}
		long when = System.currentTimeMillis();
		for (int vk : edges)
		{
			keyManager.dispatchPressed(new java.awt.event.KeyEvent(KEY_EVENT_SOURCE,
				java.awt.event.KeyEvent.KEY_PRESSED, when, mods, vk, java.awt.event.KeyEvent.CHAR_UNDEFINED));
		}
	}

	@Override
	public void render(Graphics2D g)
	{
		if (plugin == null)
		{
			return;
		}
		OverlayRenderer.render(g, overlayManager);
		menuPopup.render(g);                 // on top of every overlay: a menu over a menu is a menu
	}

	@Override
	public String status()
	{
		if (plugin == null)
		{
			return "failed to construct";
		}
		return autoWalkStatus != null ? autoWalkStatus : "";
	}

	/** Last non-null status from the auto-walk driver, for the control panel. */
	private String autoWalkStatus;
}
