// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_btt_vivid.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_hardware.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace helix;
using namespace helix::printer;

AmsBackendBttVivid::AmsBackendBttVivid(MoonrakerAPI* api, MoonrakerClient* client)
    : AmsSubscriptionBackend(api, client), lifetime_(api ? api->get_main_thread_runner() : nullptr) {
    system_info_.type_name = "BTT Vivid";
    system_info_.topology = PathTopology::LINEAR;
}

AmsBackendBttVivid::~AmsBackendBttVivid() = default;

void AmsBackendBttVivid::on_started() {
    AmsSubscriptionBackend::on_started();

    // Force an initial fetch of printer.mms
    fetch_initial_state("mms", [this, guard = lifetime_.lock()](const nlohmann::json& result) {
        if (!guard) return;
        if (result.contains("status") && result["status"].contains("mms")) {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            parse_mms_state(result["status"]["mms"]);
            notify_update();
        }
    });
}

void AmsBackendBttVivid::handle_status_update(const nlohmann::json& notification) {
    if (!notification.contains("params") || !notification["params"].is_array() || notification["params"].empty()) {
        return;
    }
    const auto& params = notification["params"][0];
    if (params.contains("mms")) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        parse_mms_state(params["mms"]);
        notify_update();
    }
}

void AmsBackendBttVivid::parse_mms_state(const nlohmann::json& mms_data) {
    if (mms_data.contains("slots") && mms_data["slots"].is_object()) {
        const auto& slots_json = mms_data["slots"];
        if (slots_.slot_count() != slots_json.size()) {
            slots_.initialize(slots_json.size(), 1, {static_cast<int>(slots_json.size())});
        }
        
        system_info_.units.clear();
        AmsUnitInfo unit;
        unit.global_index = 0;
        unit.first_slot_global_index = 0;
        unit.slot_count = slots_json.size();
        unit.name = "BTT Vivid";
        unit.topology = PathTopology::LINEAR;
        system_info_.units.push_back(unit);

        for (const auto& [key, slot_data] : slots_json.items()) {
            int slot_idx = -1;
            try {
                slot_idx = std::stoi(key);
            } catch (...) { continue; }

            if (slot_idx >= 0 && slot_idx < slots_.slot_count()) {
                auto* entry = slots_.get_mut(slot_idx);
                if (entry && slot_data.is_object()) {
                    bool is_empty = slot_data.value("is_empty", true);
                    entry->info.has_filament = !is_empty;
                    
                    if (slot_data.contains("filament_color") && slot_data["filament_color"].is_string()) {
                        std::string hex = slot_data["filament_color"].get<std::string>();
                        if (!hex.empty()) {
                            if (hex[0] != '#') hex = "#" + hex;
                            entry->info.color_hex = hex;
                        }
                    }
                    if (slot_data.contains("filament_material") && slot_data["filament_material"].is_string()) {
                        entry->info.material = slot_data["filament_material"].get<std::string>();
                    }
                }
            }
        }
    }

    if (mms_data.contains("loading_slots") && mms_data["loading_slots"].is_array()) {
        auto loading = mms_data["loading_slots"];
        if (!loading.empty()) {
            system_info_.current_slot = loading[0].get<int>();
        } else {
            system_info_.current_slot = -1;
        }
    }
}

AmsSystemInfo AmsBackendBttVivid::get_system_info() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return system_info_;
}

AmsType AmsBackendBttVivid::get_type() const {
    return AmsType::BTT_VIVID;
}

SlotInfo AmsBackendBttVivid::get_slot_info(int slot_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (auto* entry = slots_.get(slot_index)) {
        return entry->info;
    }
    return {};
}

PathTopology AmsBackendBttVivid::get_topology() const {
    return PathTopology::LINEAR;
}

PathSegment AmsBackendBttVivid::get_filament_segment() const {
    return PathSegment::NONE;
}

PathSegment AmsBackendBttVivid::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (auto* entry = slots_.get(slot_index)) {
        return entry->info.has_filament ? PathSegment::SPOOL : PathSegment::NONE;
    }
    return PathSegment::NONE;
}

PathSegment AmsBackendBttVivid::infer_error_segment() const {
    return PathSegment::NONE;
}

AmsError AmsBackendBttVivid::validate_slot_index(int slot_index) const {
    if (slot_index < 0 || slot_index >= slots_.slot_count()) {
        return AmsError{AmsError::INVALID_GATE};
    }
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::load_filament(int slot_index) {
    if (auto err = validate_slot_index(slot_index); err.code != AmsError::SUCCESS) return err;
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode(fmt::format("MMS_LOAD SLOT={}", slot_index));
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::unload_filament(int slot_index) {
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    if (slot_index >= 0) {
        api_->send_gcode(fmt::format("MMS_UNLOAD SLOT={}", slot_index));
    } else {
        api_->send_gcode("MMS_UNLOAD");
    }
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::select_slot(int slot_index) {
    if (auto err = validate_slot_index(slot_index); err.code != AmsError::SUCCESS) return err;
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode(fmt::format("MMS_SELECT SLOT={}", slot_index));
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::change_tool(int tool_number) {
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode(fmt::format("T{}", tool_number));
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::recover() {
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode("MMS_RESUME");
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::reset() {
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode("MMS_STOP");
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::cancel() {
    if (!api_) return AmsError{AmsError::API_DISCONNECTED};
    api_->send_gcode("MMS_STOP");
    return AmsError{AmsError::SUCCESS};
}

AmsError AmsBackendBttVivid::set_slot_info(int slot_index, const SlotInfo& info, bool persist) {
    return AmsError{AmsError::NOT_SUPPORTED};
}

AmsError AmsBackendBttVivid::set_tool_mapping(int tool_number, int slot_index) {
    return AmsError{AmsError::NOT_SUPPORTED};
}

std::vector<int> AmsBackendBttVivid::get_tool_mapping() const {
    return {};
}

DryerInfo AmsBackendBttVivid::get_dryer_info(int unit) const {
    return {};
}

AmsError AmsBackendBttVivid::start_drying(float temp_c, int duration_min, int fan_pct, int unit) {
    return AmsError{AmsError::NOT_SUPPORTED};
}

AmsError AmsBackendBttVivid::stop_drying(int unit) {
    return AmsError{AmsError::NOT_SUPPORTED};
}
