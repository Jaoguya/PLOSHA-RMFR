"""
Phase V: Adaptive Feedback Learning and Threshold Optimization (AFLTO)
=======================================================================
Securely commits the final aggregation result and continuously refines
operational parameters through closed-loop optimization.

Gramine-SGX Edition — separates γ (decay) from α_h (blend), uses
κ_1/κ_2/κ_3 error weights, S̄ = 0.95, margin = 0.01.

Paper Reference: Section III-C, Phase V (5 Steps)
  Step 1: Final Aggregate Commitment
  Step 2: Performance Evaluation
  Step 3: Historical Learning and Error Estimation
  Step 4: Adaptive Threshold Optimization
  Step 5: Feedback Generation and Closed-Loop Adaptation
"""

from typing import Dict, Optional

from src.config import SystemConfig
from src.entities.fog_node import FogNode
from src.entities.cloud_server import CloudServer
from src.crypto.tee import compute_hash


def execute(config: SystemConfig, fog_node: FogNode,
            agg_output: dict, rec_output: dict,
            cloud: CloudServer, epoch: int) -> dict:
    """
    Execute Phase V: AFLTO for a single fog node.

    Commits aggregation result and adapts thresholds.

    Args:
        config: System configuration (mutable thresholds).
        fog_node: Fog node being processed.
        agg_output: Aggregation output from Phase III.
        rec_output: Recovery output from Phase IV.
        cloud: Cloud server for commitment.
        epoch: Current epoch number.

    Returns:
        Feedback state FB_i(t).
    """

    # =========================================================================
    # Step 1: Final Aggregate Commitment
    # C_i^{final} = C_{agg,i}        if Φ_i = 0
    #             = C_{agg,i}^{rec}  if Φ_i = 1 ∧ RecStatus = 1
    #             = C_{agg,i}        if Φ_i = 1 ∧ RecStatus = 0
    #
    # T_i(t) = (C_i^{final}, Mode_i, Rel_i(t+1), RecStatus_i)
    # σ_i(t) = Sign_{sk_i^TEE}(H(T_i(t)))
    # Cloud stores (T_i(t), σ_i(t)) only after verification
    # =========================================================================
    incomplete_flag = agg_output['incomplete_flag']
    recovery_status = rec_output['recovery_status']

    if incomplete_flag == 0:
        final_aggregate = agg_output['fog_aggregate']
    elif incomplete_flag == 1 and recovery_status == 1:
        final_aggregate = rec_output.get('recovered_aggregate',
                                          agg_output['fog_aggregate'])
    else:
        # Φ_i = 1 and RecStatus = 0: use original (possibly incomplete)
        final_aggregate = agg_output['fog_aggregate']

    # Construct aggregation package T_i(t)
    aggregation_package = {
        'final_aggregate': str(final_aggregate),
        'mode': rec_output['mode'],
        'reliability': rec_output['new_reliability'],
        'recovery_status': recovery_status
    }

    # Sign inside TEE enclave (Gramine-SGX ECDSA)
    hash_data = compute_hash((
        str(final_aggregate),
        str(rec_output['mode']),
        str(rec_output['new_reliability']),
        str(recovery_status)
    ))

    if fog_node.tee.attested:
        signature = fog_node.tee.sign(hash_data)
        # Submit to cloud with signature verification
        cloud.receive_and_verify(
            fog_node.node_id, epoch,
            aggregation_package, signature,
            fog_node.tee
        )
    else:
        # Direct store without signature (node may have failed)
        cloud.store_aggregate(fog_node.node_id, epoch, aggregation_package)

    # Increment sequence for replay resistance
    fog_node.increment_sequence()

    # =========================================================================
    # Step 2: Performance Evaluation
    # Score_i(t) = ω_1·V_i(t) + ω_2·Rel_i(t+1)
    # where ω_1 + ω_2 = 1
    # "A larger value indicates higher completeness and stronger reliability"
    # =========================================================================
    completeness = agg_output['completeness']
    new_reliability = rec_output['new_reliability']

    score = (config.omega_score_1 * completeness +
             config.omega_score_2 * new_reliability)
    score = max(0.0, min(1.0, score))
    fog_node.quality_score = score

    # =========================================================================
    # Step 3: Historical Learning and Error Estimation
    #
    # Experiment Plan §7, Table 6:
    #   Hist_i(t+1) = γ · Hist_i(t) + (1-γ) · Score_i(t)
    #   Score_i*(t) = α_h · Hist_i(t+1) + (1-α_h) · Score_i(t)
    #
    # γ = gamma_decay (default 0.9) — history decay factor
    # α_h = alpha_hist (default 0.3) — history blend factor
    # These are SEPARATE parameters, not conflated.
    #
    # e_i(t) = κ_1·(S̄ - Score_i*) + κ_2·RU_i + κ_3·(1-Rel_i(t+1))
    # subject to κ_1 + κ_2 + κ_3 = 1
    # =========================================================================

    # Update historical performance profile
    hist = (config.gamma_decay * fog_node.history_score +
            (1.0 - config.gamma_decay) * score)
    fog_node.history_score = hist

    # Fused current-historical score (uses alpha_hist, not gamma_decay)
    score_star = config.alpha_hist * hist + (1.0 - config.alpha_hist) * score

    # Adaptive control error (uses kappa weights)
    ru = rec_output['recovery_urgency']
    error = (config.kappa_1 * (config.s_target - score_star) +
             config.kappa_2 * ru +
             config.kappa_3 * (1.0 - new_reliability))
    fog_node.control_error = error

    # =========================================================================
    # Step 4: Adaptive Threshold Optimization
    # τ_x(t+1) = Π_{[0,1]}(τ_x(t) + μ_x · e_i(t))
    # for x ∈ {v, r, 1, 2, 3, f}
    #
    # Π_{[0,1]}(y) = min{1, max(0, y}}  (projection operator)
    #
    # Enforce ordering: τ_1(t+1) < τ_2(t+1) < τ_3(t+1)
    #
    # "As error increases, thresholds become more sensitive"
    # "When conditions improve, thresholds gradually relax"
    # =========================================================================
    config.tau_v = _project(config.tau_v + config.mu_v * error)
    config.tau_r = _project(config.tau_r + config.mu_r * error)
    config.tau_1 = _project(config.tau_1 + config.mu_1 * error)
    config.tau_2 = _project(config.tau_2 + config.mu_2 * error)
    config.tau_3 = _project(config.tau_3 + config.mu_3 * error)
    config.tau_f = _project(config.tau_f + config.mu_f * error)

    # Enforce escalation ordering: τ_1 < τ_2 < τ_3
    _enforce_threshold_ordering(config)

    # =========================================================================
    # Step 5: Feedback Generation and Closed-Loop Adaptation
    # FB_i(t) = (Score_i, Hist_i(t+1), e_i, τ_v, τ_r, τ_1, τ_2, τ_3, τ_f)
    #
    # "Feedback state is supplied to prediction and recovery modules
    #  during the next aggregation epoch"
    # =========================================================================
    feedback = {
        'score': score,
        'history': hist,
        'score_star': score_star,
        'error': error,
        'tau_v': config.tau_v,
        'tau_r': config.tau_r,
        'tau_1': config.tau_1,
        'tau_2': config.tau_2,
        'tau_3': config.tau_3,
        'tau_f': config.tau_f,
        'final_aggregate': final_aggregate,
        'recovery_status': recovery_status
    }

    return feedback


