# Dual-Panel Slot Layout for Ultrawide Displays

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Use XML slots to support a dual-panel layout on ultrawide displays (>2.5:1 ratio, e.g. 1920x440), where the left slot shows a persistent context panel (print status during prints, home dashboard when idle) and the right slot shows the nav-selected panel.

**Architecture:** Introduce a `content_layout` XML component with two variants — single-slot (standard/all other layouts) and dual-slot (ultrawide). The existing `LayoutManager::resolve_xml_path()` already handles per-layout XML overrides (`ui_xml/ultrawide/` directory). NavigationManager stays largely unchanged — it manages the right slot (same as today's panel_container). A new lightweight `SlotController` manages the left slot based on print state. Panels are reparented between slots using `lv_obj_set_parent()` rather than duplicated.

**Tech Stack:** LVGL 9.5, helix-xml slots (`ComponentName-slot_name` syntax), existing LayoutManager + breakpoint system

---

## Background: Existing Infrastructure

These systems already exist and this plan builds on them:

- **LayoutManager** (`include/layout_manager.h`, `src/layout_manager.cpp`): Detects `LayoutType::ULTRAWIDE` when aspect ratio > 2.5:1. `resolve_xml_path("foo.xml")` returns `ui_xml/ultrawide/foo.xml` if it exists, else falls back to `ui_xml/foo.xml`.
- **XML Slots** (`lib/helix-xml/src/xml/lv_xml.c:908-924`): Fully implemented but unused. Syntax: `<component-slot_name>...children...</component-slot_name>`. Splits on hyphen, finds named placeholder via `lv_obj_find_by_name()`.
- **XML Registration** (`src/xml_registration.cpp:127-131`): All components already go through `register_xml()` which calls `LayoutManager::resolve_xml_path()`.
- **Ultrawide overrides**: `ui_xml/ultrawide/home_panel.xml` already exists as a layout-specific override.

## Design Decisions

**Why reparent, not duplicate?** Panels are heavyweight singletons with observers, subjects, and C++ state. Duplicating them would require a major refactor of PanelBase and every panel. `lv_obj_set_parent()` exists in LVGL 9.5 (`lib/lvgl/src/core/lv_obj_tree.h:83`) and moves an object cleanly between parents.

**Why not modify NavigationManager's core logic?** It already handles show/hide for one set of panels perfectly. For dual mode, the left slot is state-driven (print active vs idle), not nav-driven. A separate, small controller keeps concerns clean.

**Why a content_layout component (not modifying app_layout)?** The navbar stays the same across all layouts. Only the content area changes between single and dual. Isolating this in a slotted component keeps the change minimal and testable.

---

## Task 1: Create `content_layout` Component (Standard — Single Slot)

**Files:**
- Create: `ui_xml/components/content_layout.xml`

This is the default (non-ultrawide) layout. It has a single slot that replaces today's `panel_container`.

**Step 1: Create the component**

```xml
<?xml version="1.0"?>
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<component>
  <view extends="lv_obj" width="100%" height="100%"
        style_bg_opa="0%" style_pad_all="0" style_border_width="0">
    <!-- Single slot: all panels live here, NavigationManager controls visibility -->
    <lv_obj name="primary_slot" width="100%" height="100%"
            style_bg_opa="0%" style_pad_all="0" style_border_width="0"/>
  </view>
</component>
```

**Step 2: Register the component**

In `src/xml_registration.cpp`, add `register_xml("components/content_layout.xml");` in the components section (before panels that use it, after semantic widgets).

**Step 3: Verify it loads**

Build and run: `make -j && ./build/bin/helix-screen --test -vv 2>&1 | head -100`
Look for: no XML registration errors for content_layout.

**Step 4: Commit**

```
feat(layout): add content_layout component with single primary slot
```

---

## Task 2: Create Ultrawide `content_layout` Override (Dual Slot)

**Files:**
- Create: `ui_xml/ultrawide/components/content_layout.xml`

**Step 1: Create the ultrawide variant**

```xml
<?xml version="1.0"?>
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<component>
  <view extends="lv_obj" width="100%" height="100%"
        style_bg_opa="0%" style_pad_all="0" style_border_width="0"
        flex_flow="row" style_pad_column="0">
    <!-- Left slot: persistent context panel (print status or home dashboard) -->
    <lv_obj name="context_slot" height="100%" flex_grow="1"
            style_bg_opa="0%" style_pad_all="0" style_border_width="0"/>
    <!-- Right slot: nav-selected panel (same role as today's panel_container) -->
    <lv_obj name="primary_slot" height="100%" flex_grow="1"
            style_bg_opa="0%" style_pad_all="0" style_border_width="0"/>
  </view>
</component>
```

Note: Both slots use `flex_grow="1"` for equal 50/50 split. Can be tuned later (e.g., context_slot could be narrower).

**Step 2: Verify LayoutManager resolution**

The existing `resolve_xml_path("components/content_layout.xml")` should return `ui_xml/ultrawide/components/content_layout.xml` when in ultrawide mode. Verify by adding a trace log or checking with `--layout ultrawide --test -vv`.

**Step 3: Commit**

```
feat(layout): add ultrawide dual-slot content_layout override
```

---

## Task 3: Wire `content_layout` into `app_layout.xml`

**Files:**
- Modify: `ui_xml/app_layout.xml`
- Modify: `src/application/application.cpp:974-998`

**Step 1: Update app_layout.xml**

Replace the raw `panel_container` with the `content_layout` component:

```xml
<!-- BEFORE -->
<lv_obj name="content_area" height="100%" flex_grow="1" style_bg_opa="0%" style_pad_all="0" flex_flow="column">
  <lv_obj name="panel_container" width="100%" flex_grow="1" style_bg_opa="0%" style_pad_all="0">
    <home_panel name="home_panel"/>
    <print_select_panel name="print_select_panel"/>
    <controls_panel name="controls_panel"/>
    <filament_panel name="filament_panel"/>
    <settings_panel name="settings_panel"/>
    <advanced_panel name="advanced_panel"/>
  </lv_obj>
</lv_obj>

<!-- AFTER -->
<lv_obj name="content_area" height="100%" flex_grow="1" style_bg_opa="0%" style_pad_all="0" flex_flow="column">
  <content_layout name="content_layout" width="100%" flex_grow="1">
    <content_layout-primary_slot>
      <home_panel name="home_panel"/>
      <print_select_panel name="print_select_panel"/>
      <controls_panel name="controls_panel"/>
      <filament_panel name="filament_panel"/>
      <settings_panel name="settings_panel"/>
      <advanced_panel name="advanced_panel"/>
    </content_layout-primary_slot>
  </content_layout>
</lv_obj>
```

All panels go into `primary_slot`. In standard mode, this is the only slot. In ultrawide mode, the context_slot exists but starts empty — Task 5 will populate it.

**Step 2: Update Application::create_ui() panel discovery**

In `src/application/application.cpp` around line 987, update the panel container lookup:

```cpp
// BEFORE:
lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "panel_container");

// AFTER:
// Panels live inside content_layout's primary_slot
// lv_obj_find_by_name searches recursively, so find primary_slot through content_layout
lv_obj_t* panel_container = lv_obj_find_by_name(content_area, "primary_slot");
```

**Step 3: Build and test**

```bash
make -j && ./build/bin/helix-screen --test -vv
```

Verify: all 6 panels still load, navigation works, no visual regressions.

**Step 4: Run existing tests**

```bash
make test && ./build/bin/helix-tests "[navigation]"
```

**Step 5: Commit**

```
refactor(layout): use content_layout slot component for panel container
```

---

## Task 4: Test Dual Layout Renders (Ultrawide Mode)

**Files:**
- None (manual testing with `--layout` flag)

**Step 1: Run in ultrawide mode**

```bash
./build/bin/helix-screen --test -vv --layout ultrawide --resolution 1920x440
```

Verify:
- The dual-slot layout renders (two columns visible)
- Panels appear in the right slot (primary_slot)
- Left slot (context_slot) is empty but present
- Navigation switching still works

**Step 2: Run in standard mode to verify no regression**

```bash
./build/bin/helix-screen --test -vv --resolution 800x480
```

---

## Task 5: Create SlotController for Context Slot

**Files:**
- Create: `include/slot_controller.h`
- Create: `src/ui/slot_controller.cpp`

The SlotController manages what appears in the `context_slot` (left side) during ultrawide mode. It observes print state and reparents panels between slots.

**Step 1: Write the failing test**

Create `tests/test_slot_controller.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "slot_controller.h"

// Test that SlotController correctly determines which panel belongs in context slot
TEST_CASE("SlotController context panel selection", "[slot_controller]") {
    SECTION("idle state shows no context panel") {
        // When not printing, context slot should be empty (single-panel behavior)
        // SlotController::desired_context_panel(PrintPhase::IDLE) == PanelId::Home
        // (or nullopt if we want context slot empty when idle)
    }

    SECTION("printing state shows print status in context") {
        // When printing, context slot should show print status
    }
}
```

Note: Full test implementation depends on how much of SlotController is pure logic vs LVGL-dependent. The state machine (print phase → desired panel) should be testable without LVGL.

**Step 2: Implement SlotController**

```cpp
// include/slot_controller.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"
#include "lvgl/lvgl.h"
#include <optional>

namespace helix {
enum class PanelId;
}

/**
 * @brief Manages the context (left) slot in dual-panel ultrawide layout.
 *
 * Observes print state to determine what panel should appear in the context slot:
 * - Idle: Home dashboard (or empty — TBD)
 * - Printing: Print Status panel
 *
 * Only active when LayoutType == ULTRAWIDE. In single-slot layouts,
 * this class does nothing.
 */
class SlotController {
  public:
    static SlotController& instance();

    /**
     * @brief Initialize with slot widget references.
     * @param context_slot The left slot (may be nullptr if not ultrawide)
     * @param primary_slot The right slot (nav-controlled panel container)
     */
    void init(lv_obj_t* context_slot, lv_obj_t* primary_slot);

    /// True if dual-slot mode is active
    bool is_dual_mode() const { return context_slot_ != nullptr; }

    /**
     * @brief Move a panel widget into the context slot.
     * Uses lv_obj_set_parent() to reparent.
     */
    void show_in_context(lv_obj_t* panel);

    /**
     * @brief Return context panel to primary slot and clear context.
     */
    void clear_context();

    void shutdown();

  private:
    SlotController() = default;

    void handle_print_phase_change(int phase);

    lv_obj_t* context_slot_ = nullptr;
    lv_obj_t* primary_slot_ = nullptr;
    lv_obj_t* current_context_panel_ = nullptr;

    ObserverGuard print_phase_observer_;
};
```

**Step 3: Implement the observer logic**

```cpp
// src/ui/slot_controller.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "slot_controller.h"

#include "layout_manager.h"
#include "observer_factory.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

using namespace helix;

SlotController& SlotController::instance() {
    static SlotController inst;
    return inst;
}

void SlotController::init(lv_obj_t* context_slot, lv_obj_t* primary_slot) {
    context_slot_ = context_slot;
    primary_slot_ = primary_slot;

    if (!context_slot_) {
        spdlog::debug("[SlotController] No context slot — single-panel mode");
        return;
    }

    spdlog::info("[SlotController] Dual-panel mode active");

    // Observe print phase to decide context slot content
    print_phase_observer_ = helix::ui::observe_int_sync<SlotController>(
        PrinterState::instance().get_print_start_phase_subject(),
        this,
        [](SlotController* self, int phase) {
            self->handle_print_phase_change(phase);
        });
}

void SlotController::handle_print_phase_change(int phase) {
    // PrintStartPhase: IDLE=0, COMPLETE=10
    // Anything > IDLE means a print is active
    bool printing = (phase > 0 && phase < 10); // Between IDLE and COMPLETE

    if (printing && !current_context_panel_) {
        // TODO: Reparent print status panel into context_slot
        // This requires getting the print status overlay widget from PanelFactory
        // and converting it from an overlay to an inline panel for ultrawide
        spdlog::info("[SlotController] Print active — should show print status in context slot");
    } else if (!printing && current_context_panel_) {
        clear_context();
    }
}

void SlotController::show_in_context(lv_obj_t* panel) {
    if (!context_slot_ || !panel) return;

    // Reparent into context slot
    lv_obj_set_parent(panel, context_slot_);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    current_context_panel_ = panel;

    spdlog::debug("[SlotController] Panel {} moved to context slot", (void*)panel);
}

void SlotController::clear_context() {
    if (!current_context_panel_ || !primary_slot_) return;

    // Return to primary slot
    lv_obj_set_parent(current_context_panel_, primary_slot_);
    lv_obj_add_flag(current_context_panel_, LV_OBJ_FLAG_HIDDEN);
    current_context_panel_ = nullptr;

    spdlog::debug("[SlotController] Context slot cleared");
}

void SlotController::shutdown() {
    print_phase_observer_.reset();
    context_slot_ = nullptr;
    primary_slot_ = nullptr;
    current_context_panel_ = nullptr;
}
```

**Step 4: Commit**

```
feat(layout): add SlotController for dual-panel context management
```

---

## Task 6: Wire SlotController into Application Startup

**Files:**
- Modify: `src/application/application.cpp:974-1012`

**Step 1: Initialize SlotController after panel discovery**

In `Application::create_ui()`, after finding `primary_slot`, also look for `context_slot`:

```cpp
// Find slots
lv_obj_t* primary_slot = lv_obj_find_by_name(content_area, "primary_slot");
if (!primary_slot) {
    spdlog::error("[Application] Failed to find primary_slot");
    return false;
}

// context_slot only exists in dual layouts (e.g., ultrawide)
lv_obj_t* context_slot = lv_obj_find_by_name(content_area, "context_slot");
if (context_slot) {
    spdlog::info("[Application] Dual-panel layout detected (context_slot found)");
}

// Initialize slot controller
SlotController::instance().init(context_slot, primary_slot);

// Initialize panels (unchanged — uses primary_slot as panel_container)
m_panels = std::make_unique<PanelFactory>();
if (!m_panels->find_panels(primary_slot)) {
    return false;
}
m_panels->setup_panels(m_screen);
```

**Step 2: Add shutdown call**

In `Application::shutdown()`, add `SlotController::instance().shutdown();` before NavigationManager shutdown.

**Step 3: Build and test both modes**

```bash
make -j
./build/bin/helix-screen --test -vv                                  # standard
./build/bin/helix-screen --test -vv --layout ultrawide -r 1920x440   # ultrawide
```

**Step 4: Commit**

```
feat(layout): wire SlotController into application lifecycle
```

---

## Task 7: Print Status as Inline Panel for Context Slot

This is the most complex task. Currently, PrintStatusPanel is an `OverlayBase` — a full-screen overlay. For the context slot, we need it to render inline (as a child of context_slot, not the screen).

**This task needs design exploration.** Two approaches:

### Option A: Dual-mode PrintStatusPanel
PrintStatusPanel gains a `create_inline(lv_obj_t* parent)` method that creates the same XML component but parented to a slot instead of the screen. The panel can exist in both overlay mode (standard screens) and inline mode (ultrawide context slot).

### Option B: Separate context panel
Create a lightweight `print_context_panel` XML component purpose-built for the context slot. It's not the full print status overlay — it's a condensed dashboard view. The full overlay is still available via tap.

**Recommendation:** Start with Option A for parity, but Option B might ultimately be the better UX (condensed info at a glance, tap to expand to full overlay). This task should be scoped as a spike to determine the right approach.

**Step 1: Spike — try reparenting the print status overlay**

In SlotController, when print starts, try:
```cpp
// Get print status panel widget from PanelFactory
lv_obj_t* ps = application.panels().print_status_panel();
show_in_context(ps);
```

Test if a panel designed as an overlay renders correctly when reparented into a smaller container. Key concerns:
- Does it respect the slot's width/height constraints?
- Do its internal flex layouts adapt?
- Do observers and subjects still fire correctly?

**Step 2: Based on spike results, implement the chosen approach**

(Details TBD based on spike findings)

**Step 3: Commit**

```
feat(layout): show print status in ultrawide context slot during prints
```

---

## Open Questions (Resolve During Implementation)

1. **Context slot when idle (not printing):** Show Home panel in left slot? Or leave it empty and collapse to single-panel? The Home dashboard in the left slot during idle would require Home to exist in two places simultaneously (or reparent it). Starting with empty context_slot when idle is simpler.

2. **50/50 split or different ratio?** The plan uses `flex_grow="1"` on both slots. A 40/60 or 35/65 split might work better for context vs nav panel. Tune after visual testing.

3. **Context slot animations:** When print starts and print status slides into context slot, should there be a transition animation? Or instant?

4. **Overlay behavior in dual mode:** When a full-screen overlay is pushed (like temperature editor), it should still cover both slots + backdrop. Verify this works — overlays are screen-level siblings, so they should cover everything naturally.

5. **Print status dual identity:** Can the same `lv_obj_t*` work as both an overlay (when tapped from standard layout) and an inline panel (when in context slot)? Or do we need separate instances? The spike in Task 7 will answer this.

---

## Not In Scope (Future Work)

- User-configurable slot content (pinning arbitrary panels)
- More than two slots
- Portrait dual-panel (top/bottom split)
- Slot transitions/animations between single↔dual mode
- Runtime layout switching (changing from standard to ultrawide without restart)
