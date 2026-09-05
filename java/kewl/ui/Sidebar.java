package kewl.ui;

import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.util.ArrayList;
import java.util.List;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JColorChooser;
import javax.swing.JComboBox;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSlider;
import javax.swing.JTextField;
import javax.swing.ScrollPaneConstants;
import javax.swing.SwingUtilities;
import javax.swing.Timer;
import javax.swing.event.DocumentEvent;
import javax.swing.event.DocumentListener;

import kewl.Natives;
import kewl.Plugin;
import kewl.api.Game;
import kewl.api.Local;
import kewl.config.Config;
import kewl.config.Setting;

/**
 * The control panel: a plain window beside the game with a switch for every plugin and its settings.
 *
 * <p>An ordinary Swing window rather than something drawn on the overlay, for one practical reason --
 * the overlay is click-through, so anything drawn there could be looked at and never clicked. Making it
 * a real window also means it is a real window: you can move it to another monitor, alt-tab to it, and
 * it does not fight the game for input.</p>
 *
 * <p>It builds itself from the plugin list. A plugin that declares three settings gets three controls,
 * and nobody writes any Swing but this file.</p>
 */
public final class Sidebar {

    private Sidebar() {}

    private static JFrame frame;
    private static JLabel statusLabel;
    private static final List<Runnable> refreshers = new ArrayList<>();

    /** Build and show the window. Call it from the EDT. */
    public static void open(List<Plugin> plugins) {
        if (frame != null) return;

        frame = new JFrame("KewlKlient");
        frame.setDefaultCloseOperation(JFrame.HIDE_ON_CLOSE);
        frame.setAlwaysOnTop(true);

        JPanel root = new JPanel(new BorderLayout());
        root.setBackground(Theme.BACKGROUND);
        root.setBorder(BorderFactory.createEmptyBorder(10, 10, 10, 10));

        root.add(header(), BorderLayout.NORTH);

        JPanel list = new JPanel();
        list.setLayout(new BoxLayout(list, BoxLayout.Y_AXIS));
        list.setBackground(Theme.BACKGROUND);
        for (Plugin p : plugins) {
            list.add(card(p));
            list.add(Box.createVerticalStrut(8));
        }
        list.add(Box.createVerticalGlue());

        JScrollPane scroll = new JScrollPane(list,
                ScrollPaneConstants.VERTICAL_SCROLLBAR_AS_NEEDED,
                ScrollPaneConstants.HORIZONTAL_SCROLLBAR_NEVER);
        scroll.setBorder(null);
        scroll.getViewport().setBackground(Theme.BACKGROUND);
        scroll.getVerticalScrollBar().setUnitIncrement(16);
        root.add(scroll, BorderLayout.CENTER);

        frame.setContentPane(root);
        frame.setSize(320, 560);
        placeBesideGame();
        frame.setVisible(true);

        // The status line is the only thing that changes on its own. A Swing timer keeps it on the EDT
        // without the render loop having to know the panel exists.
        new Timer(250, e -> refresh()).start();
    }

    private static JPanel header() {
        JPanel head = new JPanel();
        head.setLayout(new BoxLayout(head, BoxLayout.Y_AXIS));
        head.setBackground(Theme.BACKGROUND);
        head.setBorder(BorderFactory.createEmptyBorder(0, 0, 10, 0));

        JLabel title = new JLabel("KewlKlient");
        title.setFont(Theme.TITLE);
        title.setForeground(Theme.TEXT);
        title.setAlignmentX(Component.LEFT_ALIGNMENT);

        statusLabel = new JLabel("waiting for the game...");
        statusLabel.setFont(Theme.UI);
        statusLabel.setForeground(Theme.TEXT_DIM);
        statusLabel.setAlignmentX(Component.LEFT_ALIGNMENT);

        head.add(title);
        head.add(Box.createVerticalStrut(2));
        head.add(statusLabel);
        return head;
    }

