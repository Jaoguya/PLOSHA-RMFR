"""
Phase IV: Risk-Aware Multi-Layer Fault Recovery (RMFR)
=======================================================
Preserves aggregation continuity under overload, incomplete aggregation,
communication disruption, and fog-node failure through progressive
recovery escalation.

Gramine-SGX Edition — recovery attempt actually calls tee_sign() and
verifies.  Uses renamed config fields (eta, kappa, etc.).

Paper Reference: Section III-C, Phase IV (7 Steps)
  Step 1: Recovery Urgency Evaluation
  Step 2: Recovery Escalation Decision
  Step 3: Recovery Candidate Selection
  Step 4: Layer-I Predictive Delegation Recovery
  Step 5: Layer-II Selective Micro-Slot Recovery
  Step 6: Layer-III Reliability-Aware Failover Recovery
  Step 7: Reliability Reinforcement and Recovery Output
"""

import time
import random
from typing import List, Dict, Optional

from src.config import SystemConfig
from src.entities.fog_node import FogNode
from src.entities.krm import KRM
from src.entities.sensor import Sensor
from src.crypto.tee import compute_hash


def execute(config: SystemConfig, fog_node: FogNode,
            all_fog_nodes: Dict[int, FogNode],
            agg_output: dict, krm: KRM,
            sensors: List[Sensor], epoch: int) -> dict:
    """
    Execute Phase IV: RMFR for a single fog node.

    Progressive recovery: Normal → Delegation → MicroRecovery → Failover

    Args:
        config: System configuration.
        fog_node: Fog node being evaluated.
        all_fog_nodes: All fog nodes by ID (for neighbor lookup).
        agg_output: Aggregation state from Phase III.
        krm: KRM module.
        sensors: All sensors.
        epoch: Current epoch number.

    Returns:
        Recovery state tuple Rec_i(t).
    """
    t_start = time.time()

    # =========================================================================
    # Step 1: Recovery Urgency Evaluation
    # RU_i(t) = ρ_1·Risk_i(t) + ρ_2·(1-V_i(t)) + ρ_3·(1-Rel_i(t))
    # where ρ_1 + ρ_2 + ρ_3 = 1
    # "A larger value indicates a higher probability of aggregation
    #  disruption and service degradation"
    # =========================================================================
    risk = agg_output['risk']
    completeness = agg_output['completeness']
    reliability = agg_output['reliability']
    incomplete_flag = agg_output['incomplete_flag']

    ru = (config.rho_1 * risk +
          config.rho_2 * (1.0 - completeness) +
          config.rho_3 * (1.0 - reliability))
    ru = max(0.0, min(1.0, ru))
    fog_node.recovery_urgency = ru

    # =========================================================================
    # Step 2: Recovery Escalation Decision
    # Mode_i(t):
    #   Normal:        Φ_i=0  ∧  RU < τ_1
    #   Delegation:    Φ_i=0  ∧  τ_1 ≤ RU < τ_2
    #   MicroRecovery: Φ_i=1  ∧  RU < τ_3
    #   Failover:      RU ≥ τ_3  ∨  Rel_i ≤ τ_f
    # Constraint: 0 ≤ τ_1 < τ_2 < τ_3 ≤ 1
    # =========================================================================
    mode = _determine_recovery_mode(config, incomplete_flag, ru, reliability)
    fog_node.recovery_mode = mode

    # =========================================================================
    # Step 3: Recovery Candidate Selection
    # When Mode ∈ {Delegation, Failover}:
    #   U_j(t) = α_c·Cap_j(t+1) + α_r·Rel_j(t) + α_k·(1-Risk_j(t))
    #   F_i* = argmax_{F_j ∈ N_i} U_j(t)
    # =========================================================================
    best_candidate = None
    if mode in ("Delegation", "Failover"):
        best_candidate = _select_recovery_candidate(
            config, fog_node, all_fog_nodes)

    recovery_success = 1  # Assume success unless recovery fails

    # =========================================================================
    # Step 4: Layer-I Predictive Delegation Recovery
    # When Mode = Delegation:
    #   DSP_i(t) = (m*, C_{agg,i}, Seq_i(t), Cap_i, Risk_i, Rel_i)
    #   "KRM securely seals and transmits DSP to attested TEE of F_i*"
    #   "Delegated fog node performs shadow aggregation"
    # =========================================================================
    if mode == "Delegation" and best_candidate is not None:
        _execute_delegation(config, fog_node, best_candidate,
                           all_fog_nodes, agg_output, krm, epoch)

    # =========================================================================
    # Step 5: Layer-II Selective Micro-Slot Recovery
    # When Mode = MicroRecovery:
    #   D_i^{miss} = {δ_k ∈ D_i : N_{δ_k}^{recv} < N_{δ_k}^{exp}}
    #   D_i^{valid} = D_i \ D_i^{miss}
    #   Reconstruct only missing micro-slots:
    #     C_{micro,k}^{rec} = Π_{j ∈ δ_k^{rec}} C_j
    #   Recovered aggregate:
    #     C_{agg,i}^{rec} = (Π valid) · (Π recovered)
    # =========================================================================
    recovered_aggregate = None
    if mode == "MicroRecovery":
        recovered_aggregate, recovery_success = _execute_micro_recovery(
            config, fog_node, agg_output)

    # =========================================================================
    # Step 6: Layer-III Reliability-Aware Failover Recovery
    # When Mode = Failover:
    #   F_i* = argmax_{F_j ∈ N_i} U_j(t)
    #   KRM provisions temporary credential to F_i* TEE
    #   Migrate FSM_i(t) = (m*, C_{agg,i}, D_i, Φ_i, Rel_i)
    #   Update Γ(S_j) ← F_i*, ∀ S_j: Γ(S_j) = F_i
    # =========================================================================
    if mode == "Failover" and best_candidate is not None:
        _execute_failover(config, fog_node, best_candidate,
                         all_fog_nodes, agg_output, krm,
                         sensors, epoch)

    # =========================================================================
    # Step 7: Reliability Reinforcement and Recovery Output
    #
    # Experiment Plan §6.1 & §4.4:
    # Succ_i(t) = 1 if recovered aggregate passes cloud verification
    #
    # Rel_i(t+1) = min{1, β_r·Rel_i(t) + (1-β_r)·[λ_s·Succ_i + λ_v·V_i]}
    # where λ_s + λ_v = 1
    #
    # Attempt actual sign + verify to determine Succ (§6.1)
    # =========================================================================
    recovery_success = _attempt_verify(fog_node, agg_output,
                                        recovered_aggregate,
                                        recovery_success)
    fog_node.recovery_success = recovery_success

    # Update reliability score — formula from config, no hardcoded weights
    new_reliability = min(1.0,
        config.beta_r * fog_node.reliability +
        (1.0 - config.beta_r) * (
            config.lambda_s * recovery_success +
            config.lambda_v * completeness
        )
    )
    fog_node.reliability = new_reliability

    # Update KRM reliability record
    krm.reliability_scores[fog_node.node_id] = new_reliability

    t_end = time.time()
    fog_node.epoch_recovery_time += (t_end - t_start)

    # Recovery output tuple
    rec_output = {
        'mode': mode,
        'recovery_urgency': ru,
        'best_candidate': best_candidate,
        'new_reliability': new_reliability,
        'recovery_status': recovery_success,
        'recovered_aggregate': recovered_aggregate
    }

    return rec_output


