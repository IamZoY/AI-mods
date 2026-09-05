// panel_ui.hpp -- the ImGui side panel the LAUNCHER draws, over the format-2 bridge model.
//
// In launcher mode the strip on the right of the launcher window is this file's job: the launcher
// process has no OpenGL and no Java, so the panel is Dear ImGui rasterized by the CPU
// (client/imgui_sw.hpp) into the window's DIB. The DATA, though, still lives in the game process --
// Java owns the plugin list, the Setting objects, the profiles and the hub, so everything drawn here
// comes from the shared-memory bridge (client/bridge.hpp, parsed in launcher/main.cpp) and every
// mutation goes back as an edit record. Edits MUST land in Java through the DLL (see kewl/panel/
// PanelBridge): an edit that went around Setting.set would silently do nothing to the plugin.
//
// The layout mirrors RuneLite's sidebar rather than inventing one: a 250px body plus a 36px icon rail
// (TAB_W/BODY_W in the Java SidePanel), four routes -- plugins, profiles, hub, debug -- with the
// active one marked by a 3px orange edge on the rail's left, a plugin list with search at the top and
// pinned plugins first (PluginListPanel), a pushed config view per plugin with a back arrow,
// collapsible sections and one widget per setting kind (ConfigPanel), a profile list with
// create/rename/duplicate/delete (ProfilePanel), and a hub view with the third-party warning, search,
// refresh and per-entry install/remove/update (PluginHubPanel).
//
// KNOWN GAPS, on purpose rather than faked:
//   * plugin tags are not searchable: the model region carries name/desc/status and no tag list, so
//     the filter runs over what the bridge actually sends;
//   * colour and text settings render read-only (the contract has no colour edit kind, and Java's own
//     panel shows text as a non-editable box for the same "no keyboard" reason);
//   * sections render in first-appearance order -- the bridge model does not carry RlConfigMeta's
//     section positions, which is what the Java ConfigView sorts by;
//   * a reset echo cannot show the default value: Setting's default lives Java-side, so the row keeps
//     its old value until the next publish carries the reset one back.
#pragma once

#include "imgui.h"
#include "imgui_internal.h"    // ClearActiveID: the launcher surrenders a live text field when
                               // the user clicks back into the game (see main.cpp keepKeyboard)
#include "bridge_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace kewl_panel {

// -------------------------------------------------------------------------------------------------
// Widths, in one place. SidePanel.java keeps TAB_W(36) + BODY_W(250) = 286 and the native side pins
// that number; here the rail and the body are two ImGui windows, so the pair below is the only
// thing they cannot drift apart over. main.cpp uses PANEL_W (or the collapsed width) for the
// game-child layout.
// -------------------------------------------------------------------------------------------------
constexpr int BODY_W  = 250;
constexpr int RAIL_W  = 36;
constexpr int PANEL_W = BODY_W + RAIL_W;      // 286 -- the width dllmain.cpp's Java panel occupies

// SidePanel's per-row metrics, kept the same so the two panels feel like one client.
constexpr float ROW_H    = 22.0f;             // PluginListView.ROW_H / ConfigView.ROW_H
constexpr float ROW_GAP  = 5.0f;              // ConfigView.GAP -- DynamicGridLayout(0, 1, 0, 5)
constexpr float TOGGLE_W = 28.0f, TOGGLE_H = 14.0f;   // Widgets.toggle's 28x14 pill
constexpr float BOX_W    = 76.0f;             // ConfigView.BOX_W -- stepper / spinner box
constexpr float COMBO_W  = 116.0f;            // slider / combo box: 14px narrower than ConfigView's,
                                              // so a setting label and the per-row reset glyph fit
                                              // beside it in a 250px body

// kewl.panel.PanelBridge.ENABLE_KEY: a plugin's on/off switch is not a Setting, so it is named
// rather than keyed -- setBool("enabled") routes to Plugin.setEnabled, every other key to Setting.set.
constexpr const char* ENABLE_KEY = "enabled";

// How long a destructive control stays armed before the arm expires (ConfigView's click-again
// affordance, reused for profile delete and plugin-config reset).
constexpr double CONFIRM_SECS = 2.0;

// -------------------------------------------------------------------------------------------------
// Theme.java, as ImU32 constants. IM_COL32 is RGBA-in-a-u32, and the software rasterizer's buffer is
// 0xAARRBBGB little-endian (see imgui_sw.hpp's format note), so these go straight into vertices.
// -------------------------------------------------------------------------------------------------
namespace theme {
constexpr ImU32 BACKGROUND = IM_COL32(28, 28, 32, 255);
constexpr ImU32 SURFACE    = IM_COL32(38, 38, 44, 255);
constexpr ImU32 SURFACE_HI = IM_COL32(50, 50, 58, 255);
constexpr ImU32 BORDER     = IM_COL32(58, 58, 66, 255);
constexpr ImU32 TEXT       = IM_COL32(226, 226, 232, 255);
constexpr ImU32 TEXT_DIM   = IM_COL32(150, 150, 160, 255);
constexpr ImU32 ON         = IM_COL32(110, 220, 140, 255);
constexpr ImU32 OFF        = IM_COL32(120, 120, 130, 255);
constexpr ImU32 WARN       = IM_COL32(255, 140, 120, 255);   // Theme.WARN: destructive + errors
// RuneLite's ColorScheme, for the pieces that copy it view for view (SidePanel, ConfigView).
constexpr ImU32 RL_ORANGE  = IM_COL32(220, 138, 0, 255);
constexpr ImU32 RL_TAB     = IM_COL32(30, 30, 30, 255);
constexpr ImU32 RL_TAB_HI  = IM_COL32(60, 60, 60, 255);
constexpr ImU32 RL_DIVIDER = IM_COL32(77, 77, 77, 255);
constexpr ImU32 RL_LABEL   = IM_COL32(255, 255, 255, 255);

inline ImVec4 f(ImU32 c) {                  // style colours arrive as ImVec4
    return ImGui::ColorConvertU32ToFloat4(c);
}
}  // namespace theme

// -------------------------------------------------------------------------------------------------
// The model, exactly as bridge.hpp's format-2 model region carries it and main.cpp's Reader parses
// it. The launcher does not get to trust another process's memory, so main.cpp's reader bounds-checks
// everything; this file only ever sees a parsed copy it may mutate (which is what the optimistic
// echo below does -- hence the pointers: an echo written into a per-frame copy would be gone by the
// next frame, before Java's revision bump arrives to replace it with truth).
// -------------------------------------------------------------------------------------------------
struct Setting {
    std::int32_t kind = 0, valueInt = 0, min = 0, max = 0, enumIndex = 0, optionCount = 0, flags = 0;
    std::string key, label, desc, section, valueText;
    std::vector<std::string> options;
};

struct PluginModel {
    std::int32_t enabled = 0, hasConfig = 0, hotkey = -1, pinned = 0;
    std::string name, desc, status;
    std::vector<Setting> settings;
};

struct ProfileModel {
    std::string name, id;        // id is the stable identifier Java keys on; name is what is shown
};

struct HubEntry {
    std::string id, name, version, author, desc;
    std::int32_t flags = 0;               // kewl_bridge::HUB_FLAG_*
    std::int32_t installedPluginIdx = -1; // the plugin record this entry installed, or -1
};

// What main.cpp hands draw() each frame: the live model plus the bridge bookkeeping the header shows.
struct Model {
    std::vector<PluginModel>* plugins = nullptr;    // mutable: the optimistic echo writes here
    std::vector<ProfileModel>* profiles = nullptr;  // ditto
    std::vector<HubEntry>* hub = nullptr;           // ditto
    std::int32_t* activeProfile = nullptr;          // ditto: index into *profiles, -1 = none
    std::int32_t* hubState = nullptr;               // ditto: kewl_bridge::HubState
    std::string* hubError = nullptr;                // ditto: one line, when *hubState == HUB_ERROR
    std::int64_t modelRevision = -1;
    std::int64_t editSeq = 0;                       // hdr->editSeq: the bridge's own edit counter
    bool bridgeUp = false;
    std::string note;                               // one utf8 line of bridge state, for the header
    std::string gameStatus;                         // utf8 of the launcher's status line, no-model case
};

// One edit, in the bridge contract's shape. main.cpp's writeEdit enqueues it and pokes the DLL.
using EditSink = std::function<void(std::int32_t kind, std::int32_t pluginIdx,
                                    const char* key, std::int64_t intVal, const char* text)>;

void applyStyle();      // once, after ImGui::CreateContext
void draw(const Model& m, const EditSink& edit);

