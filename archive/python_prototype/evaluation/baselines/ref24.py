"""
Baseline: PPDA - Robust Privacy-Preserving Data Aggregation (Ref [24])
========================================================================
A Robust Privacy-Preserving Data Aggregation Scheme for Edge-Supported IIoT.

Paper: Shang et al., "A Robust Privacy-Preserving Data Aggregation Scheme
       for Edge-Supported IIoT", IEEE Trans. Industrial Informatics, 2024.

Algorithm Summary (from the paper):
  1. System Init: CC generates Paillier keys (N,g) and ECDSA params on
     elliptic curve Gq(a,b) with generator G of order n (Sec IV-A).
  2. Registration: ESs and Ss register with CC via ECDSA signatures
     δ = sig(d, H1(ID||T)), mutual authentication (Sec IV-B).
  3. Data Generation: Sensor Sij encrypts reading m_ij as
     c_ij = g^{m_ij} · R_ij^N mod N² (Paillier encryption) and signs
     δ_ij = sig(d_Sij, H1(ID||c||T)) (Sec IV-C.2).
  4. Aggregation at ES: Batch-verify all t signatures using modified ECDSA:
     Σ H(c)s⁻¹ · G + Σ rs⁻¹ · Q = Σ P  (Sec II-C, Sec V-A.1)
     Then homomorphic aggregation: ci = g^{Σmij} · (∏Rij)^N (Sec IV-C.3).
  5. Differential Privacy: ESi adds Laplace noise ĉi = ci · g^{m̂i}
     (Sec IV-C.3).
  6. Decryption at CC: Batch-verify ES signatures, then decrypt
     M̂i = L(ĉi^λ mod N²) · μ mod N (Sec IV-C.4).

Computation Cost (Table III of PLOSHA-RMFR paper):
  Prediction:   None
  Scheduling:   None
  Aggregation:  N_s × (T_PEnc + T_Sig + T_BVer + T_PAdd)
  Recovery:     None

Communication Cost (Table IV):
  Data Collection:  N_s × (|CT_P| + |Sig|)
  Coordination:     N_s × |Sig|
  Recovery:         None

Key behavioral characteristics:
  - Pure privacy-preserving aggregation (Paillier + ECDSA batch verification)
  - Flat aggregation window — no micro-slot partitioning
  - No prediction, no scheduling, no recovery
  - Steepest latency increase under high sensor counts (heavy crypto per report)
  - Every sensor performs full Paillier encryption + ECDSA signature
  - Loss exposure = 1.0 (entire epoch at risk from single failure)
"""

import numpy as np
from evaluation.baselines.base import BaselineScheme
from src.config import SystemConfig