def _attempt_verify(fog_node: FogNode, agg_output: dict,
                    recovered_aggregate, prior_success: int) -> int:
    """
    Attempt actual sign + verify to determine Succ_i(t).

    Experiment Plan §6.1:
        sigma = tee_sign(C_final)
        if cloud.verify(C_final, sigma): return 1
        return 0

    We sign the final aggregate hash and verify it to confirm
    the enclave-to-cloud path works.  If the TEE is not attested
    (node failed), Succ = 0.
    """
    if not fog_node.tee.attested:
        return 0

    try:
        # Choose the final aggregate
        final = recovered_aggregate if recovered_aggregate is not None \
            else agg_output.get('fog_aggregate')
        if final is None:
            return 0

        # Sign and verify (§6.1)
        payload = compute_hash((str(final),))
        sigma = fog_node.tee.sign(payload)
        if fog_node.tee.verify_signature(payload, sigma):
            return prior_success  # Keep prior recovery outcome
        return 0
    except Exception:
        return 0


def _determine_recovery_mode(config: SystemConfig,
                              incomplete_flag: int,
                              ru: float,
                              reliability: float) -> str:
    """
    Determine recovery mode based on escalation thresholds.

    Paper: Phase IV, Step 2 - Recovery Escalation Decision
    """
    # Check Failover first (highest priority)
    if ru >= config.tau_3 or reliability <= config.tau_f:
        return "Failover"

    # Check MicroRecovery: aggregation is incomplete
    if incomplete_flag == 1 and ru < config.tau_3:
        return "MicroRecovery"

    # Check Delegation: complete but future risk predicted
    if incomplete_flag == 0 and config.tau_1 <= ru < config.tau_2:
        return "Delegation"

    # Normal operation
    return "Normal"


