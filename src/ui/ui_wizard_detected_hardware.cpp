// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_detected_hardware.h"

#include "ams_state.h"
#include "ams_types.h"
#include "probe_sensor_manager.h"
#include "probe_sensor_types.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

using helix::sensors::probe_type_to_display_string;
using helix::sensors::ProbeSensorManager;

// Static buffer definitions
char WizardDetectedHardwareStep::probe_name_buf_[64] = {};
char WizardDetectedHardwareStep::probe_type_buf_[64] = {};
char WizardDetectedHardwareStep::ams_name_buf_[64] = {};
char WizardDetectedHardwareStep::ams_details_buf_[64] = {};
// LED and accelerometer use global subjects from PrinterCapabilitiesState

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardDetectedHardwareStep> g_wizard_detected_hardware_step;

WizardDetectedHardwareStep* get_wizard_detected_hardware_step() {
    if (!g_wizard_detected_hardware_step) {
        g_wizard_detected_hardware_step = std::make_unique<WizardDetectedHardwareStep>();
        StaticPanelRegistry::instance().register_destroy(
            "WizardDetectedHardwareStep", []() { g_wizard_detected_hardware_step.reset(); });
    }
    return g_wizard_detected_hardware_step.get();
}

void destroy_wizard_detected_hardware_step() {
    g_wizard_detected_hardware_step.reset();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardDetectedHardwareStep::WizardDetectedHardwareStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardDetectedHardwareStep::~WizardDetectedHardwareStep() {
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }
    screen_root_ = nullptr;
}

// ============================================================================
// Subject Initialization
// ============================================================================

