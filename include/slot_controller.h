// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"

class IPanelLifecycle;

/**
 * @brief Manages the context (left) slot in dual-panel ultrawide layout.
 *
 * Observes print state to determine what panel should appear in the context slot:
 * - Idle: empty (single-panel behavior via primary_slot only)
 * - Printing: Print Status panel reparented into context_slot
 *
 * Only active when LayoutType == ULTRAWIDE and a context_slot exists.
 * In single-slot layouts, all methods are no-ops.
 *
 * @threading Main thread only (LVGL operations)
 */
class SlotController {
  public:
    static SlotController& instance();

    // Non-copyable, non-movable (singleton)
    SlotController(const SlotController&) = delete;
    SlotController& operator=(const SlotController&) = delete;

    /**
     * @brief Initialize with slot widget references.
     * @param context_slot The left slot (nullptr if not ultrawide — single-panel mode)
     * @param primary_slot The right slot (nav-controlled panel container)
     */
    void init(lv_obj_t* context_slot, lv_obj_t* primary_slot);

    /// True if dual-slot mode is active (context_slot exists)
    [[nodiscard]] bool is_dual_mode() const {
        return context_slot_ != nullptr;
    }

    /**
     * @brief Register the print status widget for automatic context slot management.
     *
     * In dual mode, SlotController observes print state and reparents this widget
     * into the context slot when a print is active (PRINTING/PAUSED), and returns
     * it to its original parent when idle.
     *
     * @param widget The print status panel root widget
     * @param lifecycle Optional lifecycle interface for on_activate/on_deactivate calls
     */
    void set_print_status_widget(lv_obj_t* widget, IPanelLifecycle* lifecycle = nullptr);

    /**
     * @brief Immediately show print status in the context slot.
     *
     * Call this when a print is initiated (before the observer fires).
     * Handles reparenting, sizing, visibility, and lifecycle activation.
     * No-op if not in dual mode or no print status widget registered.
     */
    void show_print_status();

    /**
     * @brief Move a panel widget into the context slot.
     * Uses lv_obj_set_parent() to reparent. Shows the panel (removes HIDDEN flag).
     * No-op if not in dual mode or panel is null.
     */
    void show_in_context(lv_obj_t* panel);

    /**
     * @brief Return context panel to its original parent and clear context.
     * Hides the panel (adds HIDDEN flag) and reparents back.
     * No-op if nothing is in the context slot.
     */
    void clear_context();

    /**
     * @brief Clean shutdown — release observers and null pointers.
     */
    void shutdown();

  private:
    SlotController() = default;

    /// Handle print state changes to auto-populate context slot
    void on_print_state_changed(int state);

    /// Reparent print status widget into context slot with proper sizing
    void move_print_status_to_context();

    /// Return print status widget to its overlay parent with original sizing
    void return_print_status_to_overlay();

    lv_obj_t* context_slot_ = nullptr;
    lv_obj_t* primary_slot_ = nullptr;
    lv_obj_t* current_context_panel_ = nullptr;

    // Print status widget management
    lv_obj_t* print_status_widget_ = nullptr;
    lv_obj_t* print_status_original_parent_ = nullptr;
    lv_coord_t print_status_original_width_ = 0;
    IPanelLifecycle* print_status_lifecycle_ = nullptr;
    bool print_status_in_context_ = false;

    ObserverGuard print_state_observer_;
};
