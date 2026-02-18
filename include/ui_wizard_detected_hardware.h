// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "subject_managed_panel.h"

/**
 * @file ui_wizard_detected_hardware.h
 * @brief Wizard step showing all auto-detected hardware add-ons
 *
 * Replaces the separate AMS Identify and Probe Sensor Select steps
 * with a single confirmational step that shows:
 * - Bed probe (type and name)
 * - Filament system (AMS/ERCF/AFC type and slot count)
 * - LEDs (detected LED chains)
 * - Accelerometer (ADXL345, LIS2DW, etc.)
 *
 * This is read-only — no configuration happens here. Just "we see your hardware".
 *
 * ## Skip Logic
 * - Skipped if no interesting hardware detected (no probe, no AMS, no LEDs, no accelerometer)
 */
class WizardDetectedHardwareStep {
  public:
    WizardDetectedHardwareStep();
    ~WizardDetectedHardwareStep();

    WizardDetectedHardwareStep(const WizardDetectedHardwareStep&) = delete;
    WizardDetectedHardwareStep& operator=(const WizardDetectedHardwareStep&) = delete;
    WizardDetectedHardwareStep(WizardDetectedHardwareStep&&) = delete;
    WizardDetectedHardwareStep& operator=(WizardDetectedHardwareStep&&) = delete;

    void init_subjects();
    void register_callbacks();
    [[nodiscard]] lv_obj_t* create(lv_obj_t* parent);
    void cleanup();
    [[nodiscard]] bool is_validated() const;
    [[nodiscard]] bool should_skip() const;

    [[nodiscard]] static const char* get_name() {
        return "Wizard Detected Hardware";
    }

  private:
    lv_obj_t* screen_root_{nullptr};

    SubjectManager subjects_;
    bool subjects_initialized_{false};

    // Probe section subjects
    lv_subject_t hw_has_probe_{};
    lv_subject_t hw_probe_name_{};
    static char probe_name_buf_[64];
    lv_subject_t hw_probe_type_{};
    static char probe_type_buf_[64];

    // AMS section subjects
    lv_subject_t hw_has_ams_{};
    lv_subject_t hw_ams_name_{};
    static char ams_name_buf_[64];
    lv_subject_t hw_ams_details_{};
    static char ams_details_buf_[64];

    // LED and accelerometer use global subjects (printer_has_led, printer_has_accelerometer)
    // from PrinterCapabilitiesState — no local subjects needed

    void update_display();
};

WizardDetectedHardwareStep* get_wizard_detected_hardware_step();
void destroy_wizard_detected_hardware_step();
