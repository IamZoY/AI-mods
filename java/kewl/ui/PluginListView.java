package kewl.ui;

import java.awt.Graphics2D;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

import kewl.Plugin;
import kewl.api.Game;
import kewl.api.Local;

/**
 * The plugins list: RuneLite's PluginListPanel. One 22px row per plugin -- name on the left, gear and
 * switch on the right -- sorted alphabetically, exactly the shape of a PluginListItem row minus the pin
 * star (pinning needs persistence the kewl config store does not have, so it is left out rather than
 * faked with a toggle that forgets).
 *
 * <p>RuneLite's rows are one line on purpose: the description and the live status are tooltips, not
 * second lines, and this list keeps that rule -- hover a row and the description (plus a running
 * plugin's status) shows in a tooltip. Clicking the name or the gear opens the plugin's config, which
 * in RuneLite pushes a ConfigPanel onto the panel's CardLayout stack.</p>
 */
final class PluginListView {

    private PluginListView() {}

    private static final int ROW_H = 22;
    private static final int GAP = 5;      // DynamicGridLayout(0, 1, 0, 5) in PluginListPanel

    static int draw(PanelCtx ctx, List<Plugin> plugins, int y) {
        y = Widgets.title(ctx, ctx.left, y, "KewlKlient", Game.ready() ? "in game" : "waiting for the game...");

        // The old panel's live position line, kept so the header still says whether the memory reads work.
        if (Game.ready()) {
            Graphics2D g = ctx.g;
            Local me = Game.me();
            g.setFont(Theme.MONO);
            g.setColor(Theme.TEXT_DIM);
            g.drawString(me.worldX() + ", " + me.worldY() + "   run " + me.runEnergy() + "%", ctx.left, y);
            y += 18;
        }
        y += 6;

        List<Plugin> sorted = new ArrayList<>(plugins);
        sorted.sort(Comparator.comparing(p -> p.name().toLowerCase()));

        for (Plugin p : sorted) {
            y = row(ctx, p, y);
        }
        return y + SidePanel.PAD;
    }

    private static int row(PanelCtx ctx, Plugin p, int y) {
        Graphics2D g = ctx.g;
        int x = ctx.left;
        int w = ctx.right - x;
        boolean hasConfig = !p.config.isEmpty();

        if (!ctx.visible(y, ROW_H)) return y + ROW_H + GAP;

        boolean hover = ctx.hover(x, y, w, ROW_H);

        // Name, white, turning RuneLite's orange on hover like PluginListItem's label.
        g.setFont(Theme.UI);
        g.setColor(hover ? Theme.RL_ORANGE : Theme.RL_LABEL);
        g.drawString(Widgets.clip(g, p.name(), w - 90), x + 2, y + 14);

        // Gear, only when there is something to configure -- PluginListItem does the same.
        if (hasConfig) {
            Widgets.gear(g, ctx.right - 46, y + 5, hover ? Theme.RL_ORANGE : Theme.TEXT_DIM);
        }
        Widgets.toggle(g, ctx.right - 28, y + 4, p.isEnabled());

        // First match wins, so the specific controls are registered before the row they sit in.
        if (hasConfig) {
            ctx.hit(ctx.right - 50, y, 18, ROW_H, () -> SidePanel.openConfig(p));
        }
        ctx.hit(ctx.right - 32, y, 32, ROW_H, () -> p.later(() -> p.setEnabled(!p.isEnabled())));
        if (hasConfig) {
            ctx.hit(x, y, w - 60, ROW_H, () -> SidePanel.openConfig(p));
        }

        // Description and live status as a tooltip; the row is one line, like RuneLite's.
        if (hover) {
            String tip = p.description();
            if (p.isEnabled() && !p.status().isEmpty()) {
                tip = (tip.isEmpty() ? "" : tip + "  |  ") + p.status();
            }
            if (p.hotkey() >= 0 && p.hotkey() < 8) {
                tip = (tip.isEmpty() ? "" : tip + "  ") + "[F" + (p.hotkey() + 1) + "]";
            }
            ctx.tooltip(tip);
        }
        return y + ROW_H + GAP;
    }
}
