// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/// Flags indicating which wizard steps are skipped
struct WizardSkipFlags {
    bool touch_cal = false;
    bool language = false;
    bool wifi = false;
    bool ams = false;
    bool led = false;
    bool filament = false;
    bool input_shaper = false;
};

/// Calculate display step number from internal step, accounting for skips.
/// Returns the 1-based display step number.
int wizard_calculate_display_step(int internal_step, const WizardSkipFlags& skips);

/// Calculate total display steps, accounting for skips.
int wizard_calculate_display_total(const WizardSkipFlags& skips);

/// Find the next non-skipped step going forward from current.
/// Returns the next valid internal step, or -1 if at end.
int wizard_next_step(int current, const WizardSkipFlags& skips);

/// Find the previous non-skipped step going backward from current.
/// Returns the previous valid internal step, or -1 if at beginning.
int wizard_prev_step(int current, const WizardSkipFlags& skips);

} // namespace helix
