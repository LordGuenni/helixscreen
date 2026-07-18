// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_backend_btt_vivid.h"
#include "moonraker_api.h"
#include "moonraker_client.h"
#include "printer_hardware.h"
#include "btt_vivid_defaults.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

using namespace helix;
using namespace helix::printer;

AmsBackendBttVivid::AmsBackendBttVivid(MoonrakerAPI* api, MoonrakerClient* client)
    : AmsSubscriptionBackend(api, client) {
    system_info_.type_name = "BTT Vivid";
}

AmsBackendBttVivid::~AmsBackendBttVivid() = default;

void AmsBackendBttVivid::on_started() {
    AmsSubscriptionBackend::on_started();
    nlohmann::json params;
    params["objects"] = {
        {"mms", nullptr},
        {"heater_generic ViViD_Dryer", nullptr},
        {"aht10 ViViD_Dryer_L", nullptr},
        {"aht10 ViViD_Dryer_R", nullptr}
    };
    
    client_->send_jsonrpc("printer.objects.query", params, [this, token = lifetime_.token()](nlohmann::json response) {
        if (response.contains("result") && response["result"].contains("status")) {
            token.defer("btt_vivid_init", [this, status = response["result"]["status"]]() mutable {
                nlohmann::json fake_notification;
                fake_notification["params"] = {status};
                handle_status_update(fake_notification);
            });
        }
    });
}

void AmsBackendBttVivid::handle_status_update(const nlohmann::json& notification) {
    if (!notification.contains("params") || !notification["params"].is_array() || notification["params"].empty()) {
        return;
    }
    const auto& params = notification["params"][0];
    if (params.contains("mms")) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            parse_mms_state(params["mms"]);
        }
        emit_event(EVENT_STATE_CHANGED);
    }
    
    // Parse temperatures and humidity from dedicated sensor objects
    bool env_updated = false;
    if (params.contains("heater_generic ViViD_Dryer")) {
        const auto& h = params["heater_generic ViViD_Dryer"];
        std::lock_guard<std::mutex> lock(mutex_);
        if (h.contains("temperature")) {
            dryer_info_.current_temp_c = h["temperature"].get<float>();
            dryer_info_.supported = true;
            env_updated = true;
        }
        if (h.contains("target")) {
            dryer_info_.target_temp_c = h["target"].get<float>();
            dryer_info_.active = (dryer_info_.target_temp_c > 0.0f);
            env_updated = true;
        }
    }
    if (params.contains("aht10 ViViD_Dryer_L")) {
        const auto& h = params["aht10 ViViD_Dryer_L"];
        std::lock_guard<std::mutex> lock(mutex_);
        if (h.contains("humidity")) {
            current_humidity_ = h["humidity"].get<float>();
            has_humidity_ = true;
            env_updated = true;
        }
        if (h.contains("temperature") && dryer_info_.current_temp_c <= 0.0f) {
            dryer_info_.current_temp_c = h["temperature"].get<float>();
            dryer_info_.supported = true;
            env_updated = true;
        }
    } else if (params.contains("aht10 ViViD_Dryer_R")) {
        const auto& h = params["aht10 ViViD_Dryer_R"];
        std::lock_guard<std::mutex> lock(mutex_);
        if (h.contains("humidity") && !has_humidity_) { // L takes precedence if both exist
            current_humidity_ = h["humidity"].get<float>();
            has_humidity_ = true;
            env_updated = true;
        }
        if (h.contains("temperature") && dryer_info_.current_temp_c <= 0.0f) {
            dryer_info_.current_temp_c = h["temperature"].get<float>();
            dryer_info_.supported = true;
            env_updated = true;
        }
    }
    
    if (env_updated) {
        emit_event(EVENT_STATE_CHANGED);
    }
}