// Offline probe, same spirit as main.cpp's KEWL_DUMP_FRAME: push a plugin's config view without a
// mouse to click the gear with. The fake-panel mode (KEWL_FAKE_PANEL + KEWL_FAKE_CONFIG) uses it to
// make the config view dumpable -- and a view that only a mouse could reach is a view that never
// gets verified. Out of range is a no-op, so a caller cannot wedge the UI with it.
void debugPushConfig(int pluginIdx);

// =================================================================================================
// Implementation. Header-only because launcher/main.cpp is the only TU that includes it -- the same
// choice imgui_sw.hpp makes -- and the UI state (active tab, nav stack, search text, armed confirms)
// lives in function-local statics, which are exactly as thread-unsafe and as sufficient as the
// one-thread PeekMessage loop that drives them.
// =================================================================================================

enum Tab { TAB_PLUGINS = 0, TAB_PROFILES = 1, TAB_HUB = 2, TAB_DEBUG = 3 };
constexpr int TAB_COUNT = 4;

constexpr float kPi = 3.14159265358979f;

// ---- the navigation model ------------------------------------------------------------------------
// RuneLite's multiplexing panel, reduced to what this sidebar needs: a route (the rail icon) plus a
// stack of pushed plugin-config pages over it. A tab switch resets to that route's root, back pops
// one page, and nothing here is a loose boolean -- every "am I in a config view" question is answered
// by looking at the stack.
inline int& uiTab() { static int t = TAB_PLUGINS; return t; }
inline std::vector<int>& uiStack()          { static std::vector<int> s; return s; }
inline int uiTop()                          { auto& s = uiStack(); return s.empty() ? -1 : s.back(); }
inline void navPush(int pluginIdx)          { uiStack().push_back(pluginIdx); }
inline void navPop()                        { if (!uiStack().empty()) uiStack().pop_back(); }
inline void navReset()                      { uiStack().clear(); }

// Scroll memory for the plugin list: pushing a config view replaces the child's content, which would
// otherwise drop the user back to the top of the list on the way back out. Saved on push, restored on
// the first frame back at the root.
inline float& uiScrollSaved()   { static float v = 0; return v; }

// The sidebar's open/closed state. main.cpp reads the ini once at boot, writes it back when this
// changes, and lays the game child out against effectivePanelW() -- that is the whole contract.
inline bool& uiCollapsed() { static bool c = false; return c; }
inline int  effectivePanelW() { return uiCollapsed() ? RAIL_W : PANEL_W; }

// ---- keyboard focus hooks ------------------------------------------------------------------------
// The game child owns Windows keyboard focus (the DLL holds it on JagRenderView), so a text field in
// the strip has to ASK for it: typing while a field is live must reach ImGui, not the game. Every
// InputText in the panel goes through textInput(), which latches a one-shot request the launcher's
// frame loop consumes with SetFocus on the launcher window, reports whether any field is live (so
// the loop can keep re-asserting focus against the DLL's WM_ACTIVATE re-focus), and lets the loop
// hand the keyboard back to the game when the last field closes.
inline bool& kbRequestFlag()   { static bool v = false; return v; }
inline bool& kbActiveFlag()    { static bool v = false; return v; }   // reset at the top of draw()
inline bool& kbSurrenderFlag() { static bool v = false; return v; }   // launcher -> draw(): close the live field

inline bool uiKeyboardRequested() { bool v = kbRequestFlag(); kbRequestFlag() = false; return v; }
inline bool uiKeyboardActive()    { return kbActiveFlag(); }

inline bool textInput(const char* id, char* buf, size_t bufsz, const char* hint = nullptr,
                      float width = 0.0f) {
    ImGui::PushItemWidth(width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
    bool changed = hint ? ImGui::InputTextWithHint(id, hint, buf, bufsz)
                        : ImGui::InputText(id, buf, bufsz);
    ImGui::PopItemWidth();
    if (ImGui::IsItemActivated()) kbRequestFlag() = true;
    if (ImGui::IsItemActive())    kbActiveFlag() = true;
    return changed;
}

// The session's edit count, debug tab.
inline long& uiEditsSent() { static long n = 0; return n; }


// -------------------------------------------------------------------------------------------------
// Style. Tuned for a 250px body: Java's 12px UI font is the 13px bitmap default here (see
// initImGui), so paddings shrink to keep one-line rows one line. The colours are Theme.java.
// -------------------------------------------------------------------------------------------------
inline void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding      = ImVec2(8, 6);
    s.FramePadding       = ImVec2(4, 3);
    s.ItemSpacing        = ImVec2(6, 4);
    s.ItemInnerSpacing   = ImVec2(4, 3);
    s.ScrollbarSize      = 8.0f;         // SidePanel.drawScrollbar's thin 2px indicator, widened to
                                         // something a mouse can actually grab
    s.GrabMinSize        = 10.0f;
    s.WindowRounding     = 0.0f;
    s.ChildRounding      = 0.0f;
    s.PopupRounding      = 0.0f;
    s.FrameRounding      = 3.0f;
    s.GrabRounding       = 3.0f;
    s.ScrollbarRounding  = 4.0f;
    s.WindowBorderSize   = 0.0f;         // the strips draw their own 1px dividers, Theme.BORDER
    s.ChildBorderSize    = 0.0f;
    s.PopupBorderSize    = 1.0f;
    s.WindowTitleAlign   = ImVec2(0, 0);
    s.ButtonTextAlign    = ImVec2(0.5f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]             = theme::f(theme::TEXT);
    c[ImGuiCol_TextDisabled]     = theme::f(theme::TEXT_DIM);
    c[ImGuiCol_WindowBg]         = theme::f(theme::BACKGROUND);
    c[ImGuiCol_ChildBg]          = theme::f(theme::BACKGROUND);
    c[ImGuiCol_PopupBg]          = theme::f(theme::SURFACE);
    c[ImGuiCol_Border]           = theme::f(theme::BORDER);
    c[ImGuiCol_TitleBg]          = theme::f(theme::BACKGROUND);
    c[ImGuiCol_TitleBgActive]    = theme::f(theme::BACKGROUND);
    c[ImGuiCol_TitleBgCollapsed] = theme::f(theme::BACKGROUND);
    c[ImGuiCol_MenuBarBg]        = theme::f(theme::SURFACE);
    c[ImGuiCol_ScrollbarBg]      = theme::f(theme::BACKGROUND);
    c[ImGuiCol_ScrollbarGrab]        = theme::f(theme::RL_DIVIDER);
    c[ImGuiCol_ScrollbarGrabHovered] = theme::f(theme::TEXT_DIM);
    c[ImGuiCol_ScrollbarGrabActive]  = theme::f(theme::TEXT);
    c[ImGuiCol_FrameBg]              = theme::f(theme::SURFACE);
    c[ImGuiCol_FrameBgHovered]       = theme::f(theme::SURFACE_HI);
    c[ImGuiCol_FrameBgActive]        = theme::f(theme::SURFACE_HI);
    c[ImGuiCol_Button]               = theme::f(theme::SURFACE);
    c[ImGuiCol_ButtonHovered]        = theme::f(theme::SURFACE_HI);
    c[ImGuiCol_ButtonActive]         = theme::f(theme::BORDER);
    c[ImGuiCol_Header]               = theme::f(theme::SURFACE);      // CollapsingHeader's block
    c[ImGuiCol_HeaderHovered]        = theme::f(theme::SURFACE_HI);
    c[ImGuiCol_HeaderActive]         = theme::f(theme::SURFACE_HI);
    c[ImGuiCol_CheckMark]            = theme::f(theme::RL_ORANGE);    // the accent, not the default
    c[ImGuiCol_SliderGrab]           = theme::f(theme::RL_ORANGE);
    c[ImGuiCol_SliderGrabActive]     = theme::f(theme::RL_LABEL);
    c[ImGuiCol_Separator]            = theme::f(theme::RL_DIVIDER);   // ConfigView's 1px dividers
    c[ImGuiCol_SeparatorHovered]     = theme::f(theme::RL_ORANGE);
    c[ImGuiCol_SeparatorActive]      = theme::f(theme::RL_ORANGE);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_NavCursor]            = theme::f(theme::RL_ORANGE);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, 0.4f);
}

// -------------------------------------------------------------------------------------------------
// Small drawing helpers, each a line-for-line port of the Widgets.java glyph it is named after.
// All of them place themselves with SetCursorScreenPos + InvisibleButton, so they can sit at a
// computed position inside a hand-laid-out row; the caller keeps laying out after them.
// -------------------------------------------------------------------------------------------------

