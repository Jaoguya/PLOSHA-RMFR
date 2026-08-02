"""
Phase III: Predictive Adaptive Hierarchical Slot Aggregation (PLOSHA)
======================================================================
Performs privacy-preserving aggregation of sensor readings while
dynamically adapting aggregation structure according to predicted
operating conditions.

Gramine-SGX Edition — m* objective uses Cap/FE/Rel interaction terms
per Experiment Plan §5.1.  Sensor readings are integers.

Paper Reference: Section III-C, Phase III (7 Steps)
  Step 1: Prediction-Driven Aggregation Planning (optimal m*)
  Step 2: Adaptive Epoch Partitioning
  Step 3: Secure Data Collection and Ciphertext Transformation
  Step 4: Micro-Slot Aggregation
  Step 5: Hierarchical Fog Aggregation
  Step 6: Aggregation Completeness Assessment
  Step 7: Aggregation Output
"""

import random
import time
from typing import List, Dict

from src.config import SystemConfig
from src.entities.sensor import Sensor
from src.entities.fog_node import FogNode
from src.entities.krm import KRM
from src.crypto import paillier


def execute(config: SystemConfig, fog_node: FogNode,
            sensors: List[Sensor], krm: KRM,
            use_real_crypto: bool = False) -> dict:
    """
    Execute Phase III: PLOSHA for a single fog node.

    Performs adaptive micro-slot aggregation over encrypted sensor data.

    Args:
        config: System configuration.
        fog_node: The fog node performing aggregation.
        sensors: Sensors assigned to this fog node.
        krm: KRM for expected report counts.
        use_real_crypto: If True, use actual Paillier operations.
                         If False, use simulated aggregation for speed.

    Returns:
        Aggregation state tuple Agg_i(t).
    """
    if not fog_node.operational:
        return _create_failed_aggregation(fog_node)

    t_start = time.time()

    # =========================================================================
    # Step 1: Prediction-Driven Aggregation Planning
    # Receive Pred_i(t) = [Cap_i(t+1), FE_i(t), Risk_i(t)]
    #
    # Experiment Plan §5.1:
    # m* = argmin_{1≤m≤m_max} [
    #   λ_1 · β_t · m · (1 - Cap) +
    #   λ_2 · FE · (1/m) +
    #   λ_3 · (1 - Rel) · (1/m)
    # ]
    # =========================================================================
    cap = fog_node.cap
    fe = fog_node.failure_exposure
    risk = fog_node.risk
    rel = fog_node.reliability

    m_star = _compute_optimal_microslots(config, cap, fe, rel)
    fog_node.optimal_m = m_star

    # Accumulate simulated timing
    fog_node.epoch_agg_time += config.t_pred + config.t_risk

    # =========================================================================
    # Step 2: Adaptive Epoch Partitioning
    # D_i = {δ_1, δ_2, ..., δ_{m*}}
    # "Each micro-slot δ_k represents a temporal aggregation interval"
    # =========================================================================
    active_sensors = [s for s in sensors if s.active]
    micro_slots = _partition_into_microslots(active_sensors, m_star)
    fog_node.micro_slots = micro_slots

    # =========================================================================
    # Step 3: Secure Data Collection and Ciphertext Transformation
    # For each sensor S_j:
    #   CT_j = Enc_{k_i}(d_j)         — AES-GCM at sensor (integer d_j)
    #   TEE: d_j = Dec_{k_i}(CT_j)    — Decrypt inside enclave
    #   TEE: C_j = Enc_{pk_P}(d_j)    — Paillier encrypt inside enclave
    #   Erase d_j                       — Plaintext transient only
    # =========================================================================
    if use_real_crypto:
        slot_data = _collect_and_transform_real(config, fog_node,
                                                 micro_slots, sensors)
    else:
        slot_data = _collect_and_transform_simulated(config, fog_node,
                                                      micro_slots, sensors)

    # =========================================================================
    # Step 4: Micro-Slot Aggregation
    # For each δ_k ∈ D_i:
    #   C_{micro,k} = Π_{j∈δ_k} C_j
    # By additive homomorphic property:
    #   Dec(C_{micro,k}) = Σ_{j∈δ_k} d_j
    # =========================================================================
    micro_aggregates = []
    for k, slot in enumerate(slot_data):
        if use_real_crypto and slot['ciphertexts']:
            micro_agg = paillier.aggregate_ciphertexts(slot['ciphertexts'])
            micro_aggregates.append(micro_agg)
        else:
            # Simulated: just sum the plaintext values
            micro_aggregates.append(slot['sum'])

        # Timing: one T_PAdd per ciphertext pair in the slot
        num_adds = max(0, len(slot.get('ciphertexts', slot.get('values', []))) - 1)
        fog_node.epoch_agg_time += num_adds * config.t_padd

    fog_node.micro_aggregates = micro_aggregates

    # =========================================================================
    # Step 5: Hierarchical Fog Aggregation
    # C_{agg,i} = Π_{k=1}^{m*} C_{micro,k}
    # Dec(C_{agg,i}) = Σ_{k=1}^{m*} Σ_{j∈δ_k} d_j
    # =========================================================================
    if use_real_crypto:
        valid_micros = [m for m in micro_aggregates if m is not None]
        if valid_micros:
            fog_aggregate = paillier.aggregate_ciphertexts(valid_micros)
        else:
            fog_aggregate = None
    else:
        fog_aggregate = sum(m for m in micro_aggregates if m is not None)

    fog_node.fog_aggregate = fog_aggregate

    # Timing for hierarchical aggregation
    fog_node.epoch_agg_time += max(0, m_star - 1) * config.t_padd

    # =========================================================================
    # Step 6: Aggregation Completeness Assessment
    # V_i(t) = N_recv / N_exp
    # N_exp(t) = Σ_{S_j: Γ(S_j)=F_i} Act_j(t)
    #
    # Φ_i(t) = 1 if V_i(t) < τ_v  (incomplete)
    #         = 0 otherwise         (complete)
    # =========================================================================
    n_expected = krm.get_expected_reports(fog_node.node_id)
    n_received = sum(len(slot.get('values', slot.get('ciphertexts', [])))
                     for slot in slot_data)

    if n_expected > 0:
        completeness = n_received / n_expected
    else:
        completeness = 1.0
    completeness = min(1.0, completeness)
    fog_node.completeness = completeness

    # Completeness indicator
    if completeness < config.tau_v:
        incomplete_flag = 1  # Recovery needed
    else:
        incomplete_flag = 0  # Complete
    fog_node.incomplete_flag = incomplete_flag

    # Communication overhead: sensor reports received
    fog_node.epoch_comm_bytes += n_received * config.ct_aes_size
    # Micro-slot metadata
    fog_node.epoch_comm_bytes += m_star * config.meta_size

    t_end = time.time()
    fog_node.epoch_agg_time += (t_end - t_start)  # Wall-clock component

    # =========================================================================
    # Step 7: Aggregation Output
    # Agg_i(t) = (C_{agg,i}, m*, V_i(t), Φ_i(t), Cap_i(t+1), Risk_i(t), Rel_i(t))
    # "Forwarded to RMFR for recovery decision making"
    # =========================================================================
    agg_output = {
        'fog_aggregate': fog_aggregate,
        'optimal_m': m_star,
        'completeness': completeness,
        'incomplete_flag': incomplete_flag,
        'capacity': cap,
        'risk': risk,
        'reliability': rel,
        'n_received': n_received,
        'n_expected': n_expected,
        'micro_aggregates': micro_aggregates,
        'micro_slots': micro_slots,
        'slot_data': slot_data
    }

    return agg_output


