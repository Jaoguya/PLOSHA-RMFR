#include "ewma_predictor.hpp"
#include "config.hpp"

namespace plosha {

// Phase II Step 2: EWMA prediction
// Ŝ_i(t+1) = α · S_i(t) + (1 − α) · Ŝ_i(t)
FogState EWMAPredictor::predictState(const FogState& current, const FogState& prev_ewma) {
    FogState predicted;
    predicted.workload   = ALPHA_EWMA * current.workload   + (1.0 - ALPHA_EWMA) * prev_ewma.workload;
    predicted.queue_load = ALPHA_EWMA * current.queue_load + (1.0 - ALPHA_EWMA) * prev_ewma.queue_load;
    predicted.latency    = ALPHA_EWMA * current.latency    + (1.0 - ALPHA_EWMA) * prev_ewma.latency;
    predicted.reliability = ALPHA_EWMA * current.reliability + (1.0 - ALPHA_EWMA) * prev_ewma.reliability;
    return predicted;
}

// Phase II Step 3: Capacity (paper Eq. 8)
// Cap_i(t+1) = Rel_hat * [1 - (ω_w·W_hat + ω_q·Q_hat + ω_l·L_hat)]
double EWMAPredictor::computeCapacity(const FogState& predicted) {
    double load = OMEGA_W * predicted.workload
                + OMEGA_Q * predicted.queue_load
                + OMEGA_L * predicted.latency;
    return predicted.reliability * (1.0 - load);
}

// Phase II Step 4: Failure Exposure (paper Eq. 10)
// FE_i(t) = W_hat · Q_hat · (1 - Rel_hat)
double EWMAPredictor::computeFailureExposure(const FogState& predicted) {
    return predicted.workload * predicted.queue_load * (1.0 - predicted.reliability);
}

// Phase II Step 5: Risk (paper Eq. 12)
// Risk_i(t) = η₁·(1 - Cap) + η₂·FE
double EWMAPredictor::computeRisk(double capacity, double failure_exposure) {
    return ETA_1 * (1.0 - capacity) + ETA_2 * failure_exposure;
}

// Phase II Step 6: Status Classification (paper Eq. 13)
NodeStatus EWMAPredictor::classifyStatus(double risk, double tau_r) {
    return (risk >= tau_r) ? NodeStatus::Critical : NodeStatus::Stable;
}

// Full prediction pipeline
PredictionVector EWMAPredictor::predict(const FogState& current, const FogState& prev_ewma,
                                        double tau_r) {
    FogState predicted = predictState(current, prev_ewma);
    PredictionVector pv;
    pv.capacity = computeCapacity(predicted);
    pv.failure_exposure = computeFailureExposure(predicted);
    pv.risk = computeRisk(pv.capacity, pv.failure_exposure);
    pv.status = classifyStatus(pv.risk, tau_r);
    return pv;
}

} // namespace plosha