// Widgets.clip: cut a string to maxW, appending "...". UTF-8-aware on purpose -- the model's strings
// come from Java and plugin names carry non-ASCII; cutting a continuation byte would hand the font
// atlas a broken sequence.
inline std::string clip(const char* s, float maxW) {
    if (!s || !*s) return {};
    if (ImGui::CalcTextSize(s).x <= maxW) return s;
    static const char* ell = "...";
    const float ellW = ImGui::CalcTextSize(ell).x;
    std::string out;
    const char* p = s;
    while (*p) {
        const char* next = p + 1;
        while (*next && ((unsigned char)*next & 0xC0) == 0x80) ++next;   // a continuation byte
        if (ImGui::CalcTextSize(s, next).x + ellW > maxW) break;
        p = next;
    }
    out.assign(s, p - s);
    out += ell;
    return out;
}

// Widgets.toggle: a 28x14 pill in ON/OFF with a 10px background-coloured knob that sits right when
// on. Returns true when clicked.
inline bool toggleWidget(const char* id, bool on, const ImVec2& pos) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(TOGGLE_W, TOGGLE_H));
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImU32 fill = on ? theme::ON : theme::OFF;
    if (ImGui::IsItemHovered()) fill = on ? IM_COL32(140, 236, 165, 255) : theme::SURFACE_HI;
    dl->AddRectFilled(a, b, fill, TOGGLE_H * 0.5f);
    float knobX = on ? b.x - 2.0f - TOGGLE_H * 0.5f : a.x + 2.0f + TOGGLE_H * 0.5f;
    dl->AddCircleFilled(ImVec2(knobX, (a.y + b.y) * 0.5f), 5.0f, theme::BACKGROUND);
    return clicked;
}

// Widgets.gear: a ring, a hub, four teeth. Returns true when clicked.
inline bool gearWidget(const char* id, const ImVec2& pos) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(12, 12));
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin();
    ImU32 col = ImGui::IsItemHovered() ? theme::RL_ORANGE : theme::TEXT_DIM;
    ImVec2 c(a.x + 6, a.y + 6);
    dl->AddCircle(c, 3.0f, col, 0, 1.0f);
    dl->AddCircleFilled(c, 1.2f, col);
    dl->AddRectFilled(ImVec2(a.x + 5, a.y),      ImVec2(a.x + 7, a.y + 3),  col);
    dl->AddRectFilled(ImVec2(a.x + 5, a.y + 9),  ImVec2(a.x + 7, a.y + 12), col);
    dl->AddRectFilled(ImVec2(a.x,     a.y + 5),  ImVec2(a.x + 3, a.y + 7),  col);
    dl->AddRectFilled(ImVec2(a.x + 9, a.y + 5),  ImVec2(a.x + 12, a.y + 7), col);
    return clicked;
}

// Widgets.backArrow: shaft plus head, in a 24x22 hotspot like ConfigView.topBar's. Returns true when
// clicked (the caller pops the config view).
inline bool backWidget(const char* id, const ImVec2& pos) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(24, 22));
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin();
    ImU32 col = ImGui::IsItemHovered() ? theme::RL_ORANGE : theme::TEXT_DIM;
    ImVec2 tip(a.x + 1, a.y + 11);
    dl->AddLine(ImVec2(a.x + 10, a.y + 11), tip, col, 1.0f);
    dl->AddLine(tip, ImVec2(a.x + 6, a.y + 7), col, 1.0f);
    dl->AddLine(tip, ImVec2(a.x + 6, a.y + 15), col, 1.0f);
    return clicked;
}

// Widgets.star: the pin/favourite control, filled when pinned. A 5-point star, outer radius 6, in a
// 12x12 hotspot. Returns true when clicked.
inline bool starWidget(const char* id, bool on, const ImVec2& pos) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(12, 12));
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c = ImGui::GetItemRectMin();
    c.x += 6; c.y += 6;
    ImU32 col = ImGui::IsItemHovered() ? theme::RL_ORANGE
                                       : (on ? theme::RL_ORANGE : theme::TEXT_DIM);
    ImVec2 pts[10];
    for (int k = 0; k < 10; ++k) {
        float a = -kPi * 0.5f + (float)k * kPi * 0.2f;      // -90 deg, then every 36 deg
        float r = (k % 2 == 0) ? 6.0f : 2.6f;
        pts[k] = ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
    }
    if (on) dl->AddConvexPolyFilled(pts, 10, col);
    else    dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.0f);
    return clicked;
}

// Widgets.reset: a circular arrow, for "restore this setting's default". Draws only -- the caller
// owns the hotspot and the click, because the same glyph is used with two different edit kinds.
inline void resetGlyph(ImDrawList* dl, const ImVec2& pos, ImU32 col) {
    ImVec2 c(pos.x + 6, pos.y + 6);
    dl->PathArcTo(c, 4.0f, kPi * 0.25f, kPi * 1.6f, 12);
    dl->PathStroke(col, 0, 1.0f);
    // The head, at the arc's upper end, pointing the way the arrow "unwinds".
    dl->AddTriangleFilled(ImVec2(c.x - 3, c.y - 5), ImVec2(c.x + 2, c.y - 4), ImVec2(c.x - 1, c.y - 0.5f), col);
}

// A small 12x12 icon button wrapping a caller-drawn glyph. Returns true when clicked.
inline bool iconButton12(const char* id, const ImVec2& pos, void (*glyph)(ImDrawList*, const ImVec2&, ImU32)) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(12, 12));
    bool clicked = ImGui::IsItemClicked();
    ImU32 col = ImGui::IsItemHovered() ? theme::RL_ORANGE : theme::TEXT_DIM;
    glyph(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), col);
    return clicked;
}

inline void pencilGlyph(ImDrawList* dl, const ImVec2& a, ImU32 col) {   // rename
    dl->AddLine(ImVec2(a.x + 2, a.y + 10), ImVec2(a.x + 9, a.y + 3), col, 1.4f);
    dl->AddTriangleFilled(ImVec2(a.x + 9, a.y + 3), ImVec2(a.x + 10.5f, a.y + 1.5f),
                          ImVec2(a.x + 8, a.y + 1.5f), col);
    dl->AddLine(ImVec2(a.x + 2, a.y + 10), ImVec2(a.x + 3.5f, a.y + 10), col, 1.4f);
}

inline void copyGlyph(ImDrawList* dl, const ImVec2& a, ImU32 col) {     // duplicate: two sheets
    dl->AddRect(ImVec2(a.x + 3.5f, a.y + 1.0f), ImVec2(a.x + 10.5f, a.y + 8.0f), col);   // behind
    dl->AddRect(ImVec2(a.x + 1.5f, a.y + 4.0f), ImVec2(a.x + 8.5f, a.y + 11.0f), col);   // in front
}

inline void crossGlyph(ImDrawList* dl, const ImVec2& a, ImU32 col) {    // delete
    dl->AddLine(ImVec2(a.x + 2.5f, a.y + 2.5f), ImVec2(a.x + 9.5f, a.y + 9.5f), col, 1.4f);
    dl->AddLine(ImVec2(a.x + 9.5f, a.y + 2.5f), ImVec2(a.x + 2.5f, a.y + 9.5f), col, 1.4f);
}

// Widgets.chevron for the rail's collapse button, pointing `right` when the sidebar can collapse and
// left when it can expand.
inline void chevronGlyph(ImDrawList* dl, const ImVec2& a, bool right, ImU32 col) {
    float midY = a.y + 6;
    if (right) dl->AddTriangleFilled(ImVec2(a.x + 5, midY - 4), ImVec2(a.x + 5, midY + 4), ImVec2(a.x + 11, midY), col);
    else       dl->AddTriangleFilled(ImVec2(a.x + 7, midY - 4), ImVec2(a.x + 7, midY + 4), ImVec2(a.x + 1, midY), col);
}

// A loading spinner: an arc that sweeps with the clock, for the hub's refresh-in-flight state.
inline void spinner(ImDrawList* dl, const ImVec2& c, float r, ImU32 col) {
    float t = (float)ImGui::GetTime();
    float start = t * 4.0f;
    dl->PathArcTo(c, r, start, start + kPi * 1.2f, 12);
    dl->PathStroke(col, 0, 1.6f);
}

