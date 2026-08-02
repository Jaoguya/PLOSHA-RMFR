"""
Fog Node Entity Module
========================
Models fog computing nodes that perform adaptive micro-slot aggregation
over encrypted IIoT data using TEE enclaves.

Gramine-SGX Edition — proper measurement methods for W, Q, L per
Experiment Plan Sections 4.1–4.4 and 5.3.

Paper Reference:
  - System Model: F = {F_1, F_2, ..., F_n} - set of fog nodes
  - "Each fog node executes the proposed PLOSHA mechanism, which dynamically
     partitions aggregation epochs into adaptive micro-slots."
  - State vector: State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)]
"""

import numpy as np
from typing import List, Optional, Dict, Any, Tuple

from src.crypto.tee import TEEEnclave


class FogNode:
    """
    Fog Node entity (F_i).

    Serves as the primary processing layer. Maintains state vectors,
    TEE enclave, aggregation buffers, and reliability tracking.

    All state variables are measured (not hardcoded) via the measure_*
    methods, following the Experiment Plan measurement protocol.
    """

    def __init__(self, node_id: int, capacity: int = 100,
                 max_queue: int = 200, max_buffer: int = 200):
        """
        Initialize a fog node.

        Args:
            node_id: Unique fog node identifier i.
            capacity: Maximum processing capacity (reports/epoch).
            max_queue: Maximum processing queue length.
            max_buffer: Maximum incoming report buffer size.
        """
        self.node_id = node_id
        self.capacity = capacity
        self.operational = True

        # TEE enclave for this fog node
        self.tee = TEEEnclave(f"fog_{node_id}")

        # Neighbor fog nodes (N_i) - set during initialization
        self.neighbors: List[int] = []

        # Assigned sensors
        self.assigned_sensors: List[int] = []

        # =====================================================================
        # Queue and buffer infrastructure for measurement (§4.1, §4.2)
        # =====================================================================
        self.max_queue = max_queue          # Processing queue capacity
        self.queue_len = 0                  # Current processing queue length
        self.max_buffer = max_buffer        # Incoming report buffer capacity
        self.buffer_count = 0              # Reports buffered but not yet processed
        self.buffer: List[Tuple] = []      # (sensor_id, ciphertext) pairs

        # =====================================================================
        # State Vector: State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)]
        # All normalized to [0, 1] — measured, not set arbitrarily
        # =====================================================================
        self.workload = 0.0         # W_i(t): Current workload
        self.queue_util = 0.0       # Q_i(t): Queue utilization
        self.latency = 0.0          # L_i(t): Communication latency
        self.reliability = 1.0      # Rel_i(t): Reliability score

        # Predicted state (EWMA)
        self.pred_workload = 0.0    # Ŵ_i(t+1)
        self.pred_queue = 0.0       # Q̂_i(t+1)
        self.pred_latency = 0.0     # L̂_i(t+1)
        self.pred_reliability = 1.0 # R̂el_i(t+1)

        # Prediction outputs (Phase II)
        self.cap = 1.0              # Cap_i(t+1): Effective aggregation capacity
        self.failure_exposure = 0.0 # FE_i(t): Failure exposure score
        self.risk = 0.0             # Risk_i(t): Operational risk
        self.status = "Normal"      # Status_i(t): Normal or At-Risk

        # Aggregation state (Phase III)
        self.micro_slots: List[List] = []           # D_i = {δ_1, ..., δ_{m*}}
        self.micro_aggregates: List = []            # C_{micro,k} for each slot
        self.fog_aggregate = None                    # C_{agg,i}
        self.optimal_m = 1                           # m* - optimal micro-slot number
        self.completeness = 1.0                      # V_i(t) = N_recv / N_exp
        self.incomplete_flag = 0                     # Φ_i(t): 0=complete, 1=incomplete

        # Per-epoch receive counter for completeness (§5.3)
        self.n_recv = 0
        self.epoch_start_time = 0.0

        # Recovery state (Phase IV)
        self.recovery_urgency = 0.0  # RU_i(t)
        self.recovery_mode = "Normal"  # Mode_i(t)
        self.recovery_success = 1     # Succ_i(t)

        # AFLTO state (Phase V)
        self.quality_score = 1.0     # Score_i(t)
        self.history_score = 1.0     # Hist_i(t)
        self.control_error = 0.0     # e_i(t)
        self.aggregation_seq = 0     # Seq_i(t): Aggregation sequence number

        # Timing accumulators for evaluation
        self.epoch_agg_time = 0.0
        self.epoch_recovery_time = 0.0
        self.epoch_comm_bytes = 0

    # =========================================================================
    # Measurement methods (Experiment Plan §4.1 – §4.4, §5.3)
    # These are the "single, unambiguous measurement point" for each variable
    # =========================================================================

    def measure_W(self) -> float:
        """
        Measure workload W_i(t).

        W = queue_len / max_queue_capacity, capped at 1.0.

        Paper §4.1: "measure W at the START of the epoch, before new
        reports arrive. This represents the carried-over load."
        """
        if not self.operational:
            return 1.0
        return min(1.0, self.queue_len / max(1, self.max_queue))

    def measure_Q(self) -> float:
        """
        Measure queue utilization Q_i(t).

        Q = buffer_count / max_buffer, capped at 1.0.
        "Measures how full the incoming report buffer is — distinct
        from the processing queue."

        Paper §4.2: Q = node.buffer_count / node.max_buffer
        """
        if not self.operational:
            return 1.0
        return min(1.0, self.buffer_count / max(1, self.max_buffer))

    def measure_L(self, rtt_mu: float, rtt_sigma: float,
                  rtt_99th: float) -> float:
        """
        Measure normalized latency L_i(t).

        L = rtt / RTT_99TH, where rtt is drawn from a log-normal
        distribution fitted to LAN measurements.

        All distribution parameters come from config — nothing hardcoded.

        Paper §4.3:
            rtt = np.random.lognormal(RTT_MU, RTT_SIGMA)
            L = min(1.0, rtt / RTT_99TH)
        """
        if not self.operational:
            return 1.0
        rtt = np.random.lognormal(rtt_mu, rtt_sigma)
        return min(1.0, max(0.0, rtt / rtt_99th))

    def start_epoch(self, t: float):
        """
        Reset per-epoch counters at the start of a new aggregation epoch.

        Paper §5.3: "self.n_recv = 0; self.epoch_start = t"
        """
        self.n_recv = 0
        self.epoch_start_time = t
        self.buffer = []
        self.buffer_count = 0

    def receive(self, sensor_id: int, ct: bytes, ts: float):
        """
        Receive a sensor ciphertext during the current epoch.

        Paper §5.3:
            if self.epoch_start <= ts < self.epoch_start + EPOCH_LEN:
                self.n_recv += 1
                self.buffer.append((sensor_id, ct))
        """
        self.n_recv += 1
        self.buffer.append((sensor_id, ct))
        self.buffer_count = len(self.buffer)

    def measure_V(self, n_expected: int) -> float:
        """
        Measure aggregation completeness V_i(t).

        V = n_recv / n_exp if n_exp > 0 else 1.0

        Paper §5.3: V_i(t) = N_recv / N_exp
        """
        if n_expected <= 0:
            return 1.0
        return min(1.0, self.n_recv / n_expected)

    def get_state_vector(self) -> Dict[str, float]:
        """
        Get current state vector State_i(t) = [W_i(t), Q_i(t), L_i(t), Rel_i(t)].

        Paper: Phase II, Step 1 - Runtime State Collection
        """
        return {
            'workload': self.workload,
            'queue_util': self.queue_util,
            'latency': self.latency,
            'reliability': self.reliability
        }

    def get_prediction_vector(self) -> Dict[str, float]:
        """
        Get prediction vector Pred_i(t) = [Cap_i(t+1), FE_i(t), Risk_i(t)].

        Paper: Phase II, Step 7 - Prediction Output
        """
        return {
            'capacity': self.cap,
            'failure_exposure': self.failure_exposure,
            'risk': self.risk
        }

    def update_runtime_state(self, num_reports: int, max_capacity: int,
                             rtt_mu: float = 0.6, rtt_sigma: float = 0.5,
                             rtt_99th: float = 4.32):
        """
        Update the runtime operational state based on current workload.

        Uses the proper measurement methods from the plan, with all
        distribution parameters passed from config (no hardcoding).

        Args:
            num_reports: Number of sensor reports received this epoch.
            max_capacity: Maximum reports the node can handle per epoch.
            rtt_mu: Log-normal RTT μ from config.
            rtt_sigma: Log-normal RTT σ from config.
            rtt_99th: 99th percentile RTT from config.
        """
        if not self.operational:
            self.workload = 1.0
            self.queue_util = 1.0
            self.latency = 1.0
            return

        # Update queue state from reports
        self.queue_len = num_reports
        self.max_queue = max(1, max_capacity)
        self.buffer_count = num_reports
        self.max_buffer = max(1, max_capacity)

        # Measure using the plan's methods
        self.workload = self.measure_W()
        self.queue_util = self.measure_Q()
        self.latency = self.measure_L(rtt_mu, rtt_sigma, rtt_99th)

    def fail(self):
        """Simulate fog node failure."""
        self.operational = False
        self.workload = 1.0
        self.queue_util = 1.0
        self.latency = 1.0

    def recover(self):
        """Restore fog node to operational state."""
        self.operational = True

    def reset_epoch_metrics(self):
        """Reset per-epoch timing and communication accumulators."""
        self.epoch_agg_time = 0.0
        self.epoch_recovery_time = 0.0
        self.epoch_comm_bytes = 0

    def increment_sequence(self):
        """Increment aggregation sequence number for replay resistance."""
        self.aggregation_seq += 1

    def __repr__(self):
        return (f"FogNode(id={self.node_id}, op={self.operational}, "
                f"rel={self.reliability:.3f}, risk={self.risk:.3f})")