void AmsBackendBttVivid::parse_mms_state(const nlohmann::json& mms_data) {
    if (mms_data.contains("slots") && mms_data["slots"].is_object()) {
        const auto& slots_json = mms_data["slots"];
        if (slots_.slot_count() == 0) {
            int max_slot = -1;
            for (const auto& [key, val] : slots_json.items()) {
                try { max_slot = std::max(max_slot, std::stoi(key)); } catch (...) {}
            }
            if (max_slot >= 0) {
                std::vector<std::string> slot_names;
                for (int i = 0; i <= max_slot; ++i) {
                    slot_names.push_back(std::to_string(i));
                }
                slots_.initialize("BTT Vivid", slot_names);
                system_info_.total_slots = slots_.slot_count();
                buffer_triggered_.resize(slots_.slot_count(), false);
                slot_segments_.resize(slots_.slot_count(), PathSegment::NONE);
            }
        }

        int found_selector = -1;

        for (const auto& [key, slot_data] : slots_json.items()) {
            int slot_idx = -1;
            try {
                slot_idx = std::stoi(key);
            } catch (...) { continue; }

            if (slot_idx >= 0 && slot_idx < slots_.slot_count()) {
                auto* entry = slots_.get_mut(slot_idx);
                if (entry && slot_data.is_object()) {
                    if (slot_data.contains("is_empty")) {
                        bool is_empty = slot_data["is_empty"].get<bool>();
                        entry->info.status = is_empty ? SlotStatus::EMPTY : SlotStatus::AVAILABLE;
                    }
                    if (slot_data.contains("selector") && slot_data["selector"].get<int>() == 1) {
                        found_selector = slot_idx;
                    } else if (slot_data.contains("elector") && slot_data["elector"].get<int>() == 1) {
                        found_selector = slot_idx;
                    }
                    
                    if (slot_data.contains("color") && slot_data["color"].is_string()) {
                        std::string hex = slot_data["color"].get<std::string>();
                        if (!hex.empty()) {
                            if (hex[0] != '#') hex = "#" + hex;
                            entry->info.color_rgb = std::strtoul(hex.c_str() + 1, nullptr, 16);
                        }
                    } else if (slot_data.contains("filament_color") && slot_data["filament_color"].is_string()) {
                        std::string hex = slot_data["filament_color"].get<std::string>();
                        if (!hex.empty()) {
                            if (hex[0] != '#') hex = "#" + hex;
                            entry->info.color_rgb = std::strtoul(hex.c_str() + 1, nullptr, 16);
                        }
                    }
                    if (slot_data.contains("material") && slot_data["material"].is_string()) {
                        entry->info.material = slot_data["material"].get<std::string>();
                    } else if (slot_data.contains("filament_material") && slot_data["filament_material"].is_string()) {
                        entry->info.material = slot_data["filament_material"].get<std::string>();
                    }
                    if (slot_data.contains("vendor") && slot_data["vendor"].is_string()) {
                        entry->info.brand = slot_data["vendor"].get<std::string>();
                    }
                    if (slot_data.contains("spool_id") && slot_data["spool_id"].is_number()) {
                        entry->info.spoolman_id = slot_data["spool_id"].get<int>();
                    }

                    // Parse path segments
                    if (slot_idx < static_cast<int>(slot_segments_.size())) {
                        bool is_loaded = false;
                        if (mms_data.contains("loading_slots") && mms_data["loading_slots"].is_array()) {
                            for (const auto& ls : mms_data["loading_slots"]) {
                                if (ls.is_number() && ls.get<int>() == slot_idx) {
                                    is_loaded = true;
                                    break;
                                }
                            }
                        }

                        // Get previous values if not present in this update
                        bool has_inlet = slot_data.contains("inlet") ? (slot_data["inlet"].get<int>() == 1) : 
                                         (slot_segments_[slot_idx] != PathSegment::NONE);
                        bool has_gate = slot_data.contains("gate") ? (slot_data["gate"].get<int>() == 1) : 
                                        (static_cast<int>(slot_segments_[slot_idx]) >= static_cast<int>(PathSegment::PREP));
                        bool has_runout = slot_data.contains("runout") ? (slot_data["runout"].get<int>() == 1) :
                                        (static_cast<int>(slot_segments_[slot_idx]) >= static_cast<int>(PathSegment::HUB));
                        bool has_outlet = slot_data.contains("outlet") ? (slot_data["outlet"].get<int>() == 1) :
                                        (static_cast<int>(slot_segments_[slot_idx]) >= static_cast<int>(PathSegment::OUTPUT));
                        bool has_entry = slot_data.contains("entry") ? (slot_data["entry"].get<int>() == 1) :
                                        (static_cast<int>(slot_segments_[slot_idx]) >= static_cast<int>(PathSegment::TOOLHEAD));

                        if (is_loaded || has_entry) {
                            slot_segments_[slot_idx] = PathSegment::TOOLHEAD;
                        } else if (has_outlet) {
                            slot_segments_[slot_idx] = PathSegment::OUTPUT;
                        } else if (has_runout) {
                            slot_segments_[slot_idx] = PathSegment::HUB;
                        } else if (has_gate) {
                            slot_segments_[slot_idx] = PathSegment::PREP; // Buffer entry
                        } else if (has_inlet) {
                            slot_segments_[slot_idx] = PathSegment::SPOOL; // Spool inserted
                        } else {
                            slot_segments_[slot_idx] = PathSegment::NONE;
                        }
                    }

                    if (slot_data.contains("filament_info") && slot_data["filament_info"].is_object()) {
                        const auto& finfo = slot_data["filament_info"];
                        if (finfo.contains("filament_manufacturer") && finfo["filament_manufacturer"].is_string()) {
                            entry->info.brand = finfo["filament_manufacturer"].get<std::string>();
                        }
                        if (finfo.contains("filament_type_detailed") && finfo["filament_type_detailed"].is_string()) {
                            entry->info.color_name = finfo["filament_type_detailed"].get<std::string>();
                        }
                        if (finfo.contains("bed_temperature") && finfo["bed_temperature"].is_number()) {
                            entry->info.bed_temp = finfo["bed_temperature"].get<int>();
                        }
                        if (finfo.contains("nozzle_temp") && finfo["nozzle_temp"].is_number()) {
                            // MMS provides a single nozzle_temp, so set min/max to it
                            entry->info.nozzle_temp_min = finfo["nozzle_temp"].get<int>();
                            entry->info.nozzle_temp_max = entry->info.nozzle_temp_min;
                        }
                        if (finfo.contains("spool_id") && finfo["spool_id"].is_number()) {
                            entry->info.spoolman_id = finfo["spool_id"].get<int>();
                        }
                        if (finfo.contains("nominal_netto_full_weight") && finfo["nominal_netto_full_weight"].is_number()) {
                            entry->info.total_weight_g = finfo["nominal_netto_full_weight"].get<float>();
                        }
                        if (finfo.contains("remaining_weight") && finfo["remaining_weight"].is_number()) {
                            entry->info.remaining_weight_g = finfo["remaining_weight"].get<float>();
                        }
                    }
                    if (slot_data.contains("remaining_weight") && slot_data["remaining_weight"].is_number()) {
                        entry->info.remaining_weight_g = slot_data["remaining_weight"].get<float>();
                    }
                    if (slot_data.contains("total_weight") && slot_data["total_weight"].is_number()) {
                        entry->info.total_weight_g = slot_data["total_weight"].get<float>();
                    }
                }
            }
        }
    }
    
    if (mms_data.contains("buffers") && mms_data["buffers"].is_object()) {
        if (buffer_pcts_.size() < buffer_triggered_.size()) {
            buffer_pcts_.resize(buffer_triggered_.size(), -1.0f);
        }
        for (const auto& [key, buf_data] : mms_data["buffers"].items()) {
            int buf_idx = -1;
            try {
                buf_idx = std::stoi(key);
            } catch (...) { continue; }
            
            if (buf_idx >= 0 && buf_idx < static_cast<int>(buffer_triggered_.size())) {
                if (buf_data.is_object() && buf_data.contains("pct")) {
                    float pct = buf_data["pct"].get<float>();
                    buffer_triggered_[buf_idx] = (pct > 0.0f);
                    buffer_pcts_[buf_idx] = pct;
                }
            }
        }
    }

    bool is_loaded = false;
    int loaded_slot = -1;
    if (mms_data.contains("loading_slots") && mms_data["loading_slots"].is_array()) {
        auto loading = mms_data["loading_slots"];
        if (!loading.empty()) {
            if (loading[0].is_number_integer()) {
                loaded_slot = loading[0].get<int>();
                is_loaded = true;
            } else if (loading[0].is_string()) {
                try { loaded_slot = std::stoi(loading[0].get<std::string>()); is_loaded = true; } catch (...) {}
            }
        }
    }
    
    // Set current slot based on loaded filament, or selector position, or fallback to 0
    if (is_loaded) {
        system_info_.current_slot = loaded_slot;
    } else if (found_selector >= 0) {
        system_info_.current_slot = found_selector;
    } else if (system_info_.current_slot < 0) {
        system_info_.current_slot = 0;
    }
    
    // BTT Vivid does not have a separate filament_loaded field, so infer it from loading_slots
    system_info_.filament_loaded = is_loaded;
    
    // Update slot statuses with buffers and current slot
    for (int i = 0; i < slots_.slot_count(); ++i) {
        auto* entry = slots_.get_mut(i);
        if (entry && entry->info.status != SlotStatus::EMPTY) {
            if (system_info_.current_slot == i) {
                entry->info.status = SlotStatus::LOADED;
            } else if (i < static_cast<int>(buffer_triggered_.size()) && buffer_triggered_[i]) {
                entry->info.status = SlotStatus::FROM_BUFFER;
            } else {
                entry->info.status = SlotStatus::AVAILABLE;
            }
        }
    }
}