// ConfigView.stepper: the shared [ < value > ] box. Sets *dec / *inc when the matching end is
// clicked; draws its own surface so it reads as a control, not as text.
inline void stepperWidget(const char* id, const ImVec2& pos, const ImVec2& size,
                          const char* value, bool* dec, bool* inc) {
    *dec = *inc = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), theme::SURFACE, 3.0f);

    char bid[64];
    std::snprintf(bid, sizeof bid, "%s_dec", id);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(bid, ImVec2(20, size.y));
    *dec = ImGui::IsItemClicked();
    ImVec2 c = ImGui::GetItemRectMin();             // Widgets.chevron, pointing left
    ImVec2 mid(c.x + 10, c.y + size.y * 0.5f);
    ImU32 chev = ImGui::IsItemHovered() ? theme::RL_LABEL : theme::TEXT_DIM;
    dl->AddTriangleFilled(ImVec2(c.x + 12, mid.y - 4), ImVec2(c.x + 12, mid.y + 4), ImVec2(c.x + 6, mid.y), chev);

    std::snprintf(bid, sizeof bid, "%s_inc", id);
    ImVec2 incPos(pos.x + size.x - 20, pos.y);
    ImGui::SetCursorScreenPos(incPos);
    ImGui::InvisibleButton(bid, ImVec2(20, size.y));
    *inc = ImGui::IsItemClicked();
    c = ImGui::GetItemRectMin();
    mid = ImVec2(c.x + 10, c.y + size.y * 0.5f);
    chev = ImGui::IsItemHovered() ? theme::RL_LABEL : theme::TEXT_DIM;
    dl->AddTriangleFilled(ImVec2(c.x + 8, mid.y - 4), ImVec2(c.x + 8, mid.y + 4), ImVec2(c.x + 14, mid.y), chev);

    // The value, centred in what is left of the box -- ConfigView.stepper's layout.
    std::string shown = clip(value, size.x - 48.0f);
    ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
    dl->AddText(ImVec2(pos.x + 20 + (size.x - 40 - ts.x) * 0.5f,
                       pos.y + (size.y - ts.y) * 0.5f), theme::TEXT, shown.c_str());
}

// SidePanel.tabIcon, drawn with the draw list instead of Java2D. p is the glyph's top-left, exactly
// the x+10, y+10 the Java side uses inside a 36px cell.
inline void tabIcon(ImDrawList* dl, const ImVec2& p, int tab, ImU32 col) {
    switch (tab) {
        case TAB_PLUGINS:                       // a list: three lines of shrinking length
            dl->AddRectFilled(ImVec2(p.x,      p.y + 1),  ImVec2(p.x + 16, p.y + 3),  col);
            dl->AddRectFilled(ImVec2(p.x,      p.y + 7),  ImVec2(p.x + 12, p.y + 9),  col);
            dl->AddRectFilled(ImVec2(p.x,      p.y + 13), ImVec2(p.x + 8,  p.y + 15), col);
            break;
        case TAB_PROFILES: {                    // a person: head and shoulders
            dl->AddCircle(ImVec2(p.x + 8, p.y + 3), 3.0f, col, 0, 1.0f);
            // Java draws a 14x10 ellipse arc; PathArcTo is circular only, so a r=7 dome stands in.
            dl->PathArcTo(ImVec2(p.x + 8, p.y + 13), 7.0f, kPi, 2.0f * kPi, 12);
            dl->PathStroke(col, 0, 1.0f);
            break;
        }
        case TAB_HUB: {                         // a plug: two prongs, a body, a cable
            dl->AddLine(ImVec2(p.x + 1,  p.y + 5),  ImVec2(p.x + 6,  p.y + 5),  col, 1.6f);
            dl->AddLine(ImVec2(p.x + 1,  p.y + 11), ImVec2(p.x + 6,  p.y + 11), col, 1.6f);
            dl->AddRectFilled(ImVec2(p.x + 5, p.y + 2), ImVec2(p.x + 11, p.y + 14), col, 2.0f);
            dl->AddLine(ImVec2(p.x + 11, p.y + 8), ImVec2(p.x + 16, p.y + 8), col, 1.6f);
            break;
        }
        case TAB_DEBUG: {                       // a pulse: flat, spike, flat
            const float ys[6] = { 8, 8, 2, 14, 8, 8 };
            for (int i = 0; i < 5; ++i)
                dl->AddLine(ImVec2(p.x + i * 3, p.y + ys[i]),
                            ImVec2(p.x + (i + 1) * 3, p.y + ys[i + 1]), col, 1.0f);
            break;
        }
        default: break;
    }
}

// -------------------------------------------------------------------------------------------------
// Edits. Every mutation goes through commit() (value edits) or sendEdit() (the command-shaped kinds
// 4..14): echo the change into the local model copy FIRST so the strip reacts within this frame, then
// hand the record to the bridge. At 30fps across two processes that echo is the difference between a
// control that feels attached and one that feels like it is on a laggy socket; the next modelRevision
// bump (Java bumps it on every Setting.set, every status change and every enable) overwrites the echo
// with truth, so a value Java rejects reads wrong for at most one refresh.
// -------------------------------------------------------------------------------------------------
inline void sendEdit(const EditSink& edit, std::int32_t kind, std::int32_t pi,
                     const char* key, std::int64_t intVal, const char* text = nullptr) {
    edit(kind, pi, key, intVal, text);
    ++uiEditsSent();
}

inline void commit(const Model& m, const EditSink& edit, std::int32_t kind, std::int32_t pi,
                   const char* key, std::int64_t intVal, const char* text = nullptr) {
    if (pi < 0 || !m.plugins || pi >= (int)m.plugins->size()) return;
    PluginModel& pl = (*m.plugins)[pi];

    if (kind == kewl_bridge::EDIT_BOOL && std::strcmp(key, ENABLE_KEY) == 0) {
        pl.enabled = intVal ? 1 : 0;                     // Plugin.setEnabled, not a Setting
    } else {
        for (Setting& st : pl.settings) {
            if (st.key != key) continue;
            switch (kind) {
                case kewl_bridge::EDIT_BOOL:
                    st.valueInt = intVal ? 1 : 0;
                    st.valueText = intVal ? "on" : "off";     // PanelBridge.valueText's words
                    break;
                case kewl_bridge::EDIT_INT:
                    st.valueInt = (std::int32_t)intVal;
                    // The echo loses the @Units suffix Java bakes into valueText; the next publish
                    // puts it back. Showing "12" where "12 ms" was for one frame is honest.
                    st.valueText = std::to_string((long long)intVal);
                    break;
                case kewl_bridge::EDIT_ENUM: {
                    int idx = (std::int32_t)intVal;
                    st.valueInt = idx;
                    st.enumIndex = idx;
                    if (idx >= 0 && idx < (int)st.options.size()) st.valueText = st.options[idx];
                    break;
                }
                case kewl_bridge::EDIT_TEXT:
                    st.valueInt = 0;
                    st.valueText = text ? text : "";
                    break;
                default: break;
            }
            break;
        }
    }

    sendEdit(edit, kind, pi, key, intVal, text);
}

// PanelBridge keeps Java's declaration order and the launcher edits by that index, but RuneLite's
// list is alphabetical -- so sort the ROWS, keep the indices (PluginListView's Comparator.comparing).
// v2 adds pinning: pinned plugins surface first (PluginListPanel does the same), still alphabetical
// within the group, and still stable so a re-publish cannot shuffle equal rows.
inline std::vector<int> sortedOrder(const std::vector<PluginModel>& plugins) {
    std::vector<int> order(plugins.size());
    for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (plugins[a].pinned != plugins[b].pinned) return plugins[a].pinned > plugins[b].pinned;
        std::string la = plugins[a].name, lb = plugins[b].name;
        for (char& c : la) c = (char)std::tolower((unsigned char)c);
        for (char& c : lb) c = (char)std::tolower((unsigned char)c);
        return la < lb;
    });
    return order;
}

// Case-insensitive substring, the way RuneLite's plugin search matches. Runs over name, description
// and the live status line -- tags are not part of the bridge model (see the header's gaps note).
inline bool matches(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower((unsigned char)a) ==
                                                    std::tolower((unsigned char)b); });
    return it != hay.end();
}

// SidePanel.mouse's x < 0 rule, translated: never show a row tooltip while a popup (a Combo's option
// list) is open over the row that spawned it.
inline bool tooltipsAllowed() {
    return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}

inline void rowTooltip(const std::string& desc, int hotkey) {
    if (!tooltipsAllowed()) return;
    if (desc.empty() && hotkey < 0) return;
    if (ImGui::BeginTooltip()) {
        if (!desc.empty()) ImGui::TextUnformatted(desc.c_str());
        if (hotkey >= 0 && hotkey < 8) ImGui::TextDisabled("[F%d]", hotkey + 1);
        ImGui::EndTooltip();
    }
}

// End a hand-laid-out row: pad the current line out to `rowH` with a Dummy. Two reasons this is an
// item and not just a SetCursorScreenPos: imgui asserts when SetCursorScreenPos moves the cursor
// past the window's extents and no item ever grows them (ErrorCheckUsingSetCursorPosToExtendParent
// Boundaries -- it fired here the first time), and the scrollbar needs the rows' real heights to
// size the content. The newline + ItemSpacing.y the Dummy emits IS the row gap, so the row pitch is
// ROW_H + style.ItemSpacing.y.
inline void endRow(const ImVec2& top, float rowH, float avail) {
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + rowH));
    ImGui::Dummy(ImVec2(avail, 0));
}

