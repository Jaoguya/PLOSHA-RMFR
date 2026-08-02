#ifndef PLOSHA_DATASET_LOADER_HPP
#define PLOSHA_DATASET_LOADER_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace plosha {

// One row from plosha_dataset.csv, parsed and ready for simulation
struct SensorReading {
    int sensor_id;
    int fog_node_id;
    double temperature;
    double pressure;
    double vibration;
    bool is_failure;
    uint32_t quantized_value; // Temperature scaled to [0, 65535]
};

// Groups readings into epochs and manages sensor-fog assignment
class DatasetLoader {
public:
    // Load the CSV and pre-process for the given sensor/fog counts.
    // If the dataset has fewer sensors than requested, virtual IDs are
    // created with readings assigned cyclically.
    void load(const std::string& csv_path,
              int num_sensors,
              int num_fog_nodes);

    // Access the full list of parsed readings
    const std::vector<SensorReading>& readings() const { return readings_; }

    // Get readings grouped by epoch. Each epoch contains one reading per
    // active sensor (or as many as the workload multiplier allows).
    // epoch_index -> vector of readings
    std::vector<std::vector<SensorReading>> groupByEpoch(int num_sensors,
                                                          int workload_multiplier = 1) const;

    // Sensor-to-fog assignment map Γ(S_j) = F_i
    const std::map<int, int>& sensorToFog() const { return sensor_to_fog_; }

    // List of sensor IDs assigned to a fog node
    std::vector<int> sensorsForFog(int fog_id) const;

    int totalReadings() const { return static_cast<int>(readings_.size()); }

private:
    std::vector<SensorReading> readings_;
    std::map<int, int> sensor_to_fog_;  // sensor_id -> fog_node_id

    // Quantize temperature to [0, MAX_SENSOR_VALUE]
    static uint32_t quantize(double value, double min_val, double max_val);
};

} // namespace plosha

#endif // PLOSHA_DATASET_LOADER_HPP