AmsSystemInfo AmsBackendBttVivid::get_system_info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AmsSystemInfo info = slots_.build_system_info();
    info.type = AmsType::BTT_VIVID;
    info.type_name = "BTT Vivid";
    info.current_slot = system_info_.current_slot;
    info.filament_loaded = system_info_.filament_loaded;
    
    if (!info.units.empty()) {
        if (dryer_info_.supported || has_humidity_) {
            EnvironmentData env;
            env.temperature_c = dryer_info_.current_temp_c;
            env.humidity_pct = current_humidity_;
            env.has_humidity = has_humidity_;
            info.units[0].environment = env;
        }

        // Copy over buffer pcts that were populated in parse_mms_state
        if (!buffer_pcts_.empty()) {
            info.units[0].buffer_pcts = buffer_pcts_;
        }
    }
    
    return info;
}

AmsType AmsBackendBttVivid::get_type() const {
    return AmsType::BTT_VIVID;
}

bool AmsBackendBttVivid::has_environment_sensors() const {
    return true;
}

SlotInfo AmsBackendBttVivid::get_slot_info(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto* entry = slots_.get(slot_index)) {
        return entry->info;
    }
    return {};
}

PathTopology AmsBackendBttVivid::get_topology() const {
    // BTT Vivid (MMS) uses individual buffers for each lane merging into a hub
    return PathTopology::HUB;
}

