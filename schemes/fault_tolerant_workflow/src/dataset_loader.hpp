#ifndef FTWORKFLOW_DATASET_LOADER_HPP
#define FTWORKFLOW_DATASET_LOADER_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace ftworkflow {

struct SensorReading {
    int sensor_id;
    int fog_node_id;
    double temperature;
    double pressure;
    double vibration;
    bool is_failure;
    uint32_t quantized_value;
};

class DatasetLoader {
public:
    void load(const std::string& csv_path, int num_sensors, int num_fog_nodes);
    const std::vector<SensorReading>& readings() const { return readings_; }
    std::vector<std::vector<SensorReading>> groupByEpoch(int num_sensors,
                                                          int workload_multiplier = 1) const;
    const std::map<int, int>& sensorToFog() const { return sensor_to_fog_; }
    int totalReadings() const { return static_cast<int>(readings_.size()); }

private:
    std::vector<SensorReading> readings_;
    std::map<int, int> sensor_to_fog_;
    static uint32_t quantize(double value, double min_val, double max_val);
};

} // namespace ftworkflow

#endif // FTWORKFLOW_DATASET_LOADER_HPP