// One plugin row (PluginListItem): pin star, name, gear and toggle right, one line, tooltip for the
// rest. Hand-laid-out rather than a Selectable so the star, the gear and the toggle win the clicks
// they sit on.
inline void pluginRow(const Model& m, const EditSink& edit, int idx) {
    PluginModel& pl = (*m.plugins)[idx];
    ImGui::PushID(idx);

    ImVec2 top = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;

    // Name. White, orange on hover, clicking it opens the config like PluginListItem's label does.
    bool clickable = pl.hasConfig != 0;
    if (clickable) {
        std::string shown = clip(pl.name.c_str(), avail - 74.0f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::IsMouseHoveringRect(top, ImVec2(top.x + avail, top.y + ROW_H))
                                  ? theme::f(theme::RL_ORANGE) : theme::f(theme::RL_LABEL));
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked()) { navPush(idx); }
    } else {
        ImGui::TextUnformatted(clip(pl.name.c_str(), avail - 74.0f).c_str());
    }

    // Controls, right-aligned, vertically centred in the row the way Java offsets them (+4):
    // toggle at the right edge, then the gear, then the pin star leftmost.
    float midY = top.y + (ROW_H - TOGGLE_H) * 0.5f;
    if (toggleWidget("##on", pl.enabled != 0, ImVec2(top.x + avail - TOGGLE_W, midY)))
        commit(m, edit, kewl_bridge::EDIT_BOOL, idx, ENABLE_KEY, pl.enabled ? 0 : 1);
    if (pl.hasConfig && gearWidget("##gear", ImVec2(top.x + avail - TOGGLE_W - 20.0f, top.y + 5)))
        navPush(idx);
    if (starWidget("##pin", pl.pinned != 0, ImVec2(top.x + avail - TOGGLE_W - 36.0f, top.y + 5))) {
        pl.pinned = pl.pinned ? 0 : 1;               // echo, then the SET_PIN edit carries the new value
        sendEdit(edit, kewl_bridge::EDIT_SET_PIN, idx, "", pl.pinned);
    }

    // The row's own height, then the next row (see endRow).
    endRow(top, ROW_H, avail);

    // Description and live status as a tooltip -- the row is one line, like RuneLite's.
    std::string tip = pl.desc;
    if (pl.enabled && !pl.status.empty())
        tip = (tip.empty() ? "" : tip + "  |  ") + pl.status;
    rowTooltip(tip, pl.hotkey);

    ImGui::PopID();
}

inline void pluginsView(const Model& m, const EditSink& edit) {
    if (m.plugins->empty()) {
        // No model yet (or a jar without PanelBridge): the status line says where the launch stands,
        // which is more useful than an empty list that looks like a working client with no plugins.
        ImGui::TextWrapped("%s", m.gameStatus.c_str());
        return;
    }

    // The search field, before the list -- PluginListPanel's search box. Local filter only: it
    // touches neither the bridge nor Java, exactly as the spec's "no bridge traffic" asks.
    static char search[64] = "";
    textInput("##pluginsearch", search, sizeof search, "search plugins");
    ImGui::Spacing();

    const std::string needle = search;
    for (int idx : sortedOrder(*m.plugins)) {
        const PluginModel& pl = (*m.plugins)[idx];
        if (!matches(pl.name, needle) && !matches(pl.desc, needle) && !matches(pl.status, needle))
            continue;
        pluginRow(m, edit, idx);
    }
}

// One setting row (ConfigView.item): label left, control right -- except text, whose value goes on a
// full-width line below the label, exactly where ConfigPanel's BorderLayout.SOUTH puts it. The label
// carries the per-setting reset glyph: Setting.reset is a Java-side call (kinds 4..14 grew one), and
// the row is where ConfigPanel would put the affordance.
inline void settingRow(const Model& m, const EditSink& edit, int pi, int si) {
    PluginModel& pl = (*m.plugins)[pi];
    Setting& st = pl.settings[si];
    ImGui::PushID(si);

    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 top = ImGui::GetCursorScreenPos();
    bool isText = st.kind == kewl_bridge::SET_TEXT;
    float rowH = isText ? 40.0f : ROW_H;            // ConfigView's TEXT_LABEL_H + TEXT_BOX_H

    // Label: white, orange on hover, tooltip on hover -- PluginListItem.addLabelPopupMenu's set.
    // The clip leaves room for the row's widest resident (slider/combo reach 132px into the row,
    // the toggle only 44). The hover test is taken HERE, before the reset glyph below becomes the
    // window's last item: IsItemHovered after that would answer for the glyph, not the label.
    bool wideCtrl = st.kind == kewl_bridge::SET_INT || st.kind == kewl_bridge::SET_ENUM;
    std::string shown = clip(st.label.c_str(),
                             avail - (isText ? 32.0f : (wideCtrl ? 138.0f : 56.0f)));
    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::IsMouseHoveringRect(top, ImVec2(top.x + avail, top.y + rowH))
            ? theme::f(theme::RL_ORANGE) : theme::f(theme::RL_LABEL));
    ImGui::TextUnformatted(shown.c_str());
    ImGui::PopStyleColor();
    bool labelHovered = ImGui::IsItemHovered();

    // The reset glyph, at the row's right edge -- on both one-line and text rows, where the value
    // box below the label would otherwise swallow the click.
    if (iconButton12("##reset", ImVec2(top.x + avail - 12.0f, top.y + (rowH - 12.0f) * 0.5f), resetGlyph))
        sendEdit(edit, kewl_bridge::EDIT_RESET_SETTING, pi, st.key.c_str(), 0);
        // No echo on purpose: the default value lives in Java, so the row keeps showing what it had
        // until the next publish delivers the reset value. Faking a default here would be a second
        // source of truth.

    if (labelHovered && !st.desc.empty()) {
        if (tooltipsAllowed() && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted(st.label.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::TEXT_DIM));
            ImGui::TextWrapped("%s", st.desc.c_str());
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
    }

    float ctrlX = top.x + avail - 16.0f;            // leave room for the reset glyph at the right
    float midY = top.y + (ROW_H - TOGGLE_H) * 0.5f;
    switch (st.kind) {
        case kewl_bridge::SET_BOOL:
            if (toggleWidget("##v", st.valueInt != 0, ImVec2(ctrlX - TOGGLE_W, midY)))
                commit(m, edit, kewl_bridge::EDIT_BOOL, pi, st.key.c_str(), st.valueInt ? 0 : 1);
            break;

        case kewl_bridge::SET_INT: {
            bool bounded = st.max > st.min && (long long)st.max - st.min <= 1000;
            if (bounded) {
                // ConfigView.slider: bounded ints get a track, value inside the grab.
                int v = st.valueInt;
                ImGui::SetCursorScreenPos(ImVec2(ctrlX - COMBO_W, top.y + 1));
                ImGui::PushItemWidth(COMBO_W);
                if (ImGui::SliderInt("##v", &v, st.min, st.max, "%d",
                                     ImGuiSliderFlags_AlwaysClamp))
                    commit(m, edit, kewl_bridge::EDIT_INT, pi, st.key.c_str(), v);
                ImGui::PopItemWidth();
            } else {
                // ConfigView.spinner: no @Range, so +/- 1 per click through the shared stepper.
                bool dec = false, inc = false;
                stepperWidget("##v", ImVec2(ctrlX - BOX_W, top.y + 2), ImVec2(BOX_W, 18),
                              st.valueText.c_str(), &dec, &inc);
                if (dec) commit(m, edit, kewl_bridge::EDIT_INT, pi, st.key.c_str(), st.valueInt - 1);
                if (inc) commit(m, edit, kewl_bridge::EDIT_INT, pi, st.key.c_str(), st.valueInt + 1);
            }
            break;
        }

        case kewl_bridge::SET_KEYBIND: {
            // ConfigView.keybind: the shim stores a Keybind as an F-index (0 = not set), so the
            // control is a stepper over "not set", F1..F8, not a real key catcher.
            bool dec = false, inc = false;
            stepperWidget("##v", ImVec2(ctrlX - BOX_W, top.y + 2), ImVec2(BOX_W, 18),
                          st.valueText.c_str(), &dec, &inc);
            int f = st.valueInt;
            if (dec) commit(m, edit, kewl_bridge::EDIT_INT, pi, st.key.c_str(), f > 0 ? f - 1 : 0);
            if (inc) commit(m, edit, kewl_bridge::EDIT_INT, pi, st.key.c_str(), f < 8 ? f + 1 : 8);
            break;
        }

        case kewl_bridge::SET_ENUM: {
            // Combo of the option strings (the contract's EDIT_ENUM carries an option index, not a
            // raw value -- bridge.hpp's EditKind note). PanelBridge puts the current index in both
            // valueInt and enumIndex; trust valueInt, fall back when it is out of range.
            int cur = st.valueInt;
            if (cur < 0 || cur >= (int)st.options.size()) cur = st.enumIndex;
            std::vector<const char*> items;
            for (const std::string& o : st.options) items.push_back(o.c_str());
            ImGui::SetCursorScreenPos(ImVec2(ctrlX - COMBO_W, top.y + 1));
            ImGui::PushItemWidth(COMBO_W);
            int sel = cur;
            if (!items.empty() && ImGui::Combo("##v", &sel, items.data(), (int)items.size()))
                commit(m, edit, kewl_bridge::EDIT_ENUM, pi, st.key.c_str(), sel);
            else if (items.empty())
                ImGui::TextDisabled("%s", st.valueText.c_str());
            ImGui::PopItemWidth();
            break;
        }

        case kewl_bridge::SET_COLOR: {
            // Read-only. The contract has no colour edit kind (kinds 0..3), and Java's own panel
            // cycles a palette with s.set(Color) -- a path the bridge cannot express yet.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a(ctrlX - 34.0f, top.y + 3), b(a.x + 34, a.y + 16);
            // valueInt is 0xRRGGBB(A) from PanelBridge.valueInt; pack it the way IM_COL32 wants.
            std::uint32_t rgb = (std::uint32_t)st.valueInt;
            dl->AddRectFilled(a, b, IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255), 4.0f);
            dl->AddRect(a, b, theme::RL_DIVIDER, 4.0f);
            break;
        }

        case kewl_bridge::SET_TEXT: {
            // Read-only, like ConfigView: text input needs a keyboard handoff this panel does not
            // have yet (the game child owns focus). Show the value the Java panel would show.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a(top.x, top.y + 20), b(top.x + avail - 16.0f, top.y + 40);
            dl->AddRectFilled(a, b, theme::SURFACE, 3.0f);
            dl->AddText(ImVec2(a.x + 5, a.y + 3), theme::TEXT,
                        clip(st.valueText.c_str(), avail - 26.0f).c_str());
            break;
        }

        default:                                    // a kind from a newer DLL: show, don't invent
            ImGui::SameLine();
            ImGui::TextDisabled("%s", st.valueText.c_str());
            break;
    }

    endRow(top, rowH, avail);
    ImGui::PopID();
}