def _project(value: float) -> float:
    """
    Projection operator Π_{[0,1]}(y) = min{1, max{0, y}}.

    Ensures all thresholds remain bounded within [0, 1].

    Paper: Phase V, Step 4 - Bounded optimization
           Theorem 8: "All AFLTO thresholds remain bounded"
    """
    return min(1.0, max(0.0, value))


def _enforce_threshold_ordering(config: SystemConfig):
    """
    Enforce recovery escalation ordering: τ_1 < τ_2 < τ_3.

    Uses config.threshold_margin (default 0.01 per plan §7.2).

    Paper: Phase V, Step 4
    "The ordering constraint τ_1(t+1) < τ_2(t+1) < τ_3(t+1) is
     enforced after each update cycle to preserve recovery-escalation
     consistency."

    Experiment Plan §7.2:
        tau1 = min(tau1, tau2 - margin)
        tau2 = max(tau2, tau1 + margin)
        tau2 = min(tau2, tau3 - margin)
        tau3 = max(tau3, tau2 + margin)
    """
    margin = config.threshold_margin

    # Clamp each to [0, 1] first
    config.tau_1 = _project(config.tau_1)
    config.tau_2 = _project(config.tau_2)
    config.tau_3 = _project(config.tau_3)

    # Enforce ordering with margin
    config.tau_1 = min(config.tau_1, config.tau_2 - margin)
    config.tau_2 = max(config.tau_2, config.tau_1 + margin)
    config.tau_2 = min(config.tau_2, config.tau_3 - margin)
    config.tau_3 = max(config.tau_3, config.tau_2 + margin)

    # Final clamp
    config.tau_1 = _project(config.tau_1)
    config.tau_2 = _project(config.tau_2)
    config.tau_3 = _project(config.tau_3)


