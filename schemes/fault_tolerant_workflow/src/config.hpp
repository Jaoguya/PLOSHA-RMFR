#ifndef FTWORKFLOW_CONFIG_HPP
#define FTWORKFLOW_CONFIG_HPP

#include <cstddef>
#include <string>

// ============================================================================
// Fault-Tolerant Workflow Configuration — Ref[37] Ren & Yao, IEEE TSC 2026
// "A Hybrid Fault-Tolerant Workflow Scheduling With Performance Fluctuated
//  Cloud Resources"
// ============================================================================

namespace ftworkflow {

// ---------------------------------------------------------------------------
// Cryptographic Parameters (same as PLOSHA for fair comparison)
// ---------------------------------------------------------------------------
constexpr int PAILLIER_KEY_BITS    = 2048;
constexpr int AES_KEY_BYTES        = 32;
constexpr int AES_IV_BYTES         = 12;
constexpr int AES_TAG_BYTES        = 16;
constexpr int MAX_SENSOR_VALUE     = 65535;

// ---------------------------------------------------------------------------
// Performance Fluctuation Model (Ref[37] §III-A)
// VM performance coefficient: p_j(t) ~ Uniform[p_min, p_max]
// ---------------------------------------------------------------------------
constexpr double PERF_COEFF_MIN    = 0.5;   // Minimum performance ratio
constexpr double PERF_COEFF_MAX    = 1.0;   // Maximum performance ratio
constexpr double PERF_EWMA_ALPHA   = 0.3;   // Smoothing for performance tracking

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Replication Parameters (Ref[37] §III-C, Eq. 12-14)
// Task replication triggered when failure probability > tau_rep
// ---------------------------------------------------------------------------
constexpr double TAU_REPLICATION    = 0.3;  // Failure probability threshold
constexpr int    MAX_REPLICAS       = 3;    // Maximum replica count
constexpr double REPLICATION_COMM_RATIO = 1.5; // Communication multiplier

// ---------------------------------------------------------------------------
// Workflow Scheduling (Ref[37] §IV)
// Hybrid strategy selection: resubmit if T_task < threshold, else replicate
// ---------------------------------------------------------------------------
constexpr double TASK_DURATION_THRESHOLD_MS = 50.0;  // Resubmit vs replicate

// Failure model: per-epoch failure rate per node
// (adapted from Ref[37]'s Weibull/exponential failure distribution)
constexpr double DEFAULT_NODE_MTBF = 100.0;  // Mean time between failures (epochs)

// ---------------------------------------------------------------------------
// Aggregation — flat single-slot (no micro-slot partitioning)
// Ref[37] does NOT use hierarchical slot aggregation
// ---------------------------------------------------------------------------
constexpr double COMPLETENESS_THRESHOLD = 0.8;

// ---------------------------------------------------------------------------
// Simulation Defaults
// ---------------------------------------------------------------------------
constexpr int    DEFAULT_NUM_SENSORS    = 1000;
constexpr int    DEFAULT_NUM_FOG_NODES  = 10;
constexpr int    DEFAULT_NUM_EPOCHS     = 10;
constexpr double DEFAULT_FAILURE_RATE   = 0.05;

// Mean per-node communication latency under modelled heterogeneity. Same value
// as PLOSHA's config.hpp so both schemes face identical link characteristics.
constexpr double COMM_LATENCY_MEAN_MS   = 5.0;
constexpr int    DEFAULT_WORKLOAD_MULT  = 1;

// ---------------------------------------------------------------------------
// Experiment Configuration
// ---------------------------------------------------------------------------
struct ExperimentConfig {
    int experiment_id       = 0;
    int num_sensors         = DEFAULT_NUM_SENSORS;
    int num_fog_nodes       = DEFAULT_NUM_FOG_NODES;
    int num_epochs          = DEFAULT_NUM_EPOCHS;
    double failure_rate     = DEFAULT_FAILURE_RATE;
    int workload_multiplier = DEFAULT_WORKLOAD_MULT;
    int forced_micro_slots  = 0;      // For Exp 5/6 compatibility (always 1 slot)
    // Ratio between the most- and least-capable fog node. 1.0 = homogeneous
    // (default, all experiments except Exp9). Mirrors the identical field in
    // PLOSHA's config so both schemes face the same node heterogeneity.
    double heterogeneity_ratio = 1.0;
    std::string output_dir  = ".";
    std::string dataset_path = "../../../dataset/plosha_dataset.csv";
};

} // namespace ftworkflow

#endif // FTWORKFLOW_CONFIG_HPP
