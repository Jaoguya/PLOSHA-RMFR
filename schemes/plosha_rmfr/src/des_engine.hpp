#ifndef PLOSHA_DES_ENGINE_HPP
#define PLOSHA_DES_ENGINE_HPP

#include "config.hpp"
#include "dataset_loader.hpp"
#include "crypto_wrapper.hpp"
#include "sensor.hpp"
#include "fog_node.hpp"
#include "ewma_predictor.hpp"
#include "plosha.hpp"
#include "rmfr.hpp"
#include "aflto.hpp"
#include "metrics.hpp"
#include <vector>
#include <random>

namespace plosha {

class DESEngine {
public:
    DESEngine();

    // Run a single experiment (1-9) with the given configuration
    void runExperiment(const ExperimentConfig& config);

    // Run all 9 experiments sequentially
    void runAll(const ExperimentConfig& base_config);

private:
    CryptoWrapper crypto_;
    DatasetLoader dataset_;
    EWMAPredictor ewma_;
    PLOSHAEngine plosha_engine_;
    RMFREngine rmfr_engine_;
    AFLTOEngine aflto_engine_;
    MetricsCollector metrics_;

    double beta_t_calibrated_ = 0.001;  // Updated by calibration at startup

    // Initialize the simulation for a given configuration
    void initializeSystem(const ExperimentConfig& config,
                          std::vector<Sensor>& sensors,
                          std::vector<FogNode>& fog_nodes,
                          std::vector<uint8_t>& fog_aes_key);

    // Run one epoch of the simulation (Phase II → V)
    EpochMetrics runEpoch(const ExperimentConfig& config,
                          std::vector<Sensor>& sensors,
                          std::vector<FogNode>& fog_nodes,
                          const std::vector<uint8_t>& fog_aes_key,
                          const std::vector<std::vector<SensorReading>>& epoch_data,
                          int epoch_index,
                          std::vector<FogState>& ewma_states,
                          std::vector<FeedbackState>& feedback_states,
                          std::mt19937& rng);

    // Experiment runners
    void runExp1_SensorScalability(const ExperimentConfig& config);
    void runExp2_FogScalability(const ExperimentConfig& config);
    void runExp3_WorkloadIntensity(const ExperimentConfig& config);
    void runExp4_FailureRate(const ExperimentConfig& config);
    void runExp5_LossExposure(const ExperimentConfig& config);
    void runExp6_RecoveryComm(const ExperimentConfig& config);
    void runExp7_AFLTOAblation(const ExperimentConfig& config);
    void runExp8_AblationAggregation(const ExperimentConfig& config);
    void runExp9_SchedulingEfficiency(const ExperimentConfig& config);
    // Exp10: workload imbalance under heterogeneous fog-node capability.
    void runExp10_Heterogeneity(const ExperimentConfig& config);

    // Helper: run a parameter sweep
    struct SweepConfig {
        std::string variable_name;
        std::vector<double> sweep_values;
        std::string output_dir;
    };

    std::vector<SweepPointResult> runSweep(const SweepConfig& sweep,
                                           ExperimentConfig config);
};

} // namespace plosha

#endif // PLOSHA_DES_ENGINE_HPP