// ConfigView.draw: back arrow, name, toggle, then sections as collapsible headers.
inline void configView(const Model& m, const EditSink& edit, int pi) {
    PluginModel& pl = (*m.plugins)[pi];
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 top = ImGui::GetCursorScreenPos();

    if (backWidget("##back", top)) { navPop(); return; }

    // The plugin's own switch, duplicated here exactly as ConfigPanel duplicates it.
    if (toggleWidget("##on", pl.enabled != 0, ImVec2(top.x + avail - TOGGLE_W, top.y + 4)))
        commit(m, edit, kewl_bridge::EDIT_BOOL, pi, ENABLE_KEY, pl.enabled ? 0 : 1);

    // Title, centred between the arrow and the toggle. Drawn straight into the draw list, then
    // registered with a Dummy: the top bar is laid out by hand, and imgui wants an item at the
    // boundary the cursor is pushed to (see endRow).
    std::string shown = clip(pl.name.c_str(), avail - 76.0f);
    ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(top.x + 28 + (avail - 28 - TOGGLE_W - ts.x) * 0.5f, top.y + 4), theme::RL_LABEL,
        shown.c_str());
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + 26));
    ImGui::Dummy(ImVec2(avail, 0));
    ImGui::Separator();
    ImGui::Spacing();

    if (pl.settings.empty()) {
        ImGui::TextDisabled("no settings");
        return;
    }

    // Group by the section name the bridge carries, keeping first-appearance order. ConfigView
    // sorts by RlConfigMeta's section positions, which the model region does not carry -- so the
    // order here is Java's declaration order, and the section headers land where Java's metadata
    // first mentions them.
    std::vector<std::pair<std::string, std::vector<int>>> sections;
    std::vector<int> loose;
    for (int si = 0; si < (int)pl.settings.size(); ++si) {
        const std::string& sec = pl.settings[si].section;
        if (sec.empty()) { loose.push_back(si); continue; }
        auto found = std::find_if(sections.begin(), sections.end(),
                                  [&](const auto& s) { return s.first == sec; });
        if (found == sections.end()) sections.push_back({ sec, { si } });
        else found->second.push_back(si);
    }

    for (int si : loose) settingRow(m, edit, pi, si);

    for (const auto& sec : sections) {
        // Section header: orange name over a 1px divider (ConfigView.sectionHeader). CollapsingHeader
        // gives the collapse behaviour; the orange comes from ImGuiCol_Text, so it is popped before
        // the rows -- only the header is orange, like Java's.
        // SetNextItemOpen(Once) because imgui's storage default for a fresh header is CLOSED
        // (imgui_widgets.cpp TreeNodeBehavior: DefaultOpen ? 1 : 0), and ConfigView opens every
        // section whose RlConfigMeta does not say closedByDefault -- that flag is not part of the
        // bridge model, so open is the only honest default. Once: seed the first frame only, then
        // imgui's per-window storage keeps whatever the user last chose.
        ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::RL_ORANGE));
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        bool open = ImGui::CollapsingHeader(sec.first.c_str());
        ImGui::PopStyleColor();
        if (!open) continue;
        for (int si : sec.second) settingRow(m, edit, pi, si);
    }

    // ConfigView's Reset row, over the whole plugin: every setting of this plugin back to its
    // default. Destructive, so it is armed: the first click turns the button into "confirm reset?"
    // for CONFIRM_SECS, and only a second click inside that window sends EDIT_RESET_PLUGIN.
    ImGui::Spacing();
    static double armed = -1.0;
    static int armedFor = -1;
    double now = ImGui::GetTime();
    if (armedFor != pi) { armed = -1; armedFor = pi; }   // an arm belongs to one plugin's view
    bool isArmed = armed > 0 && now - armed < CONFIRM_SECS;
    if (!isArmed) armed = -1;
    ImGui::PushStyleColor(ImGuiCol_Text, isArmed ? theme::f(theme::WARN) : theme::f(theme::TEXT));
    if (ImGui::Button(isArmed ? "confirm reset?" : "Reset", ImVec2(avail, 22)) && isArmed) {
        sendEdit(edit, kewl_bridge::EDIT_RESET_PLUGIN, pi, "", 0);
        armed = -1;      // sent; the next publish replaces every row with Java's defaults
    } else if (ImGui::IsItemClicked()) {
        armed = now;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tooltipsAllowed())
        ImGui::SetTooltip(isArmed ? "click again to reset every setting of %s" : "restore every setting of %s to its default",
                          pl.name.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back", ImVec2(avail, 22))) navPop();
}

// The debug tab's launcher-side half of kewl.ui.DebugView: what the bridge is actually carrying.
// The memory-read lines (ready / you / npcs / inventory / pathcheck) are Java's debugLines, which
// the bridge contract does not transport -- they stay on the Java panel until someone adds a debug
// section to the model region.
inline void debugView(const Model& m) {
    ImGui::TextUnformatted("Debug");
    ImGui::TextDisabled("bridge state, not game state");
    ImGui::Spacing();
    ImGui::Text("bridge: %s", m.bridgeUp ? "up" : "down");
    ImGui::TextWrapped("%s", m.note.c_str());
    ImGui::Text("model rev: %lld", (long long)m.modelRevision);
    ImGui::Text("edits sent: %ld", uiEditsSent());
    ImGui::Text("edit seq:  %lld", (long long)m.editSeq);
    ImGui::Separator();
    if (!m.plugins) return;
    ImGui::Text("%d plugins", (int)m.plugins->size());
    for (const PluginModel& pl : *m.plugins) {
        ImGui::BulletText("%s%s", pl.pinned ? "* " : "", clip(pl.name.c_str(), 180.0f).c_str());
        ImGui::SameLine();
        if (pl.enabled) ImGui::TextDisabled("%s", pl.status.c_str());
        else ImGui::TextDisabled("off");
    }
    if (m.profiles && m.activeProfile)
        ImGui::Text("%d profiles, active %d", (int)m.profiles->size(), *m.activeProfile);
    if (m.hub && m.hubState)
        ImGui::Text("hub: %d entries, state %d", (int)m.hub->size(), *m.hubState);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::TEXT_DIM));
    ImGui::TextWrapped("not in the bridge contract yet: the memory-read lines (ready, you, npcs, "
                       "inventory, pathcheck).");
    ImGui::PopStyleColor();
}