PathSegment AmsBackendBttVivid::get_filament_segment() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (system_info_.current_slot >= 0 && system_info_.current_slot < static_cast<int>(slot_segments_.size())) {
        return slot_segments_[system_info_.current_slot];
    }
    return PathSegment::NONE;
}

PathSegment AmsBackendBttVivid::get_slot_filament_segment(int slot_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slot_index >= 0 && slot_index < static_cast<int>(slot_segments_.size())) {
        return slot_segments_[slot_index];
    }
    return PathSegment::NONE;
}

PathSegment AmsBackendBttVivid::infer_error_segment() const {
    return PathSegment::NONE;
}

AmsError AmsBackendBttVivid::validate_slot_index(int slot_index) const {
    if (slot_index < 0 || slot_index >= slots_.slot_count()) {
        return AmsErrorHelper::invalid_slot(slot_index, slots_.slot_count() - 1);
    }
    return AmsErrorHelper::success();
}

AmsError AmsBackendBttVivid::load_filament(int slot_index) {
    if (auto err = validate_slot_index(slot_index); err.result != AmsResult::SUCCESS) return err;
    if (system_info_.filament_loaded && system_info_.current_slot != slot_index && system_info_.current_slot >= 0) {
        return execute_gcode(fmt::format("MMS_EJECT\nMMS_CHARGE SLOT={}", slot_index));
    }
    return execute_gcode(fmt::format("MMS_CHARGE SLOT={}", slot_index));
}

AmsError AmsBackendBttVivid::unload_filament(int /*slot_index*/) {
    return execute_gcode("MMS_EJECT");
}

AmsError AmsBackendBttVivid::pop_filament(int slot_index) {
    if (auto err = validate_slot_index(slot_index); err.result != AmsResult::SUCCESS) return err;
    return execute_gcode(fmt::format("MMS_POP SLOT={}", slot_index));
}

AmsError AmsBackendBttVivid::select_slot(int slot_index) {
    if (auto err = validate_slot_index(slot_index); err.result != AmsResult::SUCCESS) return err;
    return execute_gcode(fmt::format("MMS_SELECT SLOT={}", slot_index));
}

AmsError AmsBackendBttVivid::select_gate(int slot_index) {
    return select_slot(slot_index);
}

AmsError AmsBackendBttVivid::move_selector(int delta) {
    if (!running_) {
        return AmsErrorHelper::not_connected("BTT Vivid backend not started");
    }

    const int count = system_info_.total_slots;
    if (count <= 0) {
        return AmsErrorHelper::not_supported("Selector jog");
    }

    int base = system_info_.current_slot;
    if (base < 0) {
        base = 0;
    }

    int target = (base + delta) % count;
    if (target < 0) {
        target += count;
    }

    return select_slot(target);
}

AmsError AmsBackendBttVivid::change_tool(int tool_number) {
    return execute_gcode(fmt::format("T{}", tool_number));
}

