// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/printer_state_test_access.h"
#include "app_globals.h"
#include "panel_lifecycle.h"
#include "slot_controller.h"

#include "../catch_amalgamated.hpp"

// Mock lifecycle tracker for verifying on_activate/on_deactivate calls
class MockLifecycle : public IPanelLifecycle {
  public:
    const char* get_name() const override {
        return "MockLifecycle";
    }
    void on_activate() override {
        activate_count++;
    }
    void on_deactivate() override {
        deactivate_count++;
    }

    int activate_count = 0;
    int deactivate_count = 0;
};

// Helper fixture that also initializes PrinterState subjects
class SlotControllerPrintFixture : public LVGLTestFixture {
  public:
    SlotControllerPrintFixture() {
        PrinterStateTestAccess::reset(get_printer_state());
        get_printer_state().init_subjects(false);
    }

    ~SlotControllerPrintFixture() override {
        // Shutdown observer BEFORE fixture teardown destroys LVGL objects
        SlotController::instance().shutdown();
    }

    /// Set print state and process the deferred observer callback
    void set_print_state(PrintJobState state) {
        lv_subject_set_int(get_printer_state().get_print_state_enum_subject(),
                           static_cast<int>(state));
        // observe_int_sync defers via ui_queue_update — process it now
        process_lvgl(10);
    }
};

// ============================================================================
// Basic mode tests (no PrinterState needed)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "SlotController single-panel mode", "[slot_controller]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);

    auto& sc = SlotController::instance();

    SECTION("init with null context_slot means single-panel mode") {
        sc.init(nullptr, primary_slot);
        REQUIRE_FALSE(sc.is_dual_mode());
        sc.shutdown();
    }

    SECTION("init with context_slot means dual mode") {
        auto* context_slot = lv_obj_create(screen);
        sc.init(context_slot, primary_slot);
        REQUIRE(sc.is_dual_mode());
        sc.shutdown();
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "SlotController reparenting", "[slot_controller]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* test_panel = lv_obj_create(primary_slot);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);

    SECTION("context_slot starts hidden after init") {
        REQUIRE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
    }

    SECTION("show_in_context moves panel to context slot and shows slot") {
        REQUIRE(lv_obj_get_parent(test_panel) == primary_slot);
        REQUIRE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));

        sc.show_in_context(test_panel);

        REQUIRE(lv_obj_get_parent(test_panel) == context_slot);
        REQUIRE_FALSE(lv_obj_has_flag(test_panel, LV_OBJ_FLAG_HIDDEN));
        REQUIRE_FALSE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
    }

    SECTION("clear_context returns panel to primary slot and hides slot") {
        sc.show_in_context(test_panel);
        REQUIRE_FALSE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));

        sc.clear_context();

        REQUIRE(lv_obj_get_parent(test_panel) == primary_slot);
        REQUIRE(lv_obj_has_flag(test_panel, LV_OBJ_FLAG_HIDDEN));
        REQUIRE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
    }

    SECTION("show_in_context with null panel is no-op") {
        sc.show_in_context(nullptr);
        // Should not crash
    }

    SECTION("clear_context when nothing in context is no-op") {
        sc.clear_context();
        // Should not crash
    }

    SECTION("show_in_context replaces previous context panel") {
        auto* panel_a = lv_obj_create(primary_slot);
        auto* panel_b = lv_obj_create(primary_slot);

        sc.show_in_context(panel_a);
        REQUIRE(lv_obj_get_parent(panel_a) == context_slot);

        // Showing panel_b should return panel_a to primary_slot
        sc.show_in_context(panel_b);
        REQUIRE(lv_obj_get_parent(panel_b) == context_slot);
        REQUIRE(lv_obj_get_parent(panel_a) == primary_slot);
        REQUIRE(lv_obj_has_flag(panel_a, LV_OBJ_FLAG_HIDDEN));
    }

    SECTION("show_in_context with same panel is no-op") {
        sc.show_in_context(test_panel);
        REQUIRE(lv_obj_get_parent(test_panel) == context_slot);

        // Showing same panel again should not crash or re-hide
        sc.show_in_context(test_panel);
        REQUIRE(lv_obj_get_parent(test_panel) == context_slot);
        REQUIRE_FALSE(lv_obj_has_flag(test_panel, LV_OBJ_FLAG_HIDDEN));
    }

    sc.shutdown();
}

TEST_CASE_METHOD(LVGLTestFixture, "SlotController ignores ops in single mode",
                 "[slot_controller]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* test_panel = lv_obj_create(primary_slot);

    auto& sc = SlotController::instance();
    sc.init(nullptr, primary_slot);

    SECTION("show_in_context is no-op without context slot") {
        auto* orig_parent = lv_obj_get_parent(test_panel);
        sc.show_in_context(test_panel);
        REQUIRE(lv_obj_get_parent(test_panel) == orig_parent);
    }

    sc.shutdown();
}

