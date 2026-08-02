#ifndef PLOSHA_PLOSHA_HPP
#define PLOSHA_PLOSHA_HPP

#include "crypto_wrapper.hpp"
#include "fog_node.hpp"
#include "ewma_predictor.hpp"
#include <vector>

namespace plosha {

struct MicroSlot {
    std::vector<QueuedReading> readings;
};

struct AggregationResult {
    PaillierCiphertext C_agg;                     // Final fog-level aggregate
    std::vector<PaillierCiphertext> micro_aggs;   // Per-micro-slot aggregates
    int m_star = 1;                               // Optimal micro-slot count
    double completeness_V = 0.0;                  // V_i(t) = N_recv / N_exp
    bool completeness_flag = true;                 // Φ_i(t) = (V >= τ_v)
    double aggregation_latency_ms = 0.0;          // Wall-clock time for Phase III
    double processing_overhead_ms = 0.0;          // Time for TEE transform + micro-slot agg only
};

class PLOSHAEngine {
public:
    // Phase III Step 1: Compute optimal micro-slots m* (paper Eq. 16)
    // Uses calibrated β_t instead of hardcoded value
    int computeOptimalMicroSlots(const PredictionVector& pred,
                                 int num_readings, double beta_t_calibrated,
                                 double reliability);

    // Phase III Step 2: Partition readings into m* micro-slots
    std::vector<MicroSlot> partitionEpoch(const std::vector<QueuedReading>& readings,
                                          int m_star);

    // Phase III Steps 3-5: Full aggregation pipeline
    // teeTransform → aggregateMicroSlot → aggregateFog
    AggregationResult aggregate(CryptoWrapper& crypto,
                                const std::vector<uint8_t>& aes_key,
                                const std::vector<QueuedReading>& readings,
                                int num_expected_sensors,
                                const PredictionVector& pred,
                                double beta_t_calibrated,
                                double tau_v,
                                double reliability,
                                int forced_m_star = 0,
                                bool hierarchical = true);  // false = skip fog-level hierarchy
};

} // namespace plosha

#endif // PLOSHA_PLOSHA_HPP