class Ref24Scheme(BaselineScheme):
    """PPDA: Robust Privacy-Preserving Data Aggregation for IIoT."""

    name = "Ref. [24]"

    # ---- Calibrated timing parameters (from Ref[24] paper, Sec VI) ----
    # Paillier key size: 1024-bit (N = p0·q0, |p0|=|q0|=512 bits)
    # ECDSA on secp256k1 curve

    # Ref[24] Table IV reports computation costs in ms:
    # Sensor side: Paillier Enc ≈ 5.2ms, ECDSA Sign ≈ 1.1ms
    # ES side: Batch Verify (t sigs) ≈ 0.4ms + 0.3ms×t, Paillier Add ≈ 0.1ms
    # CC side: Paillier Dec ≈ 5.5ms

    T_PAILLIER_ENC = 0.0052      # Paillier encryption per sensor reading
    T_ECDSA_SIGN = 0.0011        # ECDSA signature generation per sensor
    T_BATCH_VERIFY_BASE = 0.0004 # Batch verify base cost
    T_BATCH_VERIFY_PER = 0.0003  # Batch verify per-signature cost
    T_PAILLIER_ADD = 0.0001      # Homomorphic addition per ciphertext
    T_PAILLIER_DEC = 0.0055      # Paillier decryption (at CC)
    T_LAPLACE_NOISE = 0.00005    # Laplace noise generation for DP

    # Communication sizes (from Ref[24] paper)
    CT_PAILLIER_SIZE = 256       # |CT_P|: Paillier ciphertext (bytes), 2048-bit = 256B
    ECDSA_SIG_SIZE = 72          # |Sig|: ECDSA signature (r, s, P) ≈ 72 bytes
    POINT_SIZE = 33              # Compressed EC point size

    def __init__(self, config: SystemConfig = None):
        super().__init__(config)

    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float = 1.0,
                            **kwargs) -> float:
        """
        PPDA aggregation latency per epoch.

        From Table III:
          Aggregation: N_s × (T_PEnc + T_Sig + T_BVer + T_PAdd)

        The paper performs FLAT aggregation: all sensors encrypt and
        sign individually, then ES batch-verifies and aggregates.
        No hierarchical structure → latency grows steeply with N_s.

        Per-sensor pipeline cost includes sensor-side T_PEnc + T_Sig
        because IIoT sensors are resource-constrained — each sensor's
        report arrives at the ES only after the sensor finishes its
        Paillier encryption (5.2ms) and ECDSA signing (1.1ms).
        This creates a sequential arrival pipeline at the ES.
        """
        N_s = int(num_sensors * workload_intensity)
        sensors_per_es = N_s / max(1, num_fog_nodes)

        # Per Table III: N_s × (T_PEnc + T_Sig + T_BVer + T_PAdd)
        # Distributed across F edge servers, per-ES sequential cost:
        t_per_sensor = (
            self.T_PAILLIER_ENC +       # Sensor: Paillier encryption
            self.T_ECDSA_SIGN +         # Sensor: ECDSA signature
            self.T_BATCH_VERIFY_PER +   # ES: batch verify per sensor
            self.T_PAILLIER_ADD         # ES: homomorphic aggregation
        )
        t_es = sensors_per_es * t_per_sensor

        # Batch verify base cost + differential privacy noise
        t_es += self.T_BATCH_VERIFY_BASE + self.T_LAPLACE_NOISE

        # CC side: batch verify ES signatures + ONE Paillier decryption
        # (homomorphic property: CC aggregates ES sub-ciphertexts via
        # Paillier multiplication, then decrypts the final aggregate ONCE)
        t_cc_verify = (self.T_BATCH_VERIFY_BASE +
                       num_fog_nodes * self.T_BATCH_VERIFY_PER)
        t_cc_agg = (num_fog_nodes - 1) * self.T_PAILLIER_ADD  # Combine sub-aggregates
        t_cc_decrypt = self.T_PAILLIER_DEC  # Single final decryption

        total = t_es + t_cc_verify + t_cc_agg + t_cc_decrypt

        return total

    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        PPDA has NO recovery mechanism.

        From Table III: Recovery = "—" (None).
        When an ES fails, the entire aggregation for that ES's
        sensors is lost. There is no re-aggregation or recovery.
        """
        return 0.0

    def loss_exposure(self, num_micro_slots: int = 10, **kwargs) -> float:
        """
        PPDA loss exposure = 1.0 (constant).

        Flat encrypted aggregation: a single ES failure potentially
        affects the entire aggregation epoch. No micro-slot partitioning.

        From paper (Sec V, Exp 5): "Ref. [24] performs flat
        privacy-preserving aggregation over encrypted reports."
        """
        return 1.0

    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        PPDA communication overhead.

        From Table IV:
          Data Collection:  N_s × (|CT_P| + |Sig|)  — sensor → ES
          Coordination:     N_s × |Sig|             — batch verify data
          Recovery:         None

        Heavy because every sensor transmits a full Paillier ciphertext
        (256 bytes) plus an ECDSA signature (72 bytes).
        """
        # Data collection: sensor → ES (Paillier ciphertext + ECDSA signature)
        data_collection = num_sensors * (self.CT_PAILLIER_SIZE + self.ECDSA_SIG_SIZE)

        # Coordination: ES → CC (aggregated ciphertexts + ES signatures)
        coordination = num_fog_nodes * (self.CT_PAILLIER_SIZE + self.ECDSA_SIG_SIZE)

        total_bytes = data_collection + coordination
        return total_bytes / 1024.0  # Convert to KB

    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        PPDA completeness.

        No recovery → all data from failed ESs is lost.
        The scheme supports robustness (resistance to differential attacks,
        replay attacks) but not fault recovery.
        """
        return max(0.0, 1.0 - failure_rate)

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        PPDA availability.

        No recovery mechanism. Epochs with any ES failure lose
        that partition's data entirely.
        """
        return max(0.0, 1.0 - failure_rate)