// ============================================================================
// Print status widget registration
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController print status registration",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen); // Simulates overlay parented to screen

    auto& sc = SlotController::instance();

    SECTION("set_print_status_widget in single mode is no-op") {
        sc.init(nullptr, primary_slot);
        MockLifecycle mock;
        sc.set_print_status_widget(ps_widget, &mock);
        // Should not crash, observer not created (no context slot)
    }

    SECTION("set_print_status_widget with null widget is no-op") {
        sc.init(context_slot, primary_slot);
        sc.set_print_status_widget(nullptr);
        // Should not crash
    }

    SECTION("set_print_status_widget saves original parent") {
        sc.init(context_slot, primary_slot);
        sc.set_print_status_widget(ps_widget);
        // Widget should still be parented to screen after registration
        REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    }
}

// ============================================================================
// Print state observer — reparenting on state changes
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController reparents print status on PRINTING state",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN); // Overlays start hidden

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Initially: widget on screen, context_slot hidden
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));

    // Transition to PRINTING
    set_print_state(PrintJobState::PRINTING);

    // Widget reparented, context slot visible
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE_FALSE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
    REQUIRE_FALSE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController reparents print status on PAUSED state",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // PAUSED should also trigger context slot display
    set_print_state(PrintJobState::PAUSED);

    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE_FALSE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController returns print status on STANDBY",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Move to context slot
    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    // Return to STANDBY
    set_print_state(PrintJobState::STANDBY);

    // Should return to original parent (screen), hidden, context slot hidden
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController returns print status on COMPLETE",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    set_print_state(PrintJobState::COMPLETE);

    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController returns print status on CANCELLED",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::CANCELLED);

    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController returns print status on ERROR",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::ERROR);

    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

// ============================================================================
// Print state observer — sizing adjustments
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController adjusts width when moving to context slot",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    lv_obj_set_size(context_slot, 888, 440); // Simulate ultrawide half-width
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_set_width(ps_widget, 700); // Simulate overlay_panel_width
    lv_obj_set_align(ps_widget, LV_ALIGN_RIGHT_MID);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Move to context slot
    set_print_state(PrintJobState::PRINTING);

    // Width should match context slot content width, alignment should be default
    auto expected_w = lv_obj_get_content_width(context_slot);
    REQUIRE(lv_obj_get_style_width(ps_widget, LV_PART_MAIN) == expected_w);
    REQUIRE(lv_obj_get_style_align(ps_widget, LV_PART_MAIN) == LV_ALIGN_DEFAULT);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController restores original width when returning from context",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_set_width(ps_widget, 700);
    lv_obj_set_align(ps_widget, LV_ALIGN_RIGHT_MID);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Move to context, then back
    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::STANDBY);

    // Original width and alignment should be restored
    REQUIRE(lv_obj_get_style_width(ps_widget, LV_PART_MAIN) == 700);
    REQUIRE(lv_obj_get_style_align(ps_widget, LV_PART_MAIN) == LV_ALIGN_RIGHT_MID);
}

// ============================================================================
// Print state observer — lifecycle callbacks
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController calls on_activate when print starts",
                 "[slot_controller][print_status][lifecycle]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    REQUIRE(mock.activate_count == 0);

    set_print_state(PrintJobState::PRINTING);

    REQUIRE(mock.activate_count == 1);
    REQUIRE(mock.deactivate_count == 0);
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController calls on_deactivate when print ends",
                 "[slot_controller][print_status][lifecycle]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::STANDBY);

    REQUIRE(mock.activate_count == 1);
    REQUIRE(mock.deactivate_count == 1);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController lifecycle across multiple print cycles",
                 "[slot_controller][print_status][lifecycle]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    // First print cycle
    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::COMPLETE);

    // Second print cycle
    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::CANCELLED);

    REQUIRE(mock.activate_count == 2);
    REQUIRE(mock.deactivate_count == 2);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController no extra lifecycle calls on redundant state",
                 "[slot_controller][print_status][lifecycle]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    // Repeated PRINTING should not trigger extra activate calls
    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::PRINTING);

    REQUIRE(mock.activate_count == 1);
    REQUIRE(mock.deactivate_count == 0);

    // Repeated STANDBY should not trigger extra deactivate calls
    set_print_state(PrintJobState::STANDBY);
    set_print_state(PrintJobState::STANDBY);
    set_print_state(PrintJobState::STANDBY);

    REQUIRE(mock.activate_count == 1);
    REQUIRE(mock.deactivate_count == 1);
}

