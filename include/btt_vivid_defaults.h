// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend.h"
#include <vector>

namespace helix::printer {

// Defines the collapsible sections in the settings overlay (e.g., "Setup", "Calibration")
std::vector<DeviceSection> btt_vivid_default_sections();

// Defines the individual actions (buttons, sliders, toggles) inside those sections
std::vector<DeviceAction> btt_vivid_default_actions();

} // namespace helix::printer
