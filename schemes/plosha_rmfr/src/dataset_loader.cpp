#include "dataset_loader.hpp"
#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <iostream>

namespace plosha {

uint32_t DatasetLoader::quantize(double value, double min_val, double max_val) {
    if (max_val <= min_val) return 0;
    double norm = (value - min_val) / (max_val - min_val);
    norm = std::max(0.0, std::min(1.0, norm));
    return static_cast<uint32_t>(norm * MAX_SENSOR_VALUE);
}

void DatasetLoader::load(const std::string& csv_path,
                          int num_sensors,
                          int num_fog_nodes) {
    readings_.clear();
    sensor_to_fog_.clear();

    // Build sensor-to-fog assignment (round-robin)
    for (int s = 0; s < num_sensors; ++s) {
        sensor_to_fog_[s] = s % num_fog_nodes;
    }

    // Parse CSV
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open dataset: " + csv_path);
    }

    std::string line;
    std::getline(file, line); // Skip header

    struct RawRow {
        int sensor_id;
        int fog_id;
        double temperature;
        double pressure;
        double vibration;
        bool is_failure;
    };
    std::vector<RawRow> raw_rows;

    while (std::getline(file, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(ss, token, ',')) {
            tokens.push_back(token);
        }
        if (tokens.size() < 7) continue;

        RawRow row;
        // Parse Sensor_ID: "S0" -> 0
        std::string sid_str = tokens[1];
        row.sensor_id = std::stoi(sid_str.substr(1));
        // Parse Fog_Node_ID: "F0" -> 0
        std::string fid_str = tokens[2];
        row.fog_id = std::stoi(fid_str.substr(1));
        row.temperature = std::stod(tokens[3]);
        row.pressure    = std::stod(tokens[4]);
        row.vibration   = std::stod(tokens[5]);
        row.is_failure  = (tokens[6] == "1");
        raw_rows.push_back(row);
    }

    if (raw_rows.empty()) {
        throw std::runtime_error("No data rows found in dataset");
    }

    // Find temperature range for quantization
    double t_min = raw_rows[0].temperature, t_max = raw_rows[0].temperature;
    for (const auto& r : raw_rows) {
        t_min = std::min(t_min, r.temperature);
        t_max = std::max(t_max, r.temperature);
    }

    // Generate readings scaled to the requested sensor count.
    // If num_sensors > dataset sensors, create virtual IDs cyclically.
    int dataset_sensor_count = 0;
    for (const auto& r : raw_rows) {
        dataset_sensor_count = std::max(dataset_sensor_count, r.sensor_id + 1);
    }

    // Generate readings perfectly interleaved by time step.
    // The dataset has `dataset_sensor_count` readings per time step.
    size_t num_time_steps = raw_rows.size() / dataset_sensor_count;
    readings_.reserve(num_time_steps * num_sensors);
    
    for (size_t t = 0; t < num_time_steps; ++t) {
        for (int sid = 0; sid < num_sensors; ++sid) {
            int original_sid = sid % dataset_sensor_count;
            const auto& r = raw_rows[t * dataset_sensor_count + original_sid];
            
            SensorReading sr;
            sr.sensor_id       = sid;
            sr.fog_node_id     = sensor_to_fog_[sid];
            sr.temperature     = r.temperature;
            sr.pressure        = r.pressure;
            sr.vibration       = r.vibration;
            sr.is_failure      = r.is_failure;
            sr.quantized_value = quantize(r.temperature, t_min, t_max);
            readings_.push_back(sr);
        }
    }

    std::cout << "[DatasetLoader] Loaded " << readings_.size()
              << " readings for " << num_sensors << " sensors, "
              << num_fog_nodes << " fog nodes\n";
}

std::vector<std::vector<SensorReading>>
DatasetLoader::groupByEpoch(int num_sensors, int workload_multiplier) const {
    // Each epoch = num_sensors * workload_multiplier readings
    int readings_per_epoch = num_sensors * workload_multiplier;
    std::vector<std::vector<SensorReading>> epochs;

    for (size_t i = 0; i < readings_.size(); i += readings_per_epoch) {
        std::vector<SensorReading> epoch;
        for (size_t j = i; j < std::min(i + (size_t)readings_per_epoch, readings_.size()); ++j) {
            epoch.push_back(readings_[j]);
        }
        // For workload_multiplier > 1, replicate readings within the epoch
        if (workload_multiplier > 1 && !epoch.empty()) {
            std::vector<SensorReading> base = epoch;
            for (int m = 1; m < workload_multiplier && epoch.size() < (size_t)readings_per_epoch; ++m) {
                for (const auto& r : base) {
                    if (epoch.size() >= (size_t)readings_per_epoch) break;
                    epoch.push_back(r);
                }
            }
        }
        if (!epoch.empty()) {
            epochs.push_back(std::move(epoch));
        }
    }

    return epochs;
}

std::vector<int> DatasetLoader::sensorsForFog(int fog_id) const {
    std::vector<int> result;
    for (const auto& [sid, fid] : sensor_to_fog_) {
        if (fid == fog_id) result.push_back(sid);
    }
    return result;
}

} // namespace plosha