    /** One plugin: its switch, what it does, and its settings. */
    private static JPanel card(Plugin plugin) {
        JPanel card = new JPanel();
        card.setLayout(new BoxLayout(card, BoxLayout.Y_AXIS));
        card.setBackground(Theme.SURFACE);
        card.setBorder(BorderFactory.createCompoundBorder(
                BorderFactory.createLineBorder(Theme.BORDER),
                BorderFactory.createEmptyBorder(8, 10, 10, 10)));
        card.setAlignmentX(Component.LEFT_ALIGNMENT);
        card.setMaximumSize(new Dimension(Integer.MAX_VALUE, Integer.MAX_VALUE));

        JCheckBox toggle = new JCheckBox(plugin.name(), plugin.isEnabled());
        toggle.setFont(Theme.UI_BOLD);
        toggle.setForeground(Theme.TEXT);
        toggle.setBackground(Theme.SURFACE);
        toggle.setFocusPainted(false);
        toggle.setAlignmentX(Component.LEFT_ALIGNMENT);
        // Run the toggle on the frame thread, not here on the EDT: startUp() can construct and load
        // heavy things (the pathfinder decompresses a collision map and parses dozens of tables), and
        // the frame thread is already touching the same state every frame.
        toggle.addActionListener(e -> plugin.later(() -> plugin.setEnabled(toggle.isSelected())));
        card.add(toggle);

        // A hotkey the plugin claimed, so the panel and the keyboard agree about what F5 does.
        int key = plugin.hotkey();
        String hint = plugin.description();
        if (key >= 0 && key <= 7) {
            hint = (hint.isEmpty() ? "" : hint + "  ") + "[F" + (key + 1) + "]";
        }
        if (!hint.isEmpty()) {
            JLabel desc = new JLabel("<html><body style='width:240px'>" + escape(hint) + "</body></html>");
            desc.setFont(Theme.UI);
            desc.setForeground(Theme.TEXT_DIM);
            desc.setAlignmentX(Component.LEFT_ALIGNMENT);
            desc.setBorder(BorderFactory.createEmptyBorder(2, 22, 4, 0));
            card.add(desc);
        }

        JLabel status = new JLabel(" ");
        status.setFont(Theme.UI);
        status.setForeground(Theme.ACCENT);
        status.setAlignmentX(Component.LEFT_ALIGNMENT);
        status.setBorder(BorderFactory.createEmptyBorder(0, 22, 0, 0));
        card.add(status);

        for (Component c : controls(plugin.config)) card.add(c);

        // The panel does not own plugin state -- a hotkey can toggle a plugin behind its back, so the
        // checkbox re-reads it rather than assuming it is the only thing that changes it.
        refreshers.add(() -> {
            if (toggle.isSelected() != plugin.isEnabled()) toggle.setSelected(plugin.isEnabled());
            String s = plugin.isEnabled() ? plugin.status() : "";
            status.setText(s == null || s.isEmpty() ? " " : s);
        });
        return card;
    }

    /** Build a control for every setting a plugin declared. */
    private static List<Component> controls(Config config) {
        List<Component> out = new ArrayList<>();
        if (config == null || config.isEmpty()) return out;

        out.add(Box.createVerticalStrut(6));
        for (Setting s : config.all()) {
            JPanel row = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 2));
            row.setBackground(Theme.SURFACE);
            row.setAlignmentX(Component.LEFT_ALIGNMENT);
            row.setMaximumSize(new Dimension(Integer.MAX_VALUE, 34));

