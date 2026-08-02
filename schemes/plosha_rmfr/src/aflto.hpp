#ifndef PLOSHA_AFLTO_HPP
#define PLOSHA_AFLTO_HPP

#include "crypto_wrapper.hpp"
#include "ewma_predictor.hpp"
#include "rmfr.hpp"

namespace plosha {

// Adaptive thresholds maintained by AFLTO
struct AdaptiveThresholds {
    double tau_1 = TAU_1_INIT;  // Delegation
    double tau_2 = TAU_2_INIT;  // MicroRecovery
    double tau_3 = TAU_3_INIT;  // Failover
    double tau_v = TAU_V_INIT;  // Completeness
    double tau_r = TAU_R_INIT;  // Risk classification
    double tau_f = TAU_F_INIT;  // Reliability floor (failover trigger)
};

// Per-fog-node feedback state
struct FeedbackState {
    double score = 0.0;           // Score_i(t) = ω₁·V + ω₂·Rel
    double history = 0.0;         // Hist_i(t)
    double fused_score = 0.0;     // Score_i*(t)
    double error = 0.0;           // e_i(t)
    AdaptiveThresholds thresholds;
};

class AFLTOEngine {
public:
    // Phase V Step 1: Determine final aggregate
    // If recovery occurred and succeeded, use recovered aggregate
    PaillierCiphertext determineFinalAggregate(
        const PaillierCiphertext& C_agg,
        const PaillierCiphertext& C_agg_recovered,
        bool completeness_flag, bool recovery_success);

    // Phase V Step 1b: Sign and commit (real ECDSA, timed)
    void signAndCommit(CryptoWrapper& crypto, const std::string& transaction_data);

    // Phase V Step 2: Evaluate performance (paper Eq. 41)
    double evaluatePerformance(double completeness_V, double reliability);

    // Phase V Step 3: Update history and compute error (paper Eqs. 43-46)
    void updateHistoryAndError(FeedbackState& fb, double score,
                               double recovery_urgency, double reliability);

    // Phase V Step 4: Update thresholds (paper Eq. 48-50)
    void updateThresholds(FeedbackState& fb);

    // Full AFLTO pipeline for one fog node
    FeedbackState processFeedback(CryptoWrapper& crypto,
                                   const FeedbackState& previous,
                                   double completeness_V, double reliability,
                                   double recovery_urgency,
                                   bool aflto_enabled);
};

} // namespace plosha

#endif // PLOSHA_AFLTO_HPP
