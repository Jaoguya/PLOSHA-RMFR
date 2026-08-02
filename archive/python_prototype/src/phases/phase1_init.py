"""
Phase I: System Initialization and Secure Provisioning
========================================================
Establishes trust relationships, cryptographic credentials, and
operational states required by the PLOSHA-RMFR framework.

Paper Reference: Section III-C, Phase I (5 Steps)
  Step 1: Entity Registration and Attestation
  Step 2: Sensor Association and Key Provisioning
  Step 3: Homomorphic Aggregation Setup
  Step 4: Reliability and Operational-State Initialization
  Step 5: Initialization Output
"""

from typing import List, Dict, Tuple

from src.config import SystemConfig
from src.entities.sensor import Sensor
from src.entities.fog_node import FogNode
from src.entities.krm import KRM
from src.entities.cloud_server import CloudServer


def execute(config: SystemConfig, sensors: List[Sensor],
            fog_nodes: List[FogNode], krm: KRM,
            cloud: CloudServer) -> dict:
    """
    Execute Phase I: System Initialization and Secure Provisioning.

    Performs all 5 initialization steps to produce O_init.

    Args:
        config: System configuration.
        sensors: List of IIoT sensors S = {S_1, ..., S_m}.
        fog_nodes: List of fog nodes F = {F_1, ..., F_n}.
        krm: Key and Reliability Management module.
        cloud: Cloud server.

    Returns:
        Initialization output O_init = {pk_P, Γ, {k_i}, {Rel_i(0)}, {State_i(0)}}
    """

    # =========================================================================
    # Step 1: Entity Registration and Attestation
    # "Each fog node F_i performs remote attestation with KRM.
    #  A fog node is admitted only if Attest(F_i) = 1."
    # =========================================================================
    for fog in fog_nodes:
        success = krm.attest_fog_node(fog)
        if not success:
            raise RuntimeError(
                f"Attestation failed for fog node {fog.node_id}")

    # =========================================================================
    # Step 2: Sensor Association and Key Provisioning
    # "KRM initializes sensor-to-fog assignment function Γ: S → F"
    # "KRM generates a unique fog-scoped AES-GCM key k_i"
    # =========================================================================

    # Step 3 first (need Paillier keys before provisioning to enclaves)
    # Step 3: Homomorphic Aggregation Setup
    # "KRM generates a Paillier key pair (pk_P, sk_P) using security parameter λ"
    # "pk_P is provisioned to all attested fog enclaves"
    # "sk_P remains sealed within the KRM and is never disclosed to fog nodes"
    krm.generate_paillier_keys()

    # Now provision fog-scoped keys (includes Paillier pk to enclaves)
    for fog in fog_nodes:
        krm.provision_fog_key(fog)

    # Set up neighbor relationships for each fog node
    # Each fog node knows about all other fog nodes as potential neighbors
    fog_ids = [f.node_id for f in fog_nodes]
    for fog in fog_nodes:
        fog.neighbors = [fid for fid in fog_ids if fid != fog.node_id]

    # Assign sensors to fog nodes and provision keys
    krm.assign_sensors(sensors, fog_nodes)

    # =========================================================================
    # Step 4: Reliability and Operational-State Initialization
    # "Rel_i(0) = 1" - fully operational at deployment
    # "State_i(0) = [W_i(0), Q_i(0), L_i(0), Rel_i(0)]"
    # =========================================================================
    krm.initialize_reliability(fog_nodes)

    # Initialize operational state vectors
    for fog in fog_nodes:
        fog.workload = 0.0      # W_i(0)
        fog.queue_util = 0.0    # Q_i(0)
        fog.latency = 0.0       # L_i(0)
        # Rel_i(0) already set to 1.0 by KRM

        # Initialize EWMA predictions to initial state
        fog.pred_workload = 0.0
        fog.pred_queue = 0.0
        fog.pred_latency = 0.0
        fog.pred_reliability = 1.0

        # Initialize capacity, risk, AFLTO state
        fog.cap = 1.0
        fog.failure_exposure = 0.0
        fog.risk = 0.0
        fog.status = "Normal"
        fog.quality_score = 1.0
        fog.history_score = 1.0
        fog.control_error = 0.0

    # =========================================================================
    # Step 5: Initialization Output
    # O_init = {pk_P, Γ, {k_i}_{i=1}^n, {Rel_i(0)}_{i=1}^n, {State_i(0)}_{i=1}^n}
    # =========================================================================
    init_output = krm.get_initialization_output()

    # Add initial state vectors
    init_output['initial_states'] = {}
    for fog in fog_nodes:
        init_output['initial_states'][fog.node_id] = fog.get_state_vector()

    return init_output
