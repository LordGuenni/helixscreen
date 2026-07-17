// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "btt_vivid_defaults.h"

namespace helix::printer {

std::vector<DeviceSection> btt_vivid_default_sections() {
    return {
        {"setup", "Setup", 0, "BTT Vivid configuration and calibration"},
        {"maintenance", "Maintenance", 1, "Lane tests and maintenance"},
    };
}

std::vector<DeviceAction> btt_vivid_default_actions() {
    std::vector<DeviceAction> actions;

    actions.push_back({
        .id = "vivid_calibration_wizard",
        .label = "Run Calibration Wizard",
        .icon = "play",
        .section = "setup",
        .description = "Interactive calibration for Vivid lanes",
        .type = ActionType::BUTTON,
        .current_value = {},
        .options = {},
        .min_value = 0,
        .max_value = 0,
        .unit = "",
        .slot_index = -1,
        .enabled = true,
        .disable_reason = "",
    });

    // Example macro mapping
    actions.push_back({
        .id = "vivid_test_lanes",
        .label = "Test All Lanes",
        .icon = "test-tube",
        .section = "maintenance",
        .description = "Run test sequence on all lanes",
        .type = ActionType::BUTTON,
        .current_value = {},
        .options = {},
        .min_value = 0,
        .max_value = 0,
        .unit = "",
        .slot_index = -1,
        .enabled = true,
        .disable_reason = "",
    });

    return actions;
}

} // namespace helix::printer