def _select_recovery_candidate(config: SystemConfig,
                                fog_node: FogNode,
                                all_fog_nodes: Dict[int, FogNode]) -> Optional[int]:
    """
    Select optimal recovery candidate from neighbors.

    F_i* = argmax_{F_j ∈ N_i} U_j(t)
    U_j(t) = α_c·Cap_j(t+1) + α_r·Rel_j(t) + α_k·(1-Risk_j(t))

    All weights from config — no hardcoded bias.

    Paper: Phase IV, Step 3 - Recovery Candidate Selection
    """
    best_utility = -1.0
    best_id = None

    for neighbor_id in fog_node.neighbors:
        neighbor = all_fog_nodes.get(neighbor_id)
        if neighbor is None or not neighbor.operational:
            continue

        # Compute utility score
        utility = (config.alpha_c * neighbor.cap +
                   config.alpha_r * neighbor.reliability +
                   config.alpha_k * (1.0 - neighbor.risk))

        if utility > best_utility:
            best_utility = utility
            best_id = neighbor_id

    return best_id


def _execute_delegation(config: SystemConfig,
                         fog_node: FogNode,
                         candidate_id: int,
                         all_fog_nodes: Dict[int, FogNode],
                         agg_output: dict,
                         krm: KRM,
                         epoch: int):
    """
    Execute Layer-I Predictive Delegation Recovery.

    DSP_i(t) = (m*, C_{agg,i}, Seq_i(t), Cap_i, Risk_i, Rel_i)
    "Proactive delegation mitigates overload before aggregation
     quality deteriorates"

    Paper: Phase IV, Step 4
    """
    candidate = all_fog_nodes.get(candidate_id)
    if candidate is None:
        return

    # Construct Delegation State Package
    dsp = {
        'optimal_m': agg_output['optimal_m'],
        'fog_aggregate': agg_output['fog_aggregate'],
        'sequence': fog_node.aggregation_seq,
        'capacity': agg_output['capacity'],
        'risk': agg_output['risk'],
        'reliability': agg_output['reliability']
    }

    # Issue delegation credential
    krm.issue_delegation_credential(candidate, epoch, epoch + 5)

    # Communication overhead: DSP transmission
    fog_node.epoch_comm_bytes += config.dsp_size
    fog_node.epoch_recovery_time += config.t_sch


