// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"

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
     * @brief Move a panel widget into the context slot.
     * Uses lv_obj_set_parent() to reparent. Shows the panel (removes HIDDEN flag).
     * No-op if not in dual mode or panel is null.
     */
    void show_in_context(lv_obj_t* panel);

    /**
     * @brief Return context panel to primary slot and clear context.
     * Hides the panel (adds HIDDEN flag) and reparents to primary_slot.
     * No-op if nothing is in the context slot.
     */
    void clear_context();

    /**
     * @brief Clean shutdown — release observers and null pointers.
     */
    void shutdown();

  private:
    SlotController() = default;

    lv_obj_t* context_slot_ = nullptr;
    lv_obj_t* primary_slot_ = nullptr;
    lv_obj_t* current_context_panel_ = nullptr;

    ObserverGuard print_phase_observer_;
};
