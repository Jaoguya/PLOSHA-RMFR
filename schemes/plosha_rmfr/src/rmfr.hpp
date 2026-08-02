#ifndef PLOSHA_RMFR_HPP
#define PLOSHA_RMFR_HPP

#include "fog_node.hpp"
#include "ewma_predictor.hpp"
#include "crypto_wrapper.hpp"
#include <vector>
#include <cstddef>

// Forward declaration to avoid circular include
namespace plosha { struct MicroSlot; }

namespace plosha {

enum class RecoveryMode { Normal, Delegation, MicroRecovery, Failover };

// Delegation State Package (paper §Phase IV Step 4)
struct DelegationPackage {
    int source_fog_id;
    int target_fog_id;
    FogState state_snapshot;
    std::vector<int> sensor_list;
    size_t payload_bytes = 0;  // Serialized size for communication overhead
};

// Failover State Migration (paper §Phase IV Step 6)
struct FailoverPackage {
    int source_fog_id;
    int target_fog_id;
    FogState state_snapshot;
    std::vector<int> reassigned_sensors;
    std::vector<PaillierCiphertext> partial_aggregates;
    size_t payload_bytes = 0;
};

struct RecoveryResult {
    RecoveryMode mode = RecoveryMode::Normal;
    bool success = true;
    double recovery_latency_ms = 0.0;
    size_t communication_bytes = 0;
    int recovery_target_id = -1;
    PaillierCiphertext recovered_aggregate;
    std::vector<int> incomplete_slots;
};

class RMFREngine {
public:
    // Phase IV Step 1: Recovery Urgency (paper Eq. 26)
    double computeRecoveryUrgency(double risk, double completeness_V, double reliability);

    // Phase IV Step 2: Determine recovery mode (paper Eq. 28)
    RecoveryMode determineRecoveryMode(bool completeness_flag, double recovery_urgency,
                                        double reliability,
                                        double tau_1, double tau_2, double tau_3,
                                        double tau_f);

    // Phase IV Step 3: Select best recovery candidate (paper Eq. 30)
    int selectRecoveryCandidate(const std::vector<FogNode>& fog_nodes,
                                const std::vector<PredictionVector>& predictions,
                                int failed_fog_id);

    // Phase IV Steps 4-6: Execute recovery
    // slots: the partitioned micro-slot data for re-aggregation of incomplete slots
    RecoveryResult executeRecovery(CryptoWrapper& crypto,
                                   std::vector<FogNode>& fog_nodes,
                                   const std::vector<PredictionVector>& predictions,
                                   int failed_fog_id,
                                   const std::vector<uint8_t>& aes_key,
                                   double tau_1, double tau_2, double tau_3,
                                   double tau_f,
                                   double completeness_V, bool completeness_flag,
                                   double risk, double reliability,
                                   const std::vector<int>& incomplete_slot_indices = {},
                                   const std::vector<MicroSlot>& slots = {});

    // Phase IV Step 7: Update reliability (paper Eq. 38)
    double updateReliability(double current_rel, bool success, double completeness_V);
};

} // namespace plosha

#endif // PLOSHA_RMFR_HPP
