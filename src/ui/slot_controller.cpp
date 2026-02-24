// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slot_controller.h"

#include "app_globals.h"
#include "observer_factory.h"
#include "panel_lifecycle.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

using helix::PrintJobState;

SlotController& SlotController::instance() {
    static SlotController inst;
    return inst;
}

void SlotController::init(lv_obj_t* context_slot, lv_obj_t* primary_slot) {
    context_slot_ = context_slot;
    primary_slot_ = primary_slot;
    current_context_panel_ = nullptr;
    print_status_widget_ = nullptr;
    print_status_original_parent_ = nullptr;
    print_status_lifecycle_ = nullptr;
    print_status_in_context_ = false;

    if (!context_slot_) {
        spdlog::debug("[SlotController] No context slot — single-panel mode");
        return;
    }

    // Start with context slot hidden — it only appears when populated
    lv_obj_add_flag(context_slot_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(context_slot_, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_remove_flag(context_slot_, LV_OBJ_FLAG_SCROLLABLE);

    spdlog::info("[SlotController] Dual-panel mode active");
}

void SlotController::set_print_status_widget(lv_obj_t* widget, IPanelLifecycle* lifecycle) {
    print_status_widget_ = widget;
    print_status_lifecycle_ = lifecycle;

    if (!widget || !context_slot_) {
        return;
    }

    // Save original parent and width for restoration
    print_status_original_parent_ = lv_obj_get_parent(widget);
    print_status_original_width_ = lv_obj_get_style_width(widget, LV_PART_MAIN);

    spdlog::debug("[SlotController] Print status widget registered (parent={}, width={})",
                  (void*)print_status_original_parent_, print_status_original_width_);

    // Observe print job state for automatic context slot management
    print_state_observer_ = helix::ui::observe_int_sync<SlotController>(
        get_printer_state().get_print_state_enum_subject(), this,
        [](SlotController* self, int state) { self->on_print_state_changed(state); });
}

void SlotController::move_print_status_to_context() {
    // Show context slot and force layout so it has real pixel dimensions
    // before reparenting (otherwise percentage-based sizes resolve to 0)
    lv_obj_remove_flag(context_slot_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(context_slot_);

    // Reparent widget into context slot
    lv_obj_set_parent(print_status_widget_, context_slot_);

    // Set explicit pixel dimensions from the slot — lv_pct(100) doesn't
    // override the XML-set pixel width (#overlay_panel_width)
    auto slot_w = lv_obj_get_content_width(context_slot_);
    auto slot_h = lv_obj_get_content_height(context_slot_);
    lv_obj_set_width(print_status_widget_, slot_w);
    lv_obj_set_height(print_status_widget_, slot_h);
    lv_obj_set_align(print_status_widget_, LV_ALIGN_DEFAULT);
    lv_obj_remove_flag(print_status_widget_, LV_OBJ_FLAG_HIDDEN);

    // Hide the overlay back button — not needed in inline context mode
    lv_obj_t* back_btn = lv_obj_find_by_name(print_status_widget_, "back_button");
    if (back_btn) {
        lv_obj_add_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
    }

    spdlog::debug("[SlotController] Context slot: {}x{}, widget set to: {}x{}",
                  lv_obj_get_width(context_slot_), lv_obj_get_height(context_slot_), slot_w,
                  slot_h);

    current_context_panel_ = print_status_widget_;
    print_status_in_context_ = true;

    // Notify lifecycle (resumes gcode viewer rendering, etc.)
    if (print_status_lifecycle_) {
        print_status_lifecycle_->on_activate();
    }
}

void SlotController::return_print_status_to_overlay() {
    // Notify lifecycle before reparent
    if (print_status_lifecycle_) {
        print_status_lifecycle_->on_deactivate();
    }

    // Restore to original parent with overlay sizing
    lv_obj_set_parent(print_status_widget_, print_status_original_parent_);
    lv_obj_set_width(print_status_widget_, print_status_original_width_);
    lv_obj_set_height(print_status_widget_, lv_pct(100));
    lv_obj_set_align(print_status_widget_, LV_ALIGN_RIGHT_MID);
    lv_obj_add_flag(print_status_widget_, LV_OBJ_FLAG_HIDDEN);

    // Restore the overlay back button
    lv_obj_t* back_btn = lv_obj_find_by_name(print_status_widget_, "back_button");
    if (back_btn) {
        lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide context slot container when empty
    lv_obj_add_flag(context_slot_, LV_OBJ_FLAG_HIDDEN);

    current_context_panel_ = nullptr;
    print_status_in_context_ = false;
}

void SlotController::show_print_status() {
    if (!context_slot_ || !print_status_widget_ || print_status_in_context_)
        return;

    spdlog::info("[SlotController] Showing print status in context slot (explicit)");

    // If something else is in the context slot, clear it first
    if (current_context_panel_ && current_context_panel_ != print_status_widget_) {
        clear_context();
    }

    move_print_status_to_context();
}

void SlotController::on_print_state_changed(int /*state*/) {
    // Read CURRENT subject value — deferred callbacks may deliver stale state
    auto job_state = static_cast<PrintJobState>(
        lv_subject_get_int(get_printer_state().get_print_state_enum_subject()));

    bool print_active =
        (job_state == PrintJobState::PRINTING || job_state == PrintJobState::PAUSED);

    if (print_active && !print_status_in_context_) {
        spdlog::info("[SlotController] Print active — showing print status in context slot");
        move_print_status_to_context();
    } else if (!print_active && print_status_in_context_) {
        spdlog::info("[SlotController] Print ended — returning print status to overlay parent");
        return_print_status_to_overlay();
    }
}

void SlotController::show_in_context(lv_obj_t* panel) {
    if (!context_slot_ || !panel)
        return;

    // If a different panel is already in context, return it first
    if (current_context_panel_ && current_context_panel_ != panel) {
        clear_context();
    }

    // Show context slot container, then reparent widget into it
    // Imperative visibility is needed here because reparenting is structural —
    // XML bindings can't express "show when in this parent"
    lv_obj_remove_flag(context_slot_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_parent(panel, context_slot_);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    current_context_panel_ = panel;

    spdlog::debug("[SlotController] Panel {} moved to context slot", (void*)panel);
}

void SlotController::clear_context() {
    if (!current_context_panel_ || !primary_slot_)
        return;

    // If print status is in context, use its own return path
    if (current_context_panel_ == print_status_widget_ && print_status_in_context_) {
        return_print_status_to_overlay();
        return;
    } else {
        // Return to primary slot, hidden (NavigationManager controls visibility)
        lv_obj_set_parent(current_context_panel_, primary_slot_);
        lv_obj_add_flag(current_context_panel_, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide context slot container when empty
    lv_obj_add_flag(context_slot_, LV_OBJ_FLAG_HIDDEN);

    spdlog::debug("[SlotController] Panel {} returned from context slot",
                  (void*)current_context_panel_);
    current_context_panel_ = nullptr;
}

void SlotController::shutdown() {
    // Return print status to original parent if still in context
    if (print_status_in_context_ && print_status_widget_ && print_status_original_parent_) {
        lv_obj_set_parent(print_status_widget_, print_status_original_parent_);
        lv_obj_add_flag(print_status_widget_, LV_OBJ_FLAG_HIDDEN);
        print_status_in_context_ = false;
    }

    print_state_observer_.reset();
    context_slot_ = nullptr;
    primary_slot_ = nullptr;
    current_context_panel_ = nullptr;
    print_status_widget_ = nullptr;
    print_status_original_parent_ = nullptr;
    print_status_lifecycle_ = nullptr;
    spdlog::debug("[SlotController] Shutdown complete");
}