def execute_static(config: SystemConfig, fog_node: FogNode,
                    agg_output: dict, rec_output: dict,
                    cloud: CloudServer, epoch: int) -> dict:
    """
    Execute AFLTO with static thresholds (for ablation study in Exp 7).

    Same as execute() but skips Step 4 (threshold optimization).

    Paper: Experiment 7 - "variant in which AFLTO is disabled and all
           thresholds remain static"
    """
    # Steps 1-3 and 5 same as normal
    incomplete_flag = agg_output['incomplete_flag']
    recovery_status = rec_output['recovery_status']

    if incomplete_flag == 0:
        final_aggregate = agg_output['fog_aggregate']
    elif incomplete_flag == 1 and recovery_status == 1:
        final_aggregate = rec_output.get('recovered_aggregate',
                                          agg_output['fog_aggregate'])
    else:
        final_aggregate = agg_output['fog_aggregate']

    aggregation_package = {
        'final_aggregate': str(final_aggregate),
        'mode': rec_output['mode'],
        'reliability': rec_output['new_reliability'],
        'recovery_status': recovery_status
    }
    cloud.store_aggregate(fog_node.node_id, epoch, aggregation_package)
    fog_node.increment_sequence()

    # Step 2: Performance evaluation
    completeness = agg_output['completeness']
    new_reliability = rec_output['new_reliability']
    score = (config.omega_score_1 * completeness +
             config.omega_score_2 * new_reliability)
    fog_node.quality_score = max(0.0, min(1.0, score))

    # Step 3: History (still track, using separated γ and α_h)
    hist = (config.gamma_decay * fog_node.history_score +
            (1.0 - config.gamma_decay) * score)
    fog_node.history_score = hist
    score_star = config.alpha_hist * hist + (1.0 - config.alpha_hist) * score
    ru = rec_output['recovery_urgency']
    error = (config.kappa_1 * (config.s_target - score_star) +
             config.kappa_2 * ru +
             config.kappa_3 * (1.0 - new_reliability))
    fog_node.control_error = error

    # Step 4: SKIPPED — thresholds remain static

    # Step 5: Output
    return {
        'score': score,
        'history': hist,
        'score_star': score_star,
        'error': error,
        'tau_v': config.tau_v,
        'tau_r': config.tau_r,
        'tau_1': config.tau_1,
        'tau_2': config.tau_2,
        'tau_3': config.tau_3,
        'tau_f': config.tau_f,
        'final_aggregate': final_aggregate,
        'recovery_status': recovery_status
    }
