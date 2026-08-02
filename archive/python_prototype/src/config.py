"""
PLOSHA-RMFR Configuration Module
=================================
All system parameters, notation constants, and default thresholds
as defined in the paper (Table 1 and throughout Phases I-V).

Gramine-SGX Edition — all defaults aligned to the Experiment Plan
(PLOSHA-RMFR_Experiment_Plan_Gramine.md, Table 1).

Reference: PLOSHA-RMFR Paper - Table 1 (Major Notations)
"""

import math
import dataclasses
from typing import Dict, Any, List


@dataclasses.dataclass
class SystemConfig:
    """Complete configuration for the PLOSHA-RMFR framework."""

    # =========================================================================
    # System Scale Parameters (Section V - Experimental Setup)
    # =========================================================================
    num_sensors: int = 1000          # |S| - Number of IIoT sensors
    num_fog_nodes: int = 10          # |F| - Number of fog nodes
    num_epochs: int = 50             # Number of aggregation epochs to simulate
    epoch_duration: float = 1.0      # Δ - Duration of one aggregation epoch (seconds)

    # =========================================================================
    # Cryptographic Parameters
    # =========================================================================
    paillier_key_size: int = 2048    # Security parameter λ (bits) for Paillier keygen
    aes_key_size: int = 256          # AES-GCM key size (bits)

    # =========================================================================
    # Sensor Data Parameters (Section 3)
    # =========================================================================
    max_sensor_value: int = 65535    # d_j ∈ [0, 65535] — 16-bit unsigned integer
    sensor_encoding: str = 'integer' # 'integer' (plan default) or 'float'

    # =========================================================================
    # Phase II: Predictive Capacity and Risk Estimation
    # =========================================================================

    # Step 2: EWMA smoothing coefficient α ∈ (0,1)
    # Ŝtate_i(t+1) = α · State_i(t) + (1-α) · Ŝtate_i(t)
    alpha_ewma: float = 0.3

    # Step 3: Effective Aggregation Capacity weights
    # Cap_i(t+1) = R̂el_i · (1 - (ω_w · Ŵ_i + ω_q · Q̂_i + ω_l · L̂_i))
    # Constraint: ω_w + ω_q + ω_l = 1
    omega_w: float = 0.4   # Workload weight
    omega_q: float = 0.3   # Queue utilization weight
    omega_l: float = 0.3   # Communication latency weight

    # Step 5: Operational Risk weights
    # Risk_i(t) = η_1 · (1 - Cap_i(t+1)) + η_2 · FE_i(t)
    # Constraint: η_1 + η_2 = 1
    eta_1: float = 0.5    # Capacity degradation weight
    eta_2: float = 0.5    # Failure exposure weight

    # Step 6: Risk classification threshold
    # Status = "Normal" if Risk < τ_r, else "At-Risk"
    tau_r: float = 0.5

    # =========================================================================
    # Phase III: PLOSHA - Adaptive Hierarchical Slot Aggregation
    # =========================================================================

    # Step 1: Micro-slot optimization
    # m* = argmin_{1≤m≤m_max} [λ_1·β_t·m·(1-Cap) + λ_2·FE/m + λ_3·(1-Rel)/m]
    m_max: int = 20                        # Maximum number of micro-slots
    beta_t: float = 0.01                   # Average processing overhead per micro-slot
    lambda_m1: float = 1.0 / 3.0           # Processing overhead weight
    lambda_m2: float = 1.0 / 3.0           # Aggregation-loss exposure weight
    lambda_m3: float = 1.0 / 3.0           # Reliability weight

    # Step 6: Aggregation completeness threshold
    # Φ_i(t) = 1 if V_i(t) < τ_v (incomplete), 0 otherwise (complete)
    tau_v: float = 0.9

    # =========================================================================
    # Phase IV: RMFR - Risk-Aware Multi-Layer Fault Recovery
    # =========================================================================

    # Step 1: Recovery Urgency weights
    # RU_i(t) = ρ_1·Risk_i + ρ_2·(1-V_i) + ρ_3·(1-Rel_i)
    # Constraint: ρ_1 + ρ_2 + ρ_3 = 1
    rho_1: float = 1.0 / 3.0    # Risk weight
    rho_2: float = 1.0 / 3.0    # Incompleteness weight
    rho_3: float = 1.0 / 3.0    # Unreliability weight

    # Step 2: Recovery Escalation Thresholds
    # Normal:        Φ_i=0 ∧ RU < τ_1
    # Delegation:    Φ_i=0 ∧ τ_1 ≤ RU < τ_2
    # MicroRecovery: Φ_i=1 ∧ RU < τ_3
    # Failover:      RU ≥ τ_3 ∨ Rel_i ≤ τ_f
    # Constraint: 0 ≤ τ_1 < τ_2 < τ_3 ≤ 1
    tau_1: float = 0.3    # Normal → Delegation threshold
    tau_2: float = 0.5    # Delegation → MicroRecovery threshold
    tau_3: float = 0.7    # MicroRecovery → Failover threshold
    tau_f: float = 0.2    # Failover reliability threshold

    # Minimum gap between escalation thresholds (Section 7.2)
    threshold_margin: float = 0.01

    # Step 3: Recovery Candidate Utility weights
    # U_j(t) = α_c·Cap_j + α_r·Rel_j + α_k·(1-Risk_j)
    # Constraint: α_c + α_r + α_k = 1
    alpha_c: float = 1.0 / 3.0    # Capacity weight
    alpha_r: float = 1.0 / 3.0    # Reliability weight
    alpha_k: float = 1.0 / 3.0    # Risk-free weight

    # Step 7: Reliability Reinforcement
    # Rel_i(t+1) = min{1, β_r·Rel_i(t) + (1-β_r)·[λ_s·Succ_i + λ_v·V_i]}
    beta_r: float = 0.9     # Reliability momentum (history decay)
    lambda_s: float = 0.5   # Recovery success weight
    lambda_v: float = 0.5   # Completeness weight

    # =========================================================================
    # Phase V: AFLTO - Adaptive Feedback Learning & Threshold Optimization
    # =========================================================================

    # Step 2: Quality score weights
    # Score_i(t) = ω_1·V_i(t) + ω_2·Rel_i(t+1)
    # Constraint: ω_1 + ω_2 = 1
    omega_score_1: float = 0.5   # Completeness weight
    omega_score_2: float = 0.5   # Reliability weight

    # Step 3: Historical learning
    # Hist_i(t+1) = γ·Hist_i(t) + (1-γ)·Score_i(t)
    gamma_decay: float = 0.9     # γ ∈ (0,1) - History decay factor

    # Score_i*(t) = α_h·Hist_i(t+1) + (1-α_h)·Score_i(t)
    alpha_hist: float = 0.3      # α_h ∈ (0,1) - History blend factor

    # Adaptive control error weights
    # e_i(t) = κ_1·(S̄ - Score_i*) + κ_2·RU_i + κ_3·(1-Rel_i)
    # Constraint: κ_1 + κ_2 + κ_3 = 1
    s_target: float = 0.95       # S̄ - Target quality score
    kappa_1: float = 1.0 / 3.0   # Quality gap weight
    kappa_2: float = 1.0 / 3.0   # Recovery urgency weight
    kappa_3: float = 1.0 / 3.0   # Unreliability weight

    # Step 4: Threshold learning rates
    # τ_x(t+1) = Π_{[0,1]}(τ_x(t) + μ_x · e_i(t))
    mu_v: float = 0.05    # Learning rate for τ_v
    mu_r: float = 0.05    # Learning rate for τ_r
    mu_1: float = 0.05    # Learning rate for τ_1
    mu_2: float = 0.05    # Learning rate for τ_2
    mu_3: float = 0.05    # Learning rate for τ_3
    mu_f: float = 0.05    # Learning rate for τ_f

    # =========================================================================
    # Simulation Parameters
    # =========================================================================
    failure_rate: float = 0.05        # Probability of fog-node failure per epoch
    sensor_drop_rate: float = 0.02    # Probability of sensor report loss
    workload_intensity: float = 1.0   # Multiplier for sensor reporting rate
    random_seed: int = 42             # For reproducibility

    # Pre-announced seeds for multi-run experiments (Section 10, Item 5)
    num_runs: int = 30                # Number of repeat runs per experiment config

    # =========================================================================
    # Latency Model Parameters (Section 4.3)
    # Log-normal RTT distribution for communication latency
    # =========================================================================
    rtt_mu: float = 0.6              # Log-normal μ (typical LAN: mean ~2ms)
    rtt_sigma: float = 0.5           # Log-normal σ (typical LAN: std ~1ms)

    # =========================================================================
    # Timing Model Parameters (calibrated to known operation costs)
    # =========================================================================
    t_aes: float = 0.00001           # T_AES: AES-GCM encrypt/decrypt (seconds)
    t_penc: float = 0.005            # T_PEnc: Paillier encryption (seconds)
    t_padd: float = 0.0001           # T_PAdd: Paillier homomorphic add (seconds)
    t_pdec: float = 0.005            # T_PDec: Paillier decryption (seconds)
    t_tee: float = 0.0001            # T_TEE: TEE ciphertext transformation (seconds)
    t_sig: float = 0.001             # T_Sig: Digital signature (seconds)
    t_pred: float = 0.0001           # T_pred: EWMA prediction (seconds)
    t_risk: float = 0.0001           # T_risk: Risk assessment (seconds)
    t_sch: float = 0.00005           # T_sch: Scheduling operation (seconds)

    # Communication sizes (bytes)
    ct_aes_size: int = 36            # |CT_AES|: nonce(12) + ct(8) + tag(16) = 36 bytes
    ct_paillier_size: int = 512      # |CT_P|: Paillier ciphertext size (2048-bit key)
    meta_size: int = 64              # |Meta|: Micro-slot metadata size
    dsp_size: int = 512              # |DSP|: Delegation state packet size
    fsm_size: int = 1024             # |FSM|: Failover state migration size
    sig_size: int = 72               # |Sig|: ECDSA-P256 DER signature size (typ. 70-72)

    @property
    def rtt_99th(self) -> float:
        """Compute 99th percentile of the log-normal RTT distribution.

        RTT_99TH = exp(μ + 2.576·σ)
        Computed from the configured μ, σ — not hardcoded.
        """
        return math.exp(self.rtt_mu + 2.576 * self.rtt_sigma)

    def get_pre_announced_seeds(self) -> List[int]:
        """Generate deterministic seed list from base seed.

        30 seeds derived from the base random_seed so they are
        reproducible but not hand-picked.
        """
        import random as _rng
        gen = _rng.Random(self.random_seed)
        return [gen.randint(0, 2**31 - 1) for _ in range(self.num_runs)]

    def validate(self):
        """Validate weight constraints from the paper."""
        _tol = 1e-9
        assert abs(self.omega_w + self.omega_q + self.omega_l - 1.0) < _tol, \
            "Capacity weights must sum to 1: ω_w + ω_q + ω_l = 1"
        assert abs(self.eta_1 + self.eta_2 - 1.0) < _tol, \
            "Risk weights must sum to 1: η_1 + η_2 = 1"
        assert abs(self.rho_1 + self.rho_2 + self.rho_3 - 1.0) < _tol, \
            "Recovery urgency weights must sum to 1: ρ_1 + ρ_2 + ρ_3 = 1"
        assert abs(self.alpha_c + self.alpha_r + self.alpha_k - 1.0) < _tol, \
            "Candidate utility weights must sum to 1: α_c + α_r + α_k = 1"
        assert abs(self.lambda_s + self.lambda_v - 1.0) < _tol, \
            "Reliability reward weights must sum to 1: λ_s + λ_v = 1"
        assert abs(self.omega_score_1 + self.omega_score_2 - 1.0) < _tol, \
            "Quality score weights must sum to 1: ω_1 + ω_2 = 1"
        assert abs(self.kappa_1 + self.kappa_2 + self.kappa_3 - 1.0) < _tol, \
            "Error weights must sum to 1: κ_1 + κ_2 + κ_3 = 1"
        assert abs(self.lambda_m1 + self.lambda_m2 + self.lambda_m3 - 1.0) < _tol, \
            "Micro-slot opt weights must sum to 1: λ_1 + λ_2 + λ_3 = 1"
        assert 0 <= self.tau_1 < self.tau_2 < self.tau_3 <= 1.0, \
            "Escalation ordering: 0 ≤ τ_1 < τ_2 < τ_3 ≤ 1"
        assert 0 < self.alpha_ewma < 1, "EWMA coefficient: α ∈ (0,1)"
        assert 0 < self.gamma_decay < 1, "History decay: γ ∈ (0,1)"
        assert 0 < self.alpha_hist < 1, "History blend: α_h ∈ (0,1)"
        assert 0 < self.beta_r < 1, "Reliability momentum: β_r ∈ (0,1)"
        assert self.threshold_margin > 0, "Threshold margin must be > 0"

    def to_dict(self) -> Dict[str, Any]:
        """Export configuration as dictionary."""
        return dataclasses.asdict(self)


# Default configuration instance
DEFAULT_CONFIG = SystemConfig()
DEFAULT_CONFIG.validate()
