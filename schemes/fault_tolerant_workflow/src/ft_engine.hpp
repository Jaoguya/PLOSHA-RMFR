#ifndef FTWORKFLOW_ENGINE_HPP
#define FTWORKFLOW_ENGINE_HPP

#include "config.hpp"
#include "dataset_loader.hpp"
#include "crypto_wrapper.hpp"
#include "sensor.hpp"
#include "fog_node.hpp"
#include "metrics.hpp"
#include <vector>
#include <random>

namespace ftworkflow {

class FTEngine {
public:
    FTEngine();
    void runExperiment(const ExperimentConfig& config);
    void runAll(const ExperimentConfig& base_config);

private:
    CryptoWrapper crypto_;
    DatasetLoader dataset_;
    MetricsCollector metrics_;
    double beta_t_calibrated_ = 0.001;

    void initializeSystem(const ExperimentConfig& config,
                          std::vector<Sensor>& sensors,
                          std::vector<FogNode>& fog_nodes,
                          std::vector<uint8_t>& fog_aes_key);

    EpochMetrics runEpoch(const ExperimentConfig& config,
                          std::vector<Sensor>& sensors,
                          std::vector<FogNode>& fog_nodes,
                          const std::vector<uint8_t>& fog_aes_key,
                          const std::vector<std::vector<SensorReading>>& epoch_data,
                          int epoch_index,
                          std::mt19937& rng);

    void runExp1_SensorScalability(const ExperimentConfig& config);
    void runExp2_FogScalability(const ExperimentConfig& config);
    void runExp3_WorkloadIntensity(const ExperimentConfig& config);
    void runExp4_FailureRate(const ExperimentConfig& config);
    void runExp5_LossExposure(const ExperimentConfig& config);
    void runExp6_RecoveryComm(const ExperimentConfig& config);
    void runExp9_SchedulingEfficiency(const ExperimentConfig& config);

    struct SweepConfig {
        std::string variable_name;
        std::vector<double> sweep_values;
        std::string output_dir;
    };

    std::vector<SweepPointResult> runSweep(const SweepConfig& sweep,
                                           ExperimentConfig config);
};

} // namespace ftworkflow

#endif // FTWORKFLOW_ENGINE_HPP
