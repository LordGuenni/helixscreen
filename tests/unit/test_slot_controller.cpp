// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "slot_controller.h"

#include "../catch_amalgamated.hpp"

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

    SECTION("show_in_context moves panel to context slot") {
        REQUIRE(lv_obj_get_parent(test_panel) == primary_slot);

        sc.show_in_context(test_panel);

        REQUIRE(lv_obj_get_parent(test_panel) == context_slot);
        REQUIRE_FALSE(lv_obj_has_flag(test_panel, LV_OBJ_FLAG_HIDDEN));
    }

    SECTION("clear_context returns panel to primary slot") {
        sc.show_in_context(test_panel);
        REQUIRE(lv_obj_get_parent(test_panel) == context_slot);

        sc.clear_context();

        REQUIRE(lv_obj_get_parent(test_panel) == primary_slot);
        REQUIRE(lv_obj_has_flag(test_panel, LV_OBJ_FLAG_HIDDEN));
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
