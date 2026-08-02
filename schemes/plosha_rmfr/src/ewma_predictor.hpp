#ifndef PLOSHA_EWMA_PREDICTOR_HPP
#define PLOSHA_EWMA_PREDICTOR_HPP

#include "fog_node.hpp"

namespace plosha {

enum class NodeStatus { Stable, Critical };

struct PredictionVector {
    double capacity = 0.0;
    double failure_exposure = 0.0;
    double risk = 0.0;
    NodeStatus status = NodeStatus::Stable;
};

class EWMAPredictor {
public:
    // Predict next state using EWMA smoothing (Phase II Step 2)
    FogState predictState(const FogState& current, const FogState& prev_ewma);

    // Compute remaining capacity (Phase II Step 3, paper Eq. 8)
    double computeCapacity(const FogState& predicted);

    // Compute failure exposure (Phase II Step 4, paper Eq. 10)
    double computeFailureExposure(const FogState& predicted);

    // Compute risk (Phase II Step 5, paper Eq. 12)
    double computeRisk(double capacity, double failure_exposure);

    // Classify node status (Phase II Step 6, paper Eq. 13)
    NodeStatus classifyStatus(double risk, double tau_r);

    // Full prediction pipeline
    PredictionVector predict(const FogState& current, const FogState& prev_ewma,
                             double tau_r = TAU_R_INIT);
};

} // namespace plosha

#endif // PLOSHA_EWMA_PREDICTOR_HPP
