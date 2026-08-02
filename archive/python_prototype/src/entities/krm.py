"""
Key and Reliability Management (KRM) Module
=============================================
Trusted management entity responsible for key provisioning, remote attestation,
reliability-state management, and delegation authorization.

Paper Reference:
  - System Model: "The KRM manages key provisioning, reliability-state
    maintenance, remote attestation, and secure delegation among fog nodes."
  - Phase I: All 5 initialization steps
  - Theorem 2: Compartmentalized Key Exposure
  - Theorem 4: Secure Delegation Authenticity
"""

import random
from typing import Dict, List, Tuple, Optional

from src.config import SystemConfig
from src.crypto import aes_gcm, paillier
from src.entities.sensor import Sensor
from src.entities.fog_node import FogNode


class KRM:
    """
    Key and Reliability Management Module.

    Trusted entity that:
    - Provisions fog-scoped AES-GCM keys (k_i)
    - Generates Paillier key pair (pk_P, sk_P)
    - Performs remote attestation
    - Maintains reliability state
    - Authorizes delegation
    """

    def __init__(self, config: SystemConfig):
        """
        Initialize KRM.

        Args:
            config: System configuration parameters.
        """
        self.config = config

        # Paillier key pair (Phase I, Step 3)
        self.paillier_pk = None
        self.paillier_sk = None  # Private key sealed within KRM

        # Fog-scoped AES keys: fog_id → k_i (Phase I, Step 2)
        self.fog_keys: Dict[int, bytes] = {}

        # Sensor-to-fog mapping: Γ(S_j) = F_i
        self.sensor_assignment: Dict[int, int] = {}

        # Active sensor registry: Act_j(t) (Phase III, Step 6)
        self.active_sensors: Dict[int, bool] = {}

        # Reliability state: {fog_id: Rel_i(t)}
        self.reliability_scores: Dict[int, float] = {}

        # Delegation credentials
        self.delegation_credentials: Dict[int, dict] = {}

        # Attestation status
        self.attested_nodes: set = set()

    def generate_paillier_keys(self):
        """
        Generate Paillier key pair (pk_P, sk_P).

        Paper: Phase I, Step 3 - KRM generates using security parameter λ.
               pk_P provisioned to all attested fog enclaves.
               sk_P remains sealed within the KRM.
        """
        self.paillier_pk, self.paillier_sk = paillier.keygen(
            self.config.paillier_key_size
        )

    def attest_fog_node(self, fog_node: FogNode) -> bool:
        """
        Perform remote attestation of a fog node.

        Paper: Phase I, Step 1 - Attest(F_i) = 1
               "A fog node is admitted only if Attest(F_i) = 1"

        Args:
            fog_node: Fog node to attest.

        Returns:
            True if attestation succeeds.
        """
        success = fog_node.tee.attest()
        if success:
            self.attested_nodes.add(fog_node.node_id)
        return success

    def provision_fog_key(self, fog_node: FogNode) -> bytes:
        """
        Generate and provision a fog-scoped AES-GCM key to a fog node's TEE.

        Paper: Phase I, Step 2 - "KRM generates a unique fog-scoped AES-GCM
               key k_i and securely provisions it to the enclave of F_i"
               Theorem 2: Each fog node maintains independent cryptographic domain

        Args:
            fog_node: Attested fog node to provision.

        Returns:
            The generated AES key k_i.
        """
        if fog_node.node_id not in self.attested_nodes:
            raise RuntimeError(f"Fog node {fog_node.node_id} not attested")

        # Generate unique fog-scoped key
        key = aes_gcm.generate_key(self.config.aes_key_size)
        self.fog_keys[fog_node.node_id] = key

        # Provision to TEE enclave
        fog_node.tee.provision_keys(key, self.paillier_pk)

        return key

    def assign_sensors(self, sensors: List[Sensor], fog_nodes: List[FogNode]):
        """
        Initialize sensor-to-fog assignment function Γ: S → F.

        Distributes sensors across fog nodes approximately uniformly.

        Paper: Phase I, Step 2 - "Γ(S_j) = F_i indicates that sensor S_j
               is assigned to fog node F_i"

        Args:
            sensors: List of all sensors.
            fog_nodes: List of all fog nodes.
        """
        num_fogs = len(fog_nodes)
        for i, sensor in enumerate(sensors):
            fog_idx = i % num_fogs
            fog_node = fog_nodes[fog_idx]

            # Assign sensor to fog node
            sensor.assigned_fog_id = fog_node.node_id
            self.sensor_assignment[sensor.sensor_id] = fog_node.node_id

            # Provision sensor with fog-scoped key
            fog_key = self.fog_keys.get(fog_node.node_id)
            if fog_key:
                sensor.aes_key = fog_key

            # Add to fog node's sensor list
            if sensor.sensor_id not in fog_node.assigned_sensors:
                fog_node.assigned_sensors.append(sensor.sensor_id)

            # Track active status
            self.active_sensors[sensor.sensor_id] = sensor.active

    def initialize_reliability(self, fog_nodes: List[FogNode]):
        """
        Initialize reliability scores for all fog nodes.

        Rel_i(0) = 1 (fully operational at deployment time)

        Paper: Phase I, Step 4 - "KRM initializes the reliability score as
               Rel_i(0) = 1"

        Args:
            fog_nodes: List of all fog nodes.
        """
        for fog in fog_nodes:
            fog.reliability = 1.0
            self.reliability_scores[fog.node_id] = 1.0

    def get_expected_reports(self, fog_node_id: int) -> int:
        """
        Get expected number of active sensor reports for a fog node.

        N_exp(t) = Σ_{S_j: Γ(S_j)=F_i} Act_j(t)

        Paper: Phase III, Step 6 - Active sensor registry maintained by KRM

        Args:
            fog_node_id: Fog node ID.

        Returns:
            Number of active sensors assigned to this fog node.
        """
        count = 0
        for sensor_id, fog_id in self.sensor_assignment.items():
            if fog_id == fog_node_id and self.active_sensors.get(sensor_id, False):
                count += 1
        return count

    def update_active_sensors(self, sensors: List[Sensor]):
        """Update the active sensor registry."""
        for sensor in sensors:
            self.active_sensors[sensor.sensor_id] = sensor.active

    def issue_delegation_credential(self, fog_node: FogNode,
                                     epoch: int, expiry: int) -> dict:
        """
        Issue a temporary delegation credential to an attested fog node.

        Cred_i^{del} = GenCred(F_i*, Epoch, Expiry)

        Paper: Phase IV, Step 6 (Failover) and Theorem 4
               "KRM generates a temporary delegation credential"

        Args:
            fog_node: Target fog node (must be attested).
            epoch: Current aggregation epoch.
            expiry: Credential expiry epoch.

        Returns:
            Delegation credential dictionary.
        """
        if fog_node.node_id not in self.attested_nodes:
            raise RuntimeError(f"Cannot issue credential: node {fog_node.node_id} not attested")

        credential = {
            'node_id': fog_node.node_id,
            'epoch': epoch,
            'expiry': expiry,
            'valid': True
        }
        self.delegation_credentials[fog_node.node_id] = credential
        return credential

    def verify_delegation_credential(self, credential: dict) -> bool:
        """
        Verify a delegation credential.

        Verify(Cred_i^{del}) = 1

        Paper: Theorem 4 - "Before accepting a delegated aggregation state,
               the receiving fog node must verify Verify(Cred_i^{del}) = 1"
        """
        if not credential.get('valid', False):
            return False
        stored = self.delegation_credentials.get(credential.get('node_id'))
        if stored is None:
            return False
        return stored == credential

    def update_sensor_assignment(self, sensor_id: int, new_fog_id: int,
                                  sensors: List[Sensor],
                                  fog_nodes: Dict[int, FogNode]):
        """
        Update sensor-to-fog mapping during failover.

        Γ(S_j) ← F_i*, ∀ S_j: Γ(S_j) = F_i

        Paper: Phase IV, Step 6 - Sensor reassignment after failover

        Args:
            sensor_id: Sensor to reassign.
            new_fog_id: New fog node ID.
            sensors: All sensors.
            fog_nodes: All fog nodes by ID.
        """
        old_fog_id = self.sensor_assignment.get(sensor_id)
        self.sensor_assignment[sensor_id] = new_fog_id

        # Update sensor object
        for s in sensors:
            if s.sensor_id == sensor_id:
                s.assigned_fog_id = new_fog_id
                new_key = self.fog_keys.get(new_fog_id)
                if new_key:
                    s.aes_key = new_key
                break

        # Update fog node sensor lists
        if old_fog_id is not None and old_fog_id in fog_nodes:
            old_fog = fog_nodes[old_fog_id]
            if sensor_id in old_fog.assigned_sensors:
                old_fog.assigned_sensors.remove(sensor_id)
        if new_fog_id in fog_nodes:
            new_fog = fog_nodes[new_fog_id]
            if sensor_id not in new_fog.assigned_sensors:
                new_fog.assigned_sensors.append(sensor_id)

    def get_initialization_output(self) -> dict:
        """
        Get the complete initialization output.

        O_init = {pk_P, Γ, {k_i}_{i=1}^n, {Rel_i(0)}_{i=1}^n, {State_i(0)}_{i=1}^n}

        Paper: Phase I, Step 5 - Initialization Output
        """
        return {
            'paillier_pk': self.paillier_pk,
            'sensor_assignment': dict(self.sensor_assignment),
            'fog_keys': {k: '***' for k in self.fog_keys},  # Redacted
            'reliability_scores': dict(self.reliability_scores),
            'attested_nodes': set(self.attested_nodes)
        }
