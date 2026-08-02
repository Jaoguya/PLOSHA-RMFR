#ifndef PLOSHA_CONFIG_HPP
#define PLOSHA_CONFIG_HPP

#include <cstddef>
#include <string>

// ============================================================================
// PLOSHA-RMFR Configuration — All parameters from the paper
// ============================================================================

namespace plosha {

// ---------------------------------------------------------------------------
// Cryptographic Parameters
// ---------------------------------------------------------------------------
constexpr int PAILLIER_KEY_BITS    = 2048;
constexpr int AES_KEY_BYTES        = 32;   // AES-256
constexpr int AES_IV_BYTES         = 12;   // GCM standard
constexpr int AES_TAG_BYTES        = 16;   // GCM tag
constexpr int MAX_SENSOR_VALUE     = 65535; // Quantized sensor range [0, 65535]

// ---------------------------------------------------------------------------
// Phase II: EWMA Prediction Parameters
// ---------------------------------------------------------------------------
constexpr double ALPHA_EWMA        = 0.3;  // Smoothing coefficient α

// Capacity weights (ω_w + ω_q + ω_l = 1)
constexpr double OMEGA_W           = 1.0 / 3.0;  // Workload weight
constexpr double OMEGA_Q           = 1.0 / 3.0;  // Queue weight
constexpr double OMEGA_L           = 1.0 / 3.0;  // Latency weight

// Risk weights (η₁ + η₂ = 1)
constexpr double ETA_1             = 0.5;  // Capacity degradation weight
constexpr double ETA_2             = 0.5;  // Failure exposure weight

// ---------------------------------------------------------------------------
// Phase III: PLOSHA Aggregation Parameters
// ---------------------------------------------------------------------------
constexpr int    M_MAX             = 20;   // Maximum micro-slots per epoch
constexpr double BETA_T            = 0.001; // Per-micro-slot overhead (seconds)
constexpr double ECDSA_VERIFY_MS   = 0.055; // Measured: 18080 verify/s on i5-10600 (openssl speed ecdsap256)
constexpr int    FIXED_SLOT_M      = 10;    // Static micro-slot count for Fixed-Slot ablation (M_MAX/2)

// Optimization weights (λ₁ + λ₂ + λ₃ = 1)
constexpr double LAMBDA_1          = 1.0 / 3.0;  // Aggregation efficiency
constexpr double LAMBDA_2          = 1.0 / 3.0;  // Failure exposure
constexpr double LAMBDA_3          = 1.0 / 3.0;  // Reliability

// Completeness threshold (initial, adapted by AFLTO)
constexpr double TAU_V_INIT        = 0.8;

// ---------------------------------------------------------------------------
// Phase IV: RMFR Recovery Parameters
// ---------------------------------------------------------------------------
// Recovery urgency weights (ρ₁ + ρ₂ + ρ₃ = 1)
constexpr double RHO_1             = 1.0 / 3.0;  // Risk weight
constexpr double RHO_2             = 1.0 / 3.0;  // Incompleteness weight
constexpr double RHO_3             = 1.0 / 3.0;  // Unreliability weight

// Recovery escalation thresholds (initial, adapted by AFLTO)
constexpr double TAU_1_INIT        = 0.3;  // Delegation threshold
constexpr double TAU_2_INIT        = 0.5;  // Micro-recovery threshold
constexpr double TAU_3_INIT        = 0.7;  // Failover threshold
constexpr double TAU_F_INIT        = 0.2;  // Reliability floor
constexpr double TAU_R_INIT        = 0.6;  // Risk threshold

// Recovery candidate weights (α_c + α_r + α_k = 1)
constexpr double ALPHA_C           = 1.0 / 3.0;  // Capacity
constexpr double ALPHA_R           = 1.0 / 3.0;  // Reliability
constexpr double ALPHA_K           = 1.0 / 3.0;  // Risk complement

// Reliability update
constexpr double BETA_REL          = 0.7;  // Reliability smoothing β_r
constexpr double LAMBDA_S          = 0.5;  // Success weight
constexpr double LAMBDA_V          = 0.5;  // Completeness weight

// ---------------------------------------------------------------------------
// Phase V: AFLTO Parameters
// ---------------------------------------------------------------------------
constexpr double GAMMA_HIST        = 0.8;  // History smoothing γ
constexpr double ALPHA_H           = 0.6;  // History-current fusion α_h
constexpr double KAPPA_1           = 1.0 / 3.0;  // Score error weight
constexpr double KAPPA_2           = 1.0 / 3.0;  // Recovery urgency weight
constexpr double KAPPA_3           = 1.0 / 3.0;  // Reliability error weight
constexpr double MU_LEARNING       = 0.05; // Learning rate μ_x
constexpr double SCORE_TARGET      = 0.95; // Target score S̄
constexpr double OMEGA_SCORE_1     = 0.5;  // Completeness score weight ω₁
constexpr double OMEGA_SCORE_2     = 0.5;  // Reliability score weight ω₂

// ---------------------------------------------------------------------------
// Simulation Defaults
// ---------------------------------------------------------------------------
constexpr int    DEFAULT_NUM_SENSORS    = 1000;
constexpr int    DEFAULT_NUM_FOG_NODES  = 10;
constexpr int    DEFAULT_NUM_EPOCHS     = 30;
constexpr double DEFAULT_FAILURE_RATE   = 0.05;  // 5%
constexpr int    DEFAULT_WORKLOAD_MULT  = 1;
constexpr int    ABLATION_EPOCHS        = 30;   // Standardized: all experiments use 30 epochs

// Failure rate used by Experiment 1 (aggregation ablation) when the caller
// does not pass --failure-rate. It is deliberately higher than
// DEFAULT_FAILURE_RATE: recovery cost is what separates the ablation variants,
// and at 5% too few epochs trigger recovery for the variants to be
// distinguishable. NOTE: this value lies outside the 0.02-0.35 range swept by
// the failure-rate experiment, so any reported figure using it must say so.
// Override on the command line for sensitivity analysis.
constexpr double EXP1_ABLATION_FAILURE_RATE = 0.50;

// Mean per-node communication (network) latency used when an experiment models
// heterogeneous fog capability. Applied only when heterogeneity_ratio > 1, so
// homogeneous experiments are unaffected. 5 ms is a representative edge-to-fog
// RTT and is small relative to the 100 ms max_latency normalizer, so it shifts
// L_i without saturating it.
constexpr double COMM_LATENCY_MEAN_MS = 5.0;

// ---------------------------------------------------------------------------
// Experiment Configuration
// ---------------------------------------------------------------------------
struct ExperimentConfig {
    int experiment_id       = 0;      // 1-9, or 0 for "all"
    int num_sensors         = DEFAULT_NUM_SENSORS;
    int num_fog_nodes       = DEFAULT_NUM_FOG_NODES;
    int num_epochs          = DEFAULT_NUM_EPOCHS;
    double failure_rate     = DEFAULT_FAILURE_RATE;
    // Set by the CLI parser when --failure-rate is supplied, so experiments
    // carrying their own default can tell "explicitly requested" apart from
    // "left unset" instead of ignoring the flag outright.
    bool failure_rate_set   = false;
    int workload_multiplier = DEFAULT_WORKLOAD_MULT;
    int forced_micro_slots  = 0;      // 0 = use optimizer; >0 = force m*
    double failure_injection_time = 0.0; // 0.0 = random time; >0.0 = specific fraction (e.g. 0.25)
    bool aflto_enabled      = true;
    bool hierarchical_aggregation = true; // false = skip fog-level hierarchy
    // --- Heterogeneity experiment (Exp10) ---
    // Ratio between the most- and least-capable fog node. 1.0 = homogeneous
    // (every node identical, as in all other experiments). >1.0 spreads queue
    // capacity and processing latency across nodes, deterministically.
    double heterogeneity_ratio = 1.0;
    // When true, the capacity-aware scheduling decision is APPLIED to routing
    // (sensors re-associate to the selected fog node) instead of being computed
    // and discarded. Off everywhere except Exp10, so no existing experiment's
    // behaviour or published numbers change.
    bool apply_scheduling_decision = false;
    std::string output_dir  = ".";
    std::string dataset_path = "../../../dataset/plosha_dataset.csv";
    bool skip_native_exp8   = false;
};

} // namespace plosha

#endif // PLOSHA_CONFIG_HPP