def _execute_micro_recovery(config: SystemConfig,
                             fog_node: FogNode,
                             agg_output: dict) -> tuple:
    """
    Execute Layer-II Selective Micro-Slot Recovery.

    D_i^{miss} = {δ_k ∈ D_i : N_{δ_k}^{recv} < N_{δ_k}^{exp}}
    Reconstruct only missing micro-slots.
    C_{agg,i}^{rec} = (Π valid)(Π recovered)

    Paper: Phase IV, Step 5
    "Since only incomplete micro-slots are recomputed, RMFR avoids
     full-epoch reaggregation"
    """
    slot_data = agg_output.get('slot_data', [])
    micro_slots = agg_output.get('micro_slots', [])
    micro_aggregates = agg_output.get('micro_aggregates', [])

    if not slot_data:
        return None, 0

    # Identify incomplete micro-slots
    # D_i^{miss} = {δ_k : N_{δ_k}^{recv} < N_{δ_k}^{exp}}
    miss_indices = []
    valid_indices = []
    for k, (slot, slot_sensors) in enumerate(zip(slot_data, micro_slots)):
        expected = len(slot_sensors)
        received = slot.get('count', 0)
        if expected > 0 and received < expected:
            miss_indices.append(k)
        else:
            valid_indices.append(k)

    if not miss_indices:
        # No recovery needed
        return agg_output['fog_aggregate'], 1

    # Recover missing micro-slots
    # Simulate retransmission / local buffer reconstruction
    recovery_success = 1
    for k in miss_indices:
        slot_sensors = micro_slots[k] if k < len(micro_slots) else []

        # Attempt recovery: simulate retransmission
        # Success probability driven by (1 - sensor_drop_rate)
        recovered_count = 0
        for sid in slot_sensors:
            if random.random() > config.sensor_drop_rate:
                recovered_count += 1

        # Timing: recovery overhead per recovered ciphertext
        fog_node.epoch_recovery_time += recovered_count * config.t_padd
        fog_node.epoch_comm_bytes += recovered_count * config.ct_aes_size

        if recovered_count == 0 and len(slot_sensors) > 0:
            recovery_success = 0

    # Rebuild aggregate (simulated)
    # C_{agg,i}^{rec} = (Π valid)(Π recovered)
    recovered_aggregate = agg_output['fog_aggregate']

    return recovered_aggregate, recovery_success


def _execute_failover(config: SystemConfig,
                       fog_node: FogNode,
                       candidate_id: int,
                       all_fog_nodes: Dict[int, FogNode],
                       agg_output: dict,
                       krm: KRM,
                       sensors: List[Sensor],
                       epoch: int):
    """
    Execute Layer-III Reliability-Aware Failover Recovery.

    F_i* = argmax_{F_j ∈ N_i} U_j(t)
    Migrate FSM_i(t) = (m*, C_{agg,i}, D_i, Φ_i, Rel_i)
    Update Γ(S_j) ← F_i*, ∀ S_j: Γ(S_j) = F_i

    Paper: Phase IV, Step 6
    """
    candidate = all_fog_nodes.get(candidate_id)
    if candidate is None:
        return

    # Construct Failover State Migration package
    fsm = {
        'optimal_m': agg_output['optimal_m'],
        'fog_aggregate': agg_output['fog_aggregate'],
        'micro_slots': agg_output['micro_slots'],
        'incomplete_flag': agg_output['incomplete_flag'],
        'reliability': agg_output['reliability']
    }

    # Issue delegation credential to candidate
    krm.issue_delegation_credential(candidate, epoch, epoch + 10)

    # Update sensor assignments: Γ(S_j) ← F_i* for all S_j assigned to F_i
    sensors_to_reassign = list(fog_node.assigned_sensors)
    for sid in sensors_to_reassign:
        krm.update_sensor_assignment(
            sid, candidate_id, sensors, all_fog_nodes)

    # Communication overhead: FSM transmission
    fog_node.epoch_comm_bytes += config.fsm_size
    fog_node.epoch_recovery_time += config.t_sch * 2  # Extra scheduling

    # Mark original fog node as failed
    fog_node.fail()
