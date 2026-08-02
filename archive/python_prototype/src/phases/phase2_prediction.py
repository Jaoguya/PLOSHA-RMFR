"""
Phase II: Predictive Capacity and Risk Estimation
===================================================
Proactively predicts the future operating condition of each fog node
using EWMA-based prediction to support adaptive aggregation planning
and risk-aware recovery.

Gramine-SGX Edition — Capacity formula includes Rel_hat factor,
risk uses η_1/η_2 weights per Experiment Plan §4, Table 3.

Paper Reference: Section III-C, Phase II (7 Steps)
  Step 1: Runtime State Collection
  Step 2: Future State Prediction (EWMA)
  Step 3: Effective Aggregation Capacity Estimation
  Step 4: Failure Exposure Analysis
  Step 5: Operational Risk Estimation
  Step 6: Risk Classification
  Step 7: Prediction Output
"""

from typing import List, Dict

from src.config import SystemConfig
from src.entities.fog_node import FogNode


def execute(config: SystemConfig, fog_nodes: List[FogNode]) -> Dict[int, dict]:
    """
    Execute Phase II: Predictive Capacity and Risk Estimation.

    Generates prediction vector Pred_i(t) for each fog node.

    Args:
        config: System configuration with EWMA and risk parameters.
        fog_nodes: List of fog nodes with current state vectors.

    Returns:
        Dictionary mapping fog_node_id → Pred_i(t) = {cap, fe, risk, status}
    """
    predictions = {}

    for fog in fog_nodes:
        pred = _predict_single_node(config, fog)
        predictions[fog.node_id] = pred

    return predictions


def _predict_single_node(config: SystemConfig, fog: FogNode) -> dict:
    """
    Execute all 7 steps of Phase II for a single fog node.

    Args:
        config: System configuration.
        fog: Fog node with current state.

    Returns:
        Prediction result dictionary.
    """
    alpha = config.alpha_ewma

    # =========================================================================
    # Step 1: Runtime State Collection
    # State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)]
    # "All state variables are normalized to the interval [0,1]"
    # =========================================================================
    W_t = fog.workload
    Q_t = fog.queue_util
    L_t = fog.latency
    R_t = fog.reliability

    # =========================================================================
    # Step 2: Future State Prediction (EWMA)
    # Ŝtate_i(t+1) = α · State_i(t) + (1-α) · Ŝtate_i(t)
    # where α ∈ (0,1) denotes the smoothing coefficient
    # =========================================================================
    fog.pred_workload = alpha * W_t + (1 - alpha) * fog.pred_workload
    fog.pred_queue = alpha * Q_t + (1 - alpha) * fog.pred_queue
    fog.pred_latency = alpha * L_t + (1 - alpha) * fog.pred_latency
    fog.pred_reliability = alpha * R_t + (1 - alpha) * fog.pred_reliability

    W_hat = fog.pred_workload
    Q_hat = fog.pred_queue
    L_hat = fog.pred_latency
    R_hat = fog.pred_reliability

    # =========================================================================
    # Step 3: Effective Aggregation Capacity Estimation
    # Cap_i(t+1) = R̂el_i(t+1) · (1 - (ω_w · Ŵ_i + ω_q · Q̂_i + ω_l · L̂_i))
    # subject to ω_w + ω_q + ω_l = 1
    #
    # Experiment Plan §4, Table 3:
    #   Cap = Rel_hat * (1 - (ow*W_hat + oq*Q_hat + ol*L_hat))
    #
    # "A larger value indicates greater available processing resources"
    # =========================================================================
    load_factor = (config.omega_w * W_hat +
                   config.omega_q * Q_hat +
                   config.omega_l * L_hat)
    cap = R_hat * (1.0 - load_factor)
    cap = max(0.0, min(1.0, cap))  # Ensure 0 ≤ Cap ≤ 1
    fog.cap = cap

    # =========================================================================
    # Step 4: Failure Exposure Analysis
    # FE_i(t) = Ŵ_i(t+1) · Q̂_i(t+1) · (1 - R̂el_i(t+1))
    # "A larger value indicates increased vulnerability to aggregation
    #  interruption caused by simultaneous workload pressure, queue
    #  congestion, and reliability degradation"
    # =========================================================================
    fe = W_hat * Q_hat * (1.0 - R_hat)
    fe = max(0.0, min(1.0, fe))  # Ensure 0 ≤ FE ≤ 1
    fog.failure_exposure = fe

    # =========================================================================
    # Step 5: Operational Risk Estimation
    # Risk_i(t) = η_1 · (1 - Cap_i(t+1)) + η_2 · FE_i(t)
    # subject to η_1 + η_2 = 1
    # "A larger value indicates approaching overload, instability, or
    #  service degradation"
    # =========================================================================
    risk = config.eta_1 * (1.0 - cap) + config.eta_2 * fe
    risk = max(0.0, min(1.0, risk))  # Ensure 0 ≤ Risk ≤ 1
    fog.risk = risk

    # =========================================================================
    # Step 6: Risk Classification
    # Status_i(t) = "Normal"  if Risk_i(t) < τ_r
    #             = "At-Risk" if Risk_i(t) ≥ τ_r
    # where τ_r is the adaptive risk threshold maintained by AFLTO
    # =========================================================================
    if risk < config.tau_r:
        status = "Normal"
    else:
        status = "At-Risk"
    fog.status = status

    # =========================================================================
    # Step 7: Prediction Output
    # Pred_i(t) = [Cap_i(t+1), FE_i(t), Risk_i(t)]
    # "Forwarded to PLOSHA for adaptive aggregation planning and to
    #  RMFR for risk-aware recovery decision making"
    # =========================================================================
    return {
        'capacity': cap,
        'failure_exposure': fe,
        'risk': risk,
        'status': status,
        'predicted_state': {
            'workload': W_hat,
            'queue': Q_hat,
            'latency': L_hat,
            'reliability': R_hat
        }
    }