// The profiles tab (ProfilePanel): the active profile marked, one row per profile with rename /
// duplicate / delete, and a create field at the top. Clicking a row switches profiles. Every action
// is an edit; the optimistic echo below only rearranges what the panel already knows, and the next
// publish replaces it with whatever Java actually did.
inline void profilesView(const Model& m, const EditSink& edit) {
    ImGui::TextUnformatted("Profiles");
    ImGui::TextDisabled("plugin config, per profile");
    ImGui::Spacing();
    if (!m.profiles || !m.activeProfile) return;

    // Create: a field and a button, ProfilePanel's add row. Java names the id; the panel only
    // sends the display name the user typed.
    static char newName[64] = "";
    float avail = ImGui::GetContentRegionAvail().x;
    textInput("##newprofile", newName, sizeof newName, "new profile name", avail - 62.0f);
    ImGui::SameLine();
    if (ImGui::Button("create", ImVec2(58, 0)) && newName[0]) {
        // Echo: a profile Java has not confirmed yet, keyed by its name. The next publish replaces
        // the whole list, so a rejected create simply disappears a frame later.
        m.profiles->push_back({ newName, newName });
        sendEdit(edit, kewl_bridge::EDIT_PROFILE_CREATE, -1, "", 0, newName);
        newName[0] = 0;
    }
    ImGui::Spacing();

    if (m.profiles->empty()) {
        ImGui::TextDisabled("(no profiles yet)");
        return;
    }

    static int renameIdx = -1;
    static char renameBuf[64] = "";
    static double armDelete = -1.0;
    static int armDeleteIdx = -1;
    double now = ImGui::GetTime();
    if (armDeleteIdx >= 0 && now - armDelete >= CONFIRM_SECS) armDeleteIdx = -1;

    for (int i = 0; i < (int)m.profiles->size(); ++i) {
        ProfileModel& pf = (*m.profiles)[i];
        bool active = i == *m.activeProfile;
        ImGui::PushID(i);

        ImVec2 top = ImGui::GetCursorScreenPos();
        avail = ImGui::GetContentRegionAvail().x;

        // The active profile's 3px orange edge, the same marker the rail uses for the active tab.
        if (active)
            ImGui::GetWindowDrawList()->AddRectFilled(top, ImVec2(top.x + 3, top.y + ROW_H),
                                                      theme::RL_ORANGE);

        if (renameIdx == i) {
            // Rename in place: the row's name becomes the field, prefilled; committing sends
            // PROFILE_RENAME, and Esc/blur without a change just closes it.
            ImGui::SetCursorScreenPos(ImVec2(top.x + 6, top.y + 1));
            textInput("##rename", renameBuf, sizeof renameBuf, nullptr, avail - 56.0f);
            bool commitNow = ImGui::IsItemDeactivatedAfterEdit();
            if (commitNow && renameBuf[0]) {
                pf.name = renameBuf;                 // echo; Java's id never changes
                sendEdit(edit, kewl_bridge::EDIT_PROFILE_RENAME, -1, "", i, renameBuf);
            }
            renameIdx = -1;
            ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + ROW_H));
            ImGui::Dummy(ImVec2(avail, 0));
            ImGui::PopID();
            continue;
        }

        // Name. Orange when it is the active profile, hover-highlighted otherwise; a click selects.
        bool rowHover = ImGui::IsMouseHoveringRect(top, ImVec2(top.x + avail, top.y + ROW_H));
        std::string shown = clip(pf.name.c_str(), avail - 60.0f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              active ? theme::f(theme::RL_ORANGE)
                                     : theme::f(rowHover ? theme::RL_ORANGE : theme::RL_LABEL));
        ImGui::SetCursorScreenPos(ImVec2(top.x + 6, top.y + 3));
        ImGui::TextUnformatted(shown.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !active) {
            *m.activeProfile = i;                    // echo, then the switch edit
            sendEdit(edit, kewl_bridge::EDIT_PROFILE_SWITCH, -1, "", i);
        }

        // Row controls, right-aligned: rename, duplicate, delete.
        float bx = top.x + avail - 14.0f;
        bool deleting = armDeleteIdx == i;
        if (deleting) {
            // Armed delete: the cross turns into a warn-coloured "sure?" text button, and a second
            // click inside the window sends PROFILE_DELETE. Clicking anything else disarms it.
            ImGui::SetCursorScreenPos(ImVec2(top.x + avail - 46.0f, top.y + 3));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::WARN));
            ImGui::TextUnformatted("sure?");
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) {
                m.profiles->erase(m.profiles->begin() + i);
                if (*m.activeProfile == i) *m.activeProfile = -1;   // truth arrives with the publish
                else if (*m.activeProfile > i) *m.activeProfile -= 1;
                sendEdit(edit, kewl_bridge::EDIT_PROFILE_DELETE, -1, "", i);
                armDeleteIdx = -1;
            }
        } else {
            if (iconButton12("##del", ImVec2(bx - 12.0f, top.y + 5), crossGlyph)) {
                armDelete = now; armDeleteIdx = i;
            }
            if (iconButton12("##dup", ImVec2(bx - 26.0f, top.y + 5), copyGlyph)) {
                // Echo a copy next to the original; Java picks the real name ("copy of ...").
                std::string dupName = pf.name + " copy";
                m.profiles->insert(m.profiles->begin() + i + 1, { dupName, dupName });
                if (*m.activeProfile > i) *m.activeProfile += 1;
                sendEdit(edit, kewl_bridge::EDIT_PROFILE_DUPLICATE, -1, "", i);
            }
            if (iconButton12("##ren", ImVec2(bx - 40.0f, top.y + 5), pencilGlyph)) {
                renameIdx = i;
                std::snprintf(renameBuf, sizeof renameBuf, "%s", pf.name.c_str());
            }
        }

        if (deleting && armDeleteIdx == i && now - armDelete >= CONFIRM_SECS) armDeleteIdx = -1;

        endRow(top, ROW_H, avail);
        ImGui::PopID();
    }
}

// The hub tab (PluginHubPanel): the third-party warning, a search field, refresh with a spinner
// while the manifest loads, and one row per hub entry with install/remove/update. All of the
// network and filesystem work happens Java-side; this view only reflects hubState and the per-entry
// flags, and every button is just an edit.
inline void hubView(const Model& m, const EditSink& edit) {
    ImGui::TextUnformatted("Plugin hub");
    ImGui::Spacing();

    // RuneLite's warning box, as a line: third-party plugins are not reviewed by us.
    ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::WARN));
    ImGui::TextWrapped("Third-party plugins. The hub is community content -- review a plugin "
                       "before installing it.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (!m.hub || !m.hubState || !m.hubError) return;
    bool loading = *m.hubState == kewl_bridge::HUB_LOADING;
    bool errored = *m.hubState == kewl_bridge::HUB_ERROR;

    // Search + refresh on one line: the field takes what is left of the button, which needs the
    // width of the word "refresh" plus padding -- narrower and imgui clips the label mid-glyph.
    static char search[64] = "";
    float avail = ImGui::GetContentRegionAvail().x;
    const float refreshW = ImGui::CalcTextSize("refresh").x + 14.0f;
    textInput("##hubsearch", search, sizeof search, "search the hub", avail - refreshW - 4.0f);
    ImGui::SameLine();
    if (loading) {
        // Spinner in the button's place; the refresh edit would only queue behind the load that is
        // already running, so the button is disabled rather than hidden (RuneLite greys it too).
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::BeginDisabled(true);
        ImGui::Button("##refresh", ImVec2(refreshW, 0));
        ImGui::EndDisabled();
        spinner(ImGui::GetWindowDrawList(), ImVec2(p.x + refreshW * 0.5f, p.y + 10), 6.0f,
                theme::RL_ORANGE);
    } else if (ImGui::Button("refresh", ImVec2(refreshW, 0))) {
        sendEdit(edit, kewl_bridge::EDIT_HUB_REFRESH, -1, "", 0);
    }
    ImGui::Spacing();

    if (errored) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::WARN));
        ImGui::TextWrapped("could not load the hub: %s", m.hubError->c_str());
        ImGui::PopStyleColor();
        return;
    }

    const std::string needle = search;
    int shown = 0;
    float lineH = ImGui::GetTextLineHeight();
    for (int i = 0; i < (int)m.hub->size(); ++i) {
        const HubEntry& he = (*m.hub)[i];
        if (!matches(he.name, needle) && !matches(he.desc, needle) && !matches(he.author, needle))
            continue;
        ++shown;

        ImGui::PushID(i);
        bool busy = (he.flags & kewl_bridge::HUB_FLAG_BUSY) != 0;
        bool installed = (he.flags & kewl_bridge::HUB_FLAG_INSTALLED) != 0;
        bool hasUpdate = (he.flags & kewl_bridge::HUB_FLAG_HAS_UPDATE) != 0;

        // The row, laid out by hand like every other row here: name + version on the first line with
        // the action button at its right, author and description under. PluginHubPanel's row,
        // narrowed to what 250px fits.
        ImVec2 top = ImGui::GetCursorScreenPos();
        std::string name = clip(he.name.c_str(), avail - 96.0f);
        std::string vers = clip(("v" + he.version).c_str(), 52.0f);
        ImGui::GetWindowDrawList()->AddText(ImVec2(top.x, top.y), theme::RL_LABEL, name.c_str());
        float nameW = ImGui::CalcTextSize(name.c_str()).x;
        ImGui::GetWindowDrawList()->AddText(ImVec2(top.x + nameW + 6.0f, top.y),
                                            theme::TEXT_DIM, vers.c_str());

        // The action button. Update is an install of the newer artifact, so it sends the same
        // HUB_INSTALL edit; BUSY disables it until Java reports the entry idle again.
        const char* action = installed ? (hasUpdate ? "update" : "remove")
                                       : (busy ? "..." : "install");
        float bw = ImGui::CalcTextSize(action).x + 14.0f;
        ImGui::SetCursorScreenPos(ImVec2(top.x + avail - bw, top.y - 2.0f));
        if (busy) ImGui::BeginDisabled(true);
        if (ImGui::Button(action, ImVec2(bw, lineH + 4.0f))) {
            if (installed && !hasUpdate)
                sendEdit(edit, kewl_bridge::EDIT_HUB_REMOVE, -1, "", 0, he.id.c_str());
            else
                sendEdit(edit, kewl_bridge::EDIT_HUB_INSTALL, -1, "", 0, he.id.c_str());
        }
        if (busy) ImGui::EndDisabled();

        // Back to the row's left edge, under the name line, for author + description.
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + lineH + 3.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::TEXT_DIM));
        ImGui::TextWrapped("by %s", he.author.c_str());
        ImGui::TextWrapped("%s", he.desc.c_str());
        ImGui::PopStyleColor();

        if (installed && he.installedPluginIdx >= 0 &&
            (!m.plugins || he.installedPluginIdx >= (int)m.plugins->size())) {
            // The entry says it installed a plugin the model no longer lists: say so rather than
            // draw a configure link into a config view that cannot open.
            ImGui::TextDisabled("installed plugin not in the model");
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (!shown) ImGui::TextDisabled(loading ? "loading the hub..."
                                            : (needle.empty() ? "(the hub is empty)" : "(no matches)"));
}