void WizardDetectedHardwareStep::init_subjects() {
    spdlog::debug("[{}] Initializing subjects", get_name());

    // Probe section
    UI_MANAGED_SUBJECT_INT(hw_has_probe_, 0, "hw_has_probe", subjects_);
    UI_MANAGED_SUBJECT_STRING(hw_probe_name_, probe_name_buf_, "", "hw_probe_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(hw_probe_type_, probe_type_buf_, "", "hw_probe_type", subjects_);

    // AMS section
    UI_MANAGED_SUBJECT_INT(hw_has_ams_, 0, "hw_has_ams", subjects_);
    UI_MANAGED_SUBJECT_STRING(hw_ams_name_, ams_name_buf_, "", "hw_ams_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(hw_ams_details_, ams_details_buf_, "", "hw_ams_details", subjects_);

    // LED and accelerometer sections use global subjects from PrinterCapabilitiesState
    // (printer_has_led, printer_has_accelerometer) — no local subjects needed

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callbacks (no-op — display only)
// ============================================================================

void WizardDetectedHardwareStep::register_callbacks() {
    spdlog::debug("[{}] Register callbacks (no-op)", get_name());
}

// ============================================================================
// Screen Creation
// ============================================================================

lv_obj_t* WizardDetectedHardwareStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating detected hardware screen", get_name());

    if (screen_root_) {
        spdlog::warn("[{}] Screen pointer not null - resetting", get_name());
        screen_root_ = nullptr;
    }

    screen_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_detected_hardware", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        return nullptr;
    }

    update_display();

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

// ============================================================================
// Display Update
// ============================================================================

void WizardDetectedHardwareStep::update_display() {
    if (!screen_root_) {
        return;
    }

    // --- Probe ---
    auto& probe_mgr = ProbeSensorManager::instance();
    auto sensors = probe_mgr.get_sensors();
    if (!sensors.empty()) {
        lv_subject_set_int(&hw_has_probe_, 1);

        std::string name = probe_type_to_display_string(sensors[0].type);
        snprintf(probe_name_buf_, sizeof(probe_name_buf_), "%s", name.c_str());
        lv_subject_copy_string(&hw_probe_name_, probe_name_buf_);

        const char* type_desc = "Standard Probe";
        switch (sensors[0].type) {
        case helix::sensors::ProbeSensorType::CARTOGRAPHER:
            type_desc = "Eddy Current Scanning Probe";
            break;
        case helix::sensors::ProbeSensorType::BEACON:
            type_desc = "Eddy Current Probe";
            break;
        case helix::sensors::ProbeSensorType::BLTOUCH:
            type_desc = "Servo-Actuated Touch Probe";
            break;
        case helix::sensors::ProbeSensorType::TAP:
            type_desc = "Nozzle Contact Probe";
            break;
        case helix::sensors::ProbeSensorType::KLICKY:
            type_desc = "Magnetic Dock Probe";
            break;
        case helix::sensors::ProbeSensorType::EDDY_CURRENT:
            type_desc = "Eddy Current Probe";
            break;
        case helix::sensors::ProbeSensorType::SMART_EFFECTOR:
            type_desc = "Piezo Contact Probe";
            break;
        default:
            break;
        }
        snprintf(probe_type_buf_, sizeof(probe_type_buf_), "%s", type_desc);
        lv_subject_copy_string(&hw_probe_type_, probe_type_buf_);

        spdlog::debug("[{}] Probe: {} ({})", get_name(), name, type_desc);
    } else {
        lv_subject_set_int(&hw_has_probe_, 0);
    }

    // --- AMS ---
    auto& ams = AmsState::instance();
    AmsBackend* backend = ams.get_backend();
    if (backend && backend->get_type() != AmsType::NONE) {
        lv_subject_set_int(&hw_has_ams_, 1);

        const char* ams_name = "Unknown";
        switch (backend->get_type()) {
        case AmsType::AFC:
            ams_name = "AFC (Armored Turtle)";
            break;
        case AmsType::HAPPY_HARE:
            ams_name = "Happy Hare MMU";
            break;
        case AmsType::VALGACE:
            ams_name = "ValgACE (ACE Pro)";
            break;
        case AmsType::TOOL_CHANGER:
            ams_name = "Tool Changer";
            break;
        default:
            break;
        }
        snprintf(ams_name_buf_, sizeof(ams_name_buf_), "%s", ams_name);
        lv_subject_copy_string(&hw_ams_name_, ams_name_buf_);

        AmsSystemInfo info = backend->get_system_info();
        if (info.total_slots > 0) {
            snprintf(ams_details_buf_, sizeof(ams_details_buf_), "%d slots", info.total_slots);
        } else {
            snprintf(ams_details_buf_, sizeof(ams_details_buf_), "System detected");
        }
        lv_subject_copy_string(&hw_ams_details_, ams_details_buf_);

        spdlog::debug("[{}] AMS: {} ({})", get_name(), ams_name, ams_details_buf_);
    } else {
        lv_subject_set_int(&hw_has_ams_, 0);
    }

    // --- LEDs and Accelerometer ---
    // These are detected via PrinterCapabilitiesState subjects which are already
    // set by the time we reach this wizard step. Use the subject values directly.
    // For the wizard step, we just show/hide sections based on detection.
    // The actual LED/accel names would require querying the object list, so for
    // now we show a generic "Detected" message.
    // LED and accelerometer detection is handled by subject bindings in XML
    // (printer_has_led and printer_has_accelerometer subjects from PrinterCapabilitiesState)

    spdlog::debug("[{}] Display updated", get_name());
}

// ============================================================================
// Cleanup
// ============================================================================

void WizardDetectedHardwareStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());
    screen_root_ = nullptr;
    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Validation & Skip
// ============================================================================

bool WizardDetectedHardwareStep::is_validated() const {
    return true; // Display-only step
}

bool WizardDetectedHardwareStep::should_skip() const {
    // Skip if no interesting hardware detected
    auto& probe_mgr = ProbeSensorManager::instance();
    bool has_probe = probe_mgr.has_sensors();

    auto& ams = AmsState::instance();
    AmsBackend* backend = ams.get_backend();
    bool has_ams = backend && backend->get_type() != AmsType::NONE;

    // If neither probe nor AMS, skip (LEDs and accel have their own steps)
    bool skip = !has_probe && !has_ams;

    if (skip) {
        spdlog::info("[{}] No probe or AMS detected, skipping step", get_name());
    } else {
        spdlog::debug("[{}] Hardware detected (probe={}, ams={}), showing step", get_name(),
                      has_probe, has_ams);
    }

    return skip;
}