            switch (s.kind()) {
                case BOOL -> {
                    JCheckBox box = new JCheckBox(s.label(), s.asBool());
                    box.setFont(Theme.UI);
                    box.setForeground(Theme.TEXT);
                    box.setBackground(Theme.SURFACE);
                    box.setFocusPainted(false);
                    box.addActionListener(e -> s.set(box.isSelected()));
                    row.add(box);
                }
                case INT -> {
                    JLabel label = label(s.label() + ":");
                    // Clamp defensively: DefaultBoundedRangeModel throws on value < min, which would
                    // kill the whole panel over one bad setting.
                    int v = Math.max(s.min(), Math.min(s.max(), s.asInt()));
                    JSlider slider = new JSlider(s.min(), s.max(), v);
                    slider.setBackground(Theme.SURFACE);
                    slider.setPreferredSize(new Dimension(120, 20));
                    JLabel value = label(String.valueOf(s.asInt()));
                    slider.addChangeListener(e -> {
                        s.set(slider.getValue());
                        value.setText(String.valueOf(slider.getValue()));
                    });
                    row.add(label);
                    row.add(slider);
                    row.add(value);
                }
                case TEXT -> {
                    JTextField field = new JTextField(s.asText(), 10);
                    field.setFont(Theme.UI);
                    field.setBackground(Theme.SURFACE_HI);
                    field.setForeground(Theme.TEXT);
                    field.setCaretColor(Theme.TEXT);
                    field.getDocument().addDocumentListener(new DocumentListener() {
                        private void changed() { s.set(field.getText()); }
                        @Override public void insertUpdate(DocumentEvent e) { changed(); }
                        @Override public void removeUpdate(DocumentEvent e) { changed(); }
                        @Override public void changedUpdate(DocumentEvent e) { changed(); }
                    });
                    row.add(label(s.label() + ":"));
                    row.add(field);
                }
                case COLOR -> {
                    JButton swatch = new JButton("      ") {
                        @Override protected void paintComponent(Graphics g) {
                            super.paintComponent(g);
                            g.setColor(s.asColor());
                            g.fillRect(4, 4, getWidth() - 8, getHeight() - 8);
                        }
                    };
                    swatch.setPreferredSize(new Dimension(44, 22));
                    swatch.setFocusPainted(false);
                    swatch.addActionListener(e -> {
                        Color picked = JColorChooser.showDialog(frame, s.label(), s.asColor());
                        if (picked != null) { s.set(picked); swatch.repaint(); }
                    });
                    row.add(label(s.label() + ":"));
                    row.add(swatch);
                }
                case ENUM -> {
                    JComboBox<Object> combo = new JComboBox<>(s.options());
                    combo.setFont(Theme.UI);
                    combo.setBackground(Theme.SURFACE_HI);
                    combo.setForeground(Theme.TEXT);
                    combo.setSelectedItem(s.value());
                    combo.addActionListener(e -> s.set(combo.getSelectedItem()));
                    row.add(label(s.label() + ":"));
                    row.add(combo);
                }
            }

            if (!s.description().isEmpty()) row.setToolTipText(s.description());
            out.add(row);
        }
        return out;
    }

    private static JLabel label(String text) {
        JLabel l = new JLabel(text);
        l.setFont(Theme.UI);
        l.setForeground(Theme.TEXT_DIM);
        return l;
    }

    /** Park the panel just right of the game window, or leave it where it is if we cannot tell. */
    private static void placeBesideGame() {
        int[] v = Natives.viewport();
        if (v.length == 4 && v[2] > 0) {
            frame.setLocation(v[0] + v[2] + 8, v[1]);
        } else {
            frame.setLocationRelativeTo(null);
        }
    }

    private static void refresh() {
        Local me = Game.me();
        if (!Game.ready()) {
            statusLabel.setForeground(Theme.TEXT_DIM);
            statusLabel.setText("waiting for the game...");
        } else {
            statusLabel.setForeground(Theme.ON);
            statusLabel.setText(me.worldX() + ", " + me.worldY()
                    + "   hp " + me.health() + "/" + me.maxHealth()
                    + "   run " + me.runEnergy() + "%");
        }
        for (Runnable r : refreshers) r.run();
    }

    private static String escape(String s) {
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    /** Antialiased text everywhere, which is the difference between "styled" and "1998". */
    public static void prettyText(Graphics2D g) {
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
        g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
        g.setRenderingHint(RenderingHints.KEY_STROKE_CONTROL, RenderingHints.VALUE_STROKE_PURE);
    }
}