// ============================================================================
// Print state transitions — PRINTING <-> PAUSED
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController stays in context when transitioning PRINTING to PAUSED",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    // Pausing should keep it in context (still active)
    set_print_state(PrintJobState::PAUSED);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE_FALSE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));

    // No extra lifecycle calls for PRINTING -> PAUSED
    REQUIRE(mock.activate_count == 1);
    REQUIRE(mock.deactivate_count == 0);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController stays in context when resuming PAUSED to PRINTING",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    set_print_state(PrintJobState::PRINTING);
    set_print_state(PrintJobState::PAUSED);
    set_print_state(PrintJobState::PRINTING);

    // Should still be in context slot throughout
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE_FALSE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

// ============================================================================
// Shutdown safety
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController shutdown returns print status to original parent",
                 "[slot_controller][print_status][shutdown]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Move to context
    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    // Shutdown should return it
    sc.shutdown();
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController shutdown when not in context is safe",
                 "[slot_controller][print_status][shutdown]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // Never moved to context — shutdown should still be safe
    sc.shutdown();
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
}

// ============================================================================
// clear_context with print status panel (special return path)
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController clear_context returns print status to screen not primary_slot",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    set_print_state(PrintJobState::PRINTING);

    // Manual clear_context should return to screen (original parent), not primary_slot
    sc.clear_context();
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController show_in_context replacing print status returns it to screen",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    auto* other_panel = lv_obj_create(primary_slot);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    // Showing another panel should return print status to screen
    sc.show_in_context(other_panel);
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
    REQUIRE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_get_parent(other_panel) == context_slot);

    // on_deactivate should have been called
    REQUIRE(mock.deactivate_count == 1);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture, "SlotController handles null lifecycle gracefully",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, nullptr); // No lifecycle

    // Should work without lifecycle callbacks
    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);

    set_print_state(PrintJobState::STANDBY);
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
}

// ============================================================================
// show_print_status() — explicit immediate show
// ============================================================================

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "show_print_status shows widget in context slot immediately",
                 "[slot_controller][print_status][show_print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    lv_obj_set_size(context_slot, 888, 440);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    sc.show_print_status();

    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE_FALSE(lv_obj_has_flag(ps_widget, LV_OBJ_FLAG_HIDDEN));
    REQUIRE_FALSE(lv_obj_has_flag(context_slot, LV_OBJ_FLAG_HIDDEN));
    auto expected_w = lv_obj_get_content_width(context_slot);
    REQUIRE(lv_obj_get_style_width(ps_widget, LV_PART_MAIN) == expected_w);
    REQUIRE(mock.activate_count == 1);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "show_print_status then observer PRINTING is no-op (already shown)",
                 "[slot_controller][print_status][show_print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    // Drain initial STANDBY observer callback from set_print_status_widget
    process_lvgl(10);

    // Explicit show (simulates PrintSelectPanel callback)
    sc.show_print_status();
    REQUIRE(mock.activate_count == 1);

    // Observer fires later with PRINTING — should be no-op (already in context)
    set_print_state(PrintJobState::PRINTING);
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
    REQUIRE(mock.activate_count == 1); // NOT 2
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "show_print_status is no-op in single mode",
                 "[slot_controller][print_status][show_print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);

    auto& sc = SlotController::instance();
    sc.init(nullptr, primary_slot);
    sc.set_print_status_widget(ps_widget);

    sc.show_print_status();
    REQUIRE(lv_obj_get_parent(ps_widget) == screen); // Unchanged
}

TEST_CASE_METHOD(SlotControllerPrintFixture, "show_print_status is idempotent",
                 "[slot_controller][print_status][show_print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);
    lv_obj_add_flag(ps_widget, LV_OBJ_FLAG_HIDDEN);

    MockLifecycle mock;
    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget, &mock);

    sc.show_print_status();
    sc.show_print_status();
    sc.show_print_status();

    REQUIRE(mock.activate_count == 1); // Only once
    REQUIRE(lv_obj_get_parent(ps_widget) == context_slot);
}

TEST_CASE_METHOD(SlotControllerPrintFixture,
                 "SlotController STANDBY without prior PRINTING is no-op",
                 "[slot_controller][print_status]") {
    auto* screen = test_screen();
    auto* primary_slot = lv_obj_create(screen);
    auto* context_slot = lv_obj_create(screen);
    auto* ps_widget = lv_obj_create(screen);

    auto& sc = SlotController::instance();
    sc.init(context_slot, primary_slot);
    sc.set_print_status_widget(ps_widget);

    // PrinterState starts at STANDBY(0) — observer fires immediately with STANDBY
    // This should be a no-op since nothing is in context
    REQUIRE(lv_obj_get_parent(ps_widget) == screen);
}
