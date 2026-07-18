// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_subscription_backend.h"
#include "async_lifetime_guard.h"
#include "error_event.h"
#include "slot_registry.h"

#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @file ams_backend_btt_vivid.h
 * @brief BTT Vivid (MMS) MMU backend implementation
 */
class AmsBackendBttVivid : public AmsSubscriptionBackend {
  public:
    AmsBackendBttVivid(MoonrakerAPI* api, helix::MoonrakerClient* client);
    ~AmsBackendBttVivid() override;

    [[nodiscard]] AmsSystemInfo get_system_info() const override;
    [[nodiscard]] AmsType get_type() const override;
    [[nodiscard]] const char* get_klipper_object_name() const override {
        return "mms"; // Matches the Klipper object name
    }

    [[nodiscard]] bool manages_active_spool() const override { return true; }
    [[nodiscard]] SlotInfo get_slot_info(int slot_index) const override;

    [[nodiscard]] PathTopology get_topology() const override;
    [[nodiscard]] PathSegment get_filament_segment() const override;
    [[nodiscard]] PathSegment get_slot_filament_segment(int slot_index) const override;
    [[nodiscard]] PathSegment infer_error_segment() const override;

    // Operations
    AmsError load_filament(int slot_index) override;
    AmsError unload_filament(int slot_index = -1) override;
    AmsError select_slot(int slot_index) override;
    AmsError change_tool(int tool_number) override;
    
    [[nodiscard]] bool supports_gate_select() const override { return true; }
    
    [[nodiscard]] bool supports_sync_feedback_visualization(const AmsSystemInfo&) const override {
        return true;
    }
    
    AmsError select_gate(int slot_index) override;
    AmsError move_selector(int delta) override;
    [[nodiscard]] bool supports_auto_heat_on_load() const override { return true; }
    [[nodiscard]] bool slot_has_prep_sensor(int /*slot_index*/) const override { return true; }
    [[nodiscard]] bool needs_unload_before_load(const AmsSystemInfo& /*info*/) const override { return false; }

    // Recovery
    AmsError recover() override;
    AmsError reset() override;
    AmsError reset_lane(int /*slot_index*/) override { return AmsErrorHelper::not_supported("Per-lane reset"); }
    AmsError eject_lane(int /*slot_index*/) override { return AmsErrorHelper::not_supported("Lane eject"); }

    AmsError cancel() override;

    // Configuration
    AmsError set_slot_info(int slot_index, const SlotInfo& info, bool persist = true) override;
    AmsError set_tool_mapping(int tool_number, int slot_index) override;

    [[nodiscard]] bool is_bypass_active() const override { return false; }
    AmsError enable_bypass() override { return AmsErrorHelper::not_supported("Bypass mode"); }
    AmsError disable_bypass() override { return AmsErrorHelper::not_supported("Bypass mode"); }
    bool has_environment_sensors() const override;

    // Configuration / UI Actions
    [[nodiscard]] std::vector<helix::printer::DeviceSection> get_device_sections() const override;
    [[nodiscard]] std::vector<helix::printer::DeviceAction> get_device_actions() const override;
    AmsError execute_device_action(const std::string& action_id, const std::any& value = {}) override;

    [[nodiscard]] RemapStrategy get_remap_strategy() const override {
        return RemapStrategy::Native;
    }

    [[nodiscard]] std::vector<int> get_tool_mapping() const override;
    
    // Dryer support
    [[nodiscard]] DryerInfo get_dryer_info(int unit = 0) const override;
    AmsError start_drying(float temp_c, int duration_min, int fan_pct = -1, int unit = 0) override;
    AmsError stop_drying(int unit = 0) override;

  protected:
    void on_started() override;
    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override { return "[AMS BTTVivid]"; }

  private:
    void parse_mms_state(const nlohmann::json& mms_data);
    AmsError validate_slot_index(int slot_index) const;

    helix::AsyncLifetimeGuard lifetime_;
    helix::printer::SlotRegistry slots_;
    std::string reason_for_pause_;

    DryerInfo dryer_info_;
    float current_humidity_ = 0.0f;
    bool has_humidity_ = false;
    std::vector<bool> buffer_triggered_;
    std::vector<PathSegment> slot_segments_;
    std::vector<float> buffer_pcts_;
    bool is_activating_ = false;
};
