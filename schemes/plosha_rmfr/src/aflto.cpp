#include "aflto.hpp"
#include "config.hpp"
#include <algorithm>
#include <cmath>

namespace plosha {

// Phase V Step 1: Determine final aggregate
PaillierCiphertext AFLTOEngine::determineFinalAggregate(
    const PaillierCiphertext& C_agg,
    const PaillierCiphertext& C_agg_recovered,
    bool completeness_flag, bool recovery_success) {
    // If complete, use original aggregate
    if (completeness_flag) return C_agg;
    // If recovery succeeded, use recovered
    if (recovery_success) return C_agg_recovered;
    // Otherwise, use best-effort original
    return C_agg;
}

// Phase V Step 1b: Sign and commit (real ECDSA signature, timed)
void AFLTOEngine::signAndCommit(CryptoWrapper& crypto,
                                 const std::string& transaction_data) {
    crypto.ecdsaSign(transaction_data);
}

// Phase V Step 2: Performance score (paper Eq. 41)
// Score_i(t) = ω₁·V + ω₂·Rel
double AFLTOEngine::evaluatePerformance(double completeness_V, double reliability) {
    return OMEGA_SCORE_1 * completeness_V + OMEGA_SCORE_2 * reliability;
}

// Phase V Step 3: Update history and compute error (paper Eqs. 43-46)
// Hist_i(t+1) = γ·Hist_i(t) + (1−γ)·Score_i(t)
// Score_i*(t) = α_h·Hist_i(t+1) + (1−α_h)·Score_i(t)
// e_i(t) = κ₁·(S̄ − Score*) + κ₂·RU + κ₃·(1 − Rel)   (R3: uses SCORE_TARGET)
void AFLTOEngine::updateHistoryAndError(FeedbackState& fb, double score,
                                         double recovery_urgency, double reliability) {
    fb.score = score;
    fb.history = GAMMA_HIST * fb.history + (1.0 - GAMMA_HIST) * score;
    fb.fused_score = ALPHA_H * fb.history + (1.0 - ALPHA_H) * score;
    fb.error = KAPPA_1 * (SCORE_TARGET - fb.fused_score)
             + KAPPA_2 * recovery_urgency
             + KAPPA_3 * (1.0 - reliability);
}

// Phase V Step 4: Update thresholds (paper Eq. 48-50)
// τ_x(t+1) = Π_{[0,1]}(τ_x(t) − μ_x · e_i(t))
// Enforce ordering: τ₁ < τ₂ < τ₃
void AFLTOEngine::updateThresholds(FeedbackState& fb) {
    auto& t = fb.thresholds;
    double delta = MU_LEARNING * fb.error;

    // Apply gradient update with projection to [0, 1]
    auto project = [](double val) { return std::max(0.0, std::min(1.0, val)); };

    t.tau_1 = project(t.tau_1 - delta);
    t.tau_2 = project(t.tau_2 - delta);
    t.tau_3 = project(t.tau_3 - delta);
    t.tau_v = project(t.tau_v - delta * 0.5);  // Slower adaptation for completeness
    t.tau_r = project(t.tau_r - delta * 0.5);  // Slower adaptation for risk
    t.tau_f = project(t.tau_f - delta * 0.5);  // Slower adaptation for reliability floor

    // Enforce ordering: τ₁ < τ₂ < τ₃ with minimum gap of 0.05
    constexpr double MIN_GAP = 0.05;
    if (t.tau_2 <= t.tau_1 + MIN_GAP) t.tau_2 = t.tau_1 + MIN_GAP;
    if (t.tau_3 <= t.tau_2 + MIN_GAP) t.tau_3 = t.tau_2 + MIN_GAP;
    // Re-project after ordering enforcement
    t.tau_1 = project(t.tau_1);
    t.tau_2 = project(t.tau_2);
    t.tau_3 = project(t.tau_3);
}

// Full AFLTO pipeline
FeedbackState AFLTOEngine::processFeedback(CryptoWrapper& /*crypto*/,
                                            const FeedbackState& previous,
                                            double completeness_V, double reliability,
                                            double recovery_urgency,
                                            bool aflto_enabled) {
    FeedbackState fb = previous;

    double score = evaluatePerformance(completeness_V, reliability);
    updateHistoryAndError(fb, score, recovery_urgency, reliability);

    if (aflto_enabled) {
        updateThresholds(fb);
    }
    // If AFLTO disabled, thresholds remain at initial values (static)

    return fb;
}

} // namespace plosha