def _compute_optimal_microslots(config: SystemConfig,
                                 cap: float, fe: float,
                                 rel: float) -> int:
    """
    Compute optimal micro-slot number m*.

    Experiment Plan §5.1:
        T_agg = β_t · m
        L_agg = 1/m
        obj = λ_1 · T_agg · (1 - Cap)
            + λ_2 · FE   · L_agg
            + λ_3 · (1 - Rel) · L_agg

    All weights (λ_1, λ_2, λ_3) and β_t come from config.
    m* is deterministic given the same prediction inputs.

    Paper: Phase III, Step 1
    - T_proc(m) = β_t · m  (processing overhead, linear in m)
    - L_agg(m) = 1/m        (loss exposure, decreasing in m)
    - "larger failure exposure or lower reliability naturally increases
       the number of micro-slots"
    """
    best_m = 1
    best_cost = float('inf')

    for m in range(1, config.m_max + 1):
        # Processing overhead: linear in m
        t_agg = config.beta_t * m

        # Aggregation-loss exposure: inversely proportional to m
        l_agg = 1.0 / m

        # Objective: weighted sum with Cap/FE/Rel interaction terms
        cost = (config.lambda_m1 * t_agg * (1.0 - cap) +
                config.lambda_m2 * fe * l_agg +
                config.lambda_m3 * (1.0 - rel) * l_agg)

        if cost < best_cost:
            best_cost = cost
            best_m = m

    return best_m


