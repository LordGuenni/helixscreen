// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slot_controller.h"

#include <spdlog/spdlog.h>

SlotController& SlotController::instance() {
    static SlotController inst;
    return inst;
}

void SlotController::init(lv_obj_t* context_slot, lv_obj_t* primary_slot) {
    context_slot_ = context_slot;
    primary_slot_ = primary_slot;
    current_context_panel_ = nullptr;

    if (!context_slot_) {
        spdlog::debug("[SlotController] No context slot — single-panel mode");
        return;
    }

    spdlog::info("[SlotController] Dual-panel mode active");

    // Future: observe print phase to auto-populate context slot
    // print_phase_observer_ = helix::ui::observe_int_sync<SlotController>(
    //     PrinterState::instance().get_print_start_phase_subject(),
    //     this,
    //     [](SlotController* self, int phase) {
    //         self->handle_print_phase_change(phase);
    //     });
}

void SlotController::show_in_context(lv_obj_t* panel) {
    if (!context_slot_ || !panel)
        return;

    // Reparent into context slot
    lv_obj_set_parent(panel, context_slot_);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    current_context_panel_ = panel;

    spdlog::debug("[SlotController] Panel {} moved to context slot", (void*)panel);
}

void SlotController::clear_context() {
    if (!current_context_panel_ || !primary_slot_)
        return;

    // Return to primary slot, hidden (NavigationManager controls visibility)
    lv_obj_set_parent(current_context_panel_, primary_slot_);
    lv_obj_add_flag(current_context_panel_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[SlotController] Panel {} returned to primary slot",
                  (void*)current_context_panel_);
    current_context_panel_ = nullptr;
}

void SlotController::shutdown() {
    print_phase_observer_.reset();
    context_slot_ = nullptr;
    primary_slot_ = nullptr;
    current_context_panel_ = nullptr;
    spdlog::debug("[SlotController] Shutdown complete");
}