// The debugPushConfig probe: the only way the config view gets exercised offline (see the header
// note on KEWL_FAKE_PANEL). Not reached by any input path -- clicks go through the gear and the
// plugin name, and both set exactly this.
inline void debugPushConfig(int pluginIdx) {
    uiTab() = TAB_PLUGINS;
    navPush(pluginIdx);
}

// -------------------------------------------------------------------------------------------------
// The strip itself: one body window (250px, hidden when collapsed), one rail window (36px), the pair
// positioned at the window's right edge. Both are ordinary ImGui windows rather than children of
// main.cpp's full-window root -- that root carries ImGuiWindowFlags_NoMouseInputs (clicks over the
// game child must not be ImGui's business), and a NoMouseInputs parent is skipped by hit-testing
// while its children are not.
// -------------------------------------------------------------------------------------------------
inline void draw(const Model& m, const EditSink& edit) {
    ImGuiIO& io = ImGui::GetIO();
    float dispW = io.DisplaySize.x, dispH = io.DisplaySize.y;
    bool collapsed = uiCollapsed();
    if (dispW < (float)effectivePanelW() || dispH <= 0) return;    // not laid out yet / squeezed

    kbActiveFlag() = false;                        // rebuilt below from this frame's text fields
    if (kbSurrenderFlag()) {                       // the user clicked back into the game: close the
        kbSurrenderFlag() = false;                 // live field, whose deactivating click (landing
        ImGui::ClearActiveID();                    // on the game child) can never reach ImGui
    }

    ImGuiWindowFlags stripFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoScrollbar;

    // ---- body ---------------------------------------------------------------------------------
    // Hidden, not skipped, when collapsed: skipping it would leave the last frame's window record
    // around, and imgui's implicit fallback window would eat the clicks meant for the rail.
    if (!collapsed) {
        ImGui::SetNextWindowPos(ImVec2(dispW - (float)PANEL_W, 0.0f));
        ImGui::SetNextWindowSize(ImVec2((float)BODY_W, dispH));
        ImGui::Begin("##kewl.body", nullptr, stripFlags);

        // Fixed header: the client name and the bridge state, always visible -- a panel that looks
        // like a working plugin list over a dead bridge is worse than one that says so.
        ImGui::TextUnformatted("KewlKlient");
        ImGui::PushStyleColor(ImGuiCol_Text, theme::f(theme::TEXT_DIM));
        ImGui::TextWrapped("%s", m.note.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();

        ImGui::BeginChild("##kewl.scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          stripFlags | ImGuiWindowFlags_NoScrollbar);
        // A pushed index that no longer names a plugin (the game died mid-view, a shrunken model)
        // un-pushes itself rather than drawing from a stale slot.
        if (uiTop() >= 0 && (!m.plugins || uiTop() >= (int)m.plugins->size())) uiStack().clear();
        // Scroll memory: leaving the list for a config view saves the position; the first frame back
        // at the root restores it. One frame late is invisible at 30fps and keeps the logic out of
        // every click path that pops.
        static int lastTop = -2;
        int curTop = uiTop();
        if (lastTop < 0 && curTop >= 0) uiScrollSaved() = ImGui::GetScrollY();
        if (lastTop >= 0 && curTop < 0) ImGui::SetScrollY(uiScrollSaved());
        lastTop = curTop;

        if (curTop >= 0) {
            configView(m, edit, curTop);
        } else {
            switch (uiTab()) {
                case TAB_PLUGINS:  pluginsView(m, edit);  break;
                case TAB_PROFILES: profilesView(m, edit); break;
                case TAB_HUB:      hubView(m, edit);      break;
                default:           debugView(m);          break;
            }
        }
        ImGui::EndChild();

        // SidePanel.drawBody's 1px divider at BODY_W-1, between the body and the rail.
        ImVec2 bp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(bp.x + (float)BODY_W - 0.5f, bp.y),
                                            ImVec2(bp.x + (float)BODY_W - 0.5f, bp.y + dispH),
                                            theme::BORDER);
        ImGui::End();
    }

    // ---- rail ---------------------------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(dispW - (float)RAIL_W, 0.0f));
    ImGui::SetNextWindowSize(ImVec2((float)RAIL_W, dispH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::f(theme::SURFACE));   // drawIconStrip's rail
    ImGui::Begin("##kewl.rail", nullptr, stripFlags);
    ImVec2 rp = ImGui::GetWindowPos();
    for (int i = 0; i < TAB_COUNT; ++i) {
        bool active = uiTab() == i && uiTop() < 0;
        ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + (float)(i * RAIL_W)));
        char id[16];
        std::snprintf(id, sizeof id, "##nav%d", i);
        ImGui::InvisibleButton(id, ImVec2((float)RAIL_W, (float)RAIL_W));
        if (ImGui::IsItemClicked()) {
            if (uiTab() != i) { uiTab() = i; navReset(); }    // a tab switch pops to the route's root
            else if (collapsed) uiCollapsed() = false;        // the rail alone is the collapsed bar
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        if (active) {
            dl->AddRectFilled(a, b, theme::BACKGROUND);
            dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 3, b.y), theme::RL_ORANGE);
        } else if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(a, b, theme::RL_TAB_HI);            // RL_TAB hover, TopLevelConfigPanel
        }
        tabIcon(dl, ImVec2(a.x + 10, a.y + 10), i, active ? theme::RL_LABEL : theme::TEXT_DIM);
        if (tooltipsAllowed() && ImGui::IsItemHovered()) {
            static const char* names[TAB_COUNT] = { "plugins", "profiles", "plugin hub", "debug" };
            ImGui::SetTooltip("%s", names[i]);
        }
    }

    // The collapse button, pinned to the bottom of the rail: chevron pointing out when the body can
    // be hidden, in when it can be brought back. The state itself lives in uiCollapsed(); main.cpp
    // notices the change, writes the ini key and lays the game child out against the new width.
    ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + dispH - (float)RAIL_W));
    ImGui::InvisibleButton("##collapse", ImVec2((float)RAIL_W, (float)RAIL_W));
    bool collapseHover = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) uiCollapsed() = !uiCollapsed();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin();
        if (collapseHover) dl->AddRectFilled(a, ImVec2(a.x + RAIL_W, a.y + RAIL_W), theme::RL_TAB_HI);
        chevronGlyph(dl, a, !collapsed, collapseHover ? theme::RL_LABEL : theme::TEXT_DIM);
        if (tooltipsAllowed() && collapseHover)
            ImGui::SetTooltip(collapsed ? "open the sidebar" : "collapse the sidebar");
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

}  // namespace kewl_panel