def _partition_into_microslots(sensors: List[Sensor],
                                m_star: int) -> List[List[int]]:
    """
    Partition sensors into m* micro-slots.

    D_i = {δ_1, δ_2, ..., δ_{m*}}

    Paper: Phase III, Step 2 - Adaptive Epoch Partitioning
    "Under stable conditions, fewer micro-slots reduce overhead.
     Under adverse conditions, additional micro-slots improve fault localization."
    """
    sensor_ids = [s.sensor_id for s in sensors]
    m = max(1, min(m_star, len(sensor_ids)))

    # Distribute sensors approximately uniformly across micro-slots
    slots = [[] for _ in range(m)]
    for i, sid in enumerate(sensor_ids):
        slots[i % m].append(sid)

    # If m_star > number of sensors, pad with empty slots
    while len(slots) < m_star:
        slots.append([])

    return slots


def _collect_and_transform_simulated(config: SystemConfig,
                                      fog_node: FogNode,
                                      micro_slots: List[List[int]],
                                      sensors: List[Sensor]) -> List[dict]:
    """
    Simulated data collection and transformation (no real crypto).

    Generates random integer sensor values in [0, max_sensor_value]
    and computes sums directly. Uses timing model for latency estimation.
    No hardcoded bias — sensor values are uniform random integers.

    Paper: Phase III, Step 3
    """
    sensor_map = {s.sensor_id: s for s in sensors}
    
    slot_data = []
    for slot_sensors in micro_slots:
        values = []
        for sid in slot_sensors:
            sensor = sensor_map.get(sid)
            if sensor and random.random() > config.sensor_drop_rate:
                value = sensor.generate_reading()
                values.append(value)
                # Timing: AES encrypt + TEE transform + Paillier encrypt
                fog_node.epoch_agg_time += (config.t_aes + config.t_tee +
                                            config.t_penc)

        slot_data.append({
            'values': values,
            'sum': sum(values) if values else 0,
            'count': len(values)
        })

    return slot_data


def _collect_and_transform_real(config: SystemConfig,
                                 fog_node: FogNode,
                                 micro_slots: List[List[int]],
                                 sensors: List[Sensor]) -> List[dict]:
    """
    Real cryptographic data collection and transformation.

    Uses actual AES-GCM and Paillier operations through the TEE enclave.

    Paper: Phase III, Step 3
    CT_j = Enc_{k_i}(d_j)   →   TEE: d_j = Dec_{k_i}(CT_j)
                              →   TEE: C_j = Enc_{pk_P}(d_j)
    """
    from src.crypto import aes_gcm

    slot_data = []
    aes_key = fog_node.tee._aes_key
    sensor_map = {s.sensor_id: s for s in sensors}

    for slot_sensors in micro_slots:
        ciphertexts = []
        values = []
        for sid in slot_sensors:
            sensor = sensor_map.get(sid)
            if sensor and random.random() > config.sensor_drop_rate:
                # Make sure sensor has the key
                if not sensor.aes_key:
                    sensor.aes_key = aes_key
                
                value, ct_aes = sensor.sense_and_encrypt()

                # TEE ciphertext transformation
                ct_paillier, plain = fog_node.tee.transform_ciphertext(ct_aes)
                ciphertexts.append(ct_paillier)
                values.append(plain)

        slot_data.append({
            'ciphertexts': ciphertexts,
            'values': values,
            'sum': sum(values) if values else 0,
            'count': len(values)
        })

    return slot_data


def _create_failed_aggregation(fog_node: FogNode) -> dict:
    """Create a failed aggregation output for non-operational fog nodes."""
    fog_node.completeness = 0.0
    fog_node.incomplete_flag = 1
    return {
        'fog_aggregate': None,
        'optimal_m': 1,
        'completeness': 0.0,
        'incomplete_flag': 1,
        'capacity': 0.0,
        'risk': 1.0,
        'reliability': fog_node.reliability,
        'n_received': 0,
        'n_expected': 0,
        'micro_aggregates': [],
        'micro_slots': [],
        'slot_data': []
    }