AmsError AmsBackendBttVivid::recover() {
    return execute_gcode("MMS_RESUME");
}

AmsError AmsBackendBttVivid::reset() {
    return execute_gcode("MMS_STOP");
}

AmsError AmsBackendBttVivid::cancel() {
    return execute_gcode("MMS_STOP");
}

AmsError AmsBackendBttVivid::set_slot_info(int slot_index, const SlotInfo& info, bool persist) {
    if (auto err = validate_slot_index(slot_index); err.result != AmsResult::SUCCESS) return err;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto* entry = slots_.get_mut(slot_index)) {
            entry->info = info;
        }
    }

    if (!persist) {
        return AmsErrorHelper::success();
    }

    std::string cmds;
    std::string hex = fmt::format("{:06x}", info.color_rgb);
    cmds += fmt::format("MMS_SLOT_COLOR SLOT={} COLOR='{}'\n", slot_index, hex);
    
    if (!info.material.empty()) {
        cmds += fmt::format("MMS_SLOT_MATERIAL SLOT={} MATERIAL='{}'\n", slot_index, info.material);
    }
    
    // For other meta attributes, use MMS_SLOT_META
    if (!info.brand.empty()) {
        cmds += fmt::format("MMS_SLOT_META SLOT={} KEY='vendor' VALUE='{}'\n", slot_index, info.brand);
    }
    if (!info.color_name.empty()) {
        cmds += fmt::format("MMS_SLOT_META SLOT={} KEY='name' VALUE='{}'\n", slot_index, info.color_name);
    }
    if (info.bed_temp > 0) {
        cmds += fmt::format("MMS_SLOT_META SLOT={} KEY='bed_temp' VALUE='{}'\n", slot_index, info.bed_temp);
    }
    if (info.nozzle_temp_min > 0) {
        cmds += fmt::format("MMS_SLOT_META SLOT={} KEY='nozzle_temp' VALUE='{}'\n", slot_index, info.nozzle_temp_min);
    }
    
    if (info.spoolman_id > 0) {
        cmds += fmt::format("MMS_SLOT_SPOOL SLOT={} SPOOL_ID={}\n", slot_index, info.spoolman_id);
    } else {
        cmds += fmt::format("MMS_SLOT_SPOOL SLOT={} SPOOL_ID=-1\n", slot_index);
    }

    execute_gcode(cmds);

    return AmsErrorHelper::success();
}

AmsError AmsBackendBttVivid::set_tool_mapping(int /*tool_number*/, int /*slot_index*/) {
    return AmsErrorHelper::not_supported("set_tool_mapping");
}

std::vector<int> AmsBackendBttVivid::get_tool_mapping() const {
    return {};
}

DryerInfo AmsBackendBttVivid::get_dryer_info(int /*unit*/) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dryer_info_;
}

AmsError AmsBackendBttVivid::start_drying(float temp_c, int /*duration_min*/, int /*fan_pct*/, int /*unit*/) {
    return execute_gcode(fmt::format("MMS_DRYER_START TEMP={}", temp_c));
}

AmsError AmsBackendBttVivid::stop_drying(int /*unit*/) {
    return execute_gcode("MMS_DRYER_STOP");
}

std::vector<DeviceSection> AmsBackendBttVivid::get_device_sections() const {
    return helix::printer::btt_vivid_default_sections();
}

std::vector<DeviceAction> AmsBackendBttVivid::get_device_actions() const {
    return helix::printer::btt_vivid_default_actions();
}

AmsError AmsBackendBttVivid::execute_device_action(const std::string& action_id, const std::any& /*value*/) {
    if (action_id == "vivid_calibration_wizard") {
        return execute_gcode("MMS_BOWDEN_CALIBRATION");
    } else if (action_id == "vivid_slots_check") {
        return execute_gcode("MMS_SLOTS_CHECK");
    } else if (action_id == "vivid_slots_walk") {
        return execute_gcode("MMS_SLOTS_WALK");
    } else if (action_id == "vivid_rfid_detect") {
        return execute_gcode("MMS_RFID_DETECT");
    } else if (action_id == "vivid_cut") {
        return execute_gcode("MMS_CUT");
    } else if (action_id == "vivid_autoload_toggle") {
        // Toggle autoload is currently sent as enable, consider full toggle if state is available
        return execute_gcode("MMS_AUTOLOAD_ENABLE");
    }
    
    return AmsErrorHelper::not_supported(fmt::format("Action {} not implemented", action_id));
}
