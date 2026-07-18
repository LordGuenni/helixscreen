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


    actions.push_back({
        .id = "vivid_slots_check",
        .label = "Check All Slots",
        .icon = "check-square",
        .section = "maintenance",
        .description = "Check the filament path for all slots",
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

    actions.push_back({
        .id = "vivid_slots_walk",
        .label = "Walk All Slots",
        .icon = "footprints",
        .section = "maintenance",
        .description = "Sequentially check and walk through all slots",
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
    actions.push_back({
        .id = "vivid_rfid_detect",
        .label = "Detect RFID Tags",
        .icon = "nfc",
        .section = "setup",
        .description = "Detect presence of RFID tags",
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

    actions.push_back({
        .id = "vivid_cut",
        .label = "Cut Filament",
        .icon = "scissors",
        .section = "maintenance",
        .description = "Perform a filament cut",
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

    actions.push_back({
        .id = "vivid_autoload_toggle",
        .label = "Toggle Autoload",
        .icon = "rotate-cw",
        .section = "setup",
        .description = "Enable or disable the autoload feature",
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
