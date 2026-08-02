"""
Baseline: Fault Tolerant Data Stream Processing in SEC (Ref [38])
==================================================================
Efficient and Fault Tolerant Data Stream Processing with Uncertain
Data Rates in Serverless Edge Computing.

Paper: Xu et al., "Efficient and Fault Tolerant Data Stream Processing
       with Uncertain Data Rates in Serverless Edge Computing",
       IEEE Trans. Services Computing, 2026.

Algorithm Summary (from the paper):
  1. Optimization Framework (Algorithm 1 - Fwk, Sec IV-A):
     - Binary search for optimal number of standby instances n_m
     - Range: n_min=1, n_max=K (max standby instances = 3)
     - Fault tolerance requirement: Π Pr(f_l) ≥ δ_m  (Eq. 1)
       where Pr(f_l) = 1 - (p_l)^{n_m+1}

  2. Function Placement (Algorithm 2 - Heu, Sec IV-B):
     - Stage 1: DAG partitioning via topological sort on G'_m
     - Stage 2: Active function placement via shortest path in
       auxiliary graph G''_m
     - Stage 3: Standby instance placement via nm disjoint
       shortest paths
     - Time: O(|R|·log|V|·(|V'_m|·|V|·log(|V'_m|·|V|) +
              |V'_m|·(|E|+|V|)))  (Theorem 2)

  3. Online Prediction & Adjustment (Algorithm 3 - Adj, Sec V):
     - MT-LSTM prediction across multiple timescales
     - Groups: g_m = ⌈log₂ ρ̃_m⌉ - 1  (Eq. 6)
     - Adjustment ratio: ξ_m = ρ̂_{m,τ+1}/(ρ_{m,τ} + ρ̂_{m,τ+1})  (Eq. 7)
     - Re-locate ⌈n_m · ξ_m⌉ standby instances
     - Time: O(T·|R|·g_m·h_g² + T·|R|·log|V|·Θ)  (Theorem 4)

  4. Delay Model (Sec III-E):
     - Processing delay: d^proc = x·α_j·M_j·ρ + x'·d^o  (Eq. 2)
     - Transmission delay: max over execution paths in DAG  (Eq. 3)
     - Recovering delay: Σ y_l·κ_m·Σ d_e  (Eq. 4)
     - Total: d_m = d^pt + d^rec  (Eq. 5)

Computation Cost (Table III of PLOSHA-RMFR paper):
  Prediction:   O(T × |R| × g_m × h_g²)  (MT-LSTM)
  Scheduling:   O(|R| × log|V| × Θ)
  Aggregation:  None
  Recovery:     O(|R| × log|V| × Θ)

  where Θ = |V'_m|·|V|·log(|V'_m|·|V|) + |V'_m|·(|E|+|V|)

Communication Cost (Table IV):
  Data Collection:  N_s × |Data|
  Coordination:     |Pred| + |Place|
  Recovery:         m* × |State| (scales with FULL processing window)

Key behavioral characteristics:
  - MT-LSTM prediction + graph-based placement optimization
  - Active-standby failover mechanism for fault tolerance
  - Recovery = full service relocation + standby reconfiguration
  - Recovery scales with full processing window m*
  - Highest prediction complexity among all baselines
  - Placement overhead grows with O(log|V|) network size
"""

import numpy as np
from evaluation.baselines.base import BaselineScheme
from src.config import SystemConfig


class Ref38Scheme(BaselineScheme):
    """Fault Tolerant Data Stream Processing in SEC."""

    name = "Ref. [38]"

    # ---- Calibrated parameters (from Ref[38] paper, Sec VI) ----
    # SEC network parameters (Sec VI-A)
    V_CLOUDLETS_BASE = 50        # |V|: base number of cloudlets
    E_LINKS_PER_CLOUDLET = 4     # Average links per cloudlet → |E| ≈ 4×|V|/2
    VM_FUNCTIONS_PER_REQ = 5     # |V'_m|: functions per DAG (avg)

    # MT-LSTM parameters (Sec V-B)
    # h_g = hidden state dimension per group
    H_G = 64                     # Hidden state dimension
    G_M_DEFAULT = 3              # Default number of timescale groups

    # Timing parameters
    T_MTLSTM_COEFF = 1e-8       # MT-LSTM per-(g_m × h_g²) coefficient
    T_PLACEMENT_COEFF = 1e-8    # Graph placement per-Θ coefficient
    T_TOPO_SORT = 0.0001        # Topological sort per DAG

    # Processing delays (Sec III-E, Eq. 2)
    ALPHA_J = 0.07               # Impact factor for memory allocation (Sec VI-A)
    M_J_MEMORY = 85              # Memory per unit data rate (MB) (Sec VI-A)
    D_COLD_START = 0.002         # Cold start delay per function (seconds)

    # Communication sizes
    DATA_SIZE = 128              # |Data|: raw sensor data payload (bytes)
    PRED_SIZE = 1024             # |Pred|: prediction output size (bytes)
    PLACE_SIZE = 2048            # |Place|: placement plan size (bytes)
    STATE_SIZE = 4096            # |State|: per-slot state (larger than Ref[37]
                                 # due to serverless function state + standby info)

    # Standby instances (Sec III-D)
    N_M_STANDBY = 3              # Maximum standby instances (Sec VI-A)
    HEARTBEAT_SIZE = 64          # Heartbeat payload: timestamp + node_id + status (bytes)

    def __init__(self, config: SystemConfig = None):
        super().__init__(config)

    def _theta(self, V_m: int, V: int, E: int) -> float:
        """
        Compute Θ = |V'_m|·|V|·log(|V'_m|·|V|) + |V'_m|·(|E|+|V|).
        From the complexity analysis in Theorem 2.
        """
        product = max(1, V_m * V)
        return V_m * V * np.log2(max(2, product)) + V_m * (E + V)

    def _network_params(self, num_fog_nodes: int):
        """Map IIoT fog nodes to SEC network parameters."""
        V = max(self.V_CLOUDLETS_BASE, num_fog_nodes * 2)  # Cloudlets
        E = V * self.E_LINKS_PER_CLOUDLET // 2  # Links
        V_m = self.VM_FUNCTIONS_PER_REQ
        return V, E, V_m

    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float = 1.0,
                            **kwargs) -> float:
        """
        SEC data stream processing latency per epoch.

        From Table III:
          Prediction:  O(T × |R| × g_m × h_g²)  (MT-LSTM)
          Scheduling:  O(|R| × log|V| × Θ)

        MT-LSTM has the highest prediction complexity among baselines.
        Graph-based placement optimization adds significant overhead
        especially as network size grows.
        """
        N_s = int(num_sensors * workload_intensity)
        V, E, V_m = self._network_params(num_fog_nodes)
        R = max(1, N_s)  # Each sensor = one stream processing request

        # MT-LSTM prediction: O(T × |R| × g_m × h_g²)
        g_m = self.G_M_DEFAULT
        T_slots = 1  # Single time slot per epoch
        t_predict = T_slots * R * g_m * (self.H_G ** 2) * self.T_MTLSTM_COEFF

        # Topological sort for DAG partitioning (Stage 1 of Heu)
        t_topo = R * self.T_TOPO_SORT

        # Function placement: O(|R| × log|V| × Θ) (Stage 2+3 of Heu)
        theta = self._theta(V_m, V, E)
        t_placement = R * np.log2(max(2, V)) * theta * self.T_PLACEMENT_COEFF

        # Processing delay (Eq. 2): d^proc = α_j · M_j · ρ + d_cold
        # Averaged over all cloudlets
        rho = workload_intensity  # Data rate factor
        t_processing = self.ALPHA_J * (self.M_J_MEMORY / 1000.0) * rho
        # Cold start: probability = 1/(1 + N_M_STANDBY) per Sec III-D
        # (standby instances reduce cold starts proportionally)
        cold_start_prob = 1.0 / (1.0 + self.N_M_STANDBY)
        t_cold_start = V_m * self.D_COLD_START * cold_start_prob

        total = t_predict + t_topo + t_placement + t_processing + t_cold_start

        # Note: More fog nodes → more cloudlets → higher |V| → higher
        # placement complexity O(|R|·log|V|·Θ). The _network_params()
        # and _theta() already capture this scaling correctly.
        # No artificial division — complexity grows with infrastructure.

        return total

    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        SEC recovery latency.

        From Table III:
          Recovery: O(|R| × log|V| × Θ)

        Recovery involves:
          1. Detecting failed function (via heartbeat)
          2. Switching to standby instance (active-standby failover)
          3. Re-locating standby instances (Adj Algorithm 3, Stage 2)
          4. State migration to new cloudlet (κ_m state buffer, Eq. 4)

        Recovery scales with the FULL processing window m*, not |D_i^miss|.
        From paper (Sec V, Exp 6): "Ref. [38] incurs even higher
        communication overhead because service migration and standby
        reconfiguration require transferring larger execution states."
        """
        V, E, V_m = self._network_params(num_fog_nodes)
        R = max(1, num_sensors)  # Each sensor = one stream processing request
        # Use continuous expected value for smooth scaling.
        # E[failed_nodes] = num_fog_nodes × failure_rate
        failed_nodes = num_fog_nodes * failure_rate
        if failed_nodes < 1e-9:
            return 0.0

        # Standby instance switching delay
        # d^rec = Σ y_l · κ_m · Σ d_e  (Eq. 4)
        # κ_m = STATE_SIZE in bytes; d_e modeled from placement graph
        # avg path length in graph ≈ log2(|V|) hops
        avg_path_length = max(1, int(np.log2(max(2, V))))
        # Per-hop delay derived from placement coefficient
        per_hop_delay = self.T_PLACEMENT_COEFF * V
        t_switching = failed_nodes * V_m * (self.STATE_SIZE / 1024.0) * per_hop_delay * avg_path_length

        # Re-location of standby instances (Stage 3 of Heu, repeated)
        theta = self._theta(V_m, V, E)
        t_relocation = failed_nodes * np.log2(max(2, V)) * theta * self.T_PLACEMENT_COEFF

        # Full window scaling: m* × state migration per failed node
        # State migration cost = STATE_SIZE transfer at placement rate
        t_state_migration = num_micro_slots * failed_nodes * self.D_COLD_START

        total = t_switching + t_relocation + t_state_migration

        return total

    def loss_exposure(self, num_micro_slots: int = 10, **kwargs) -> float:
        """
        SEC loss exposure.

        Active-standby failover provides some fault isolation, but
        recovery operates at the service/function level, not micro-slot.
        Standby instances can absorb single failures, but cascading
        failures affect larger portions.

        From paper (Sec V, Exp 5): "Refs. [37] and [38] recover
        failures at the workflow or service level, resulting in larger
        recovery scopes and greater aggregation disruption."

        The standby mechanism provides better isolation than Ref[37]
        (since standby instances are already deployed), but still
        worse than PLOSHA-RMFR's micro-slot containment.
        """
        # Service-level recovery: the recovery scope covers one function
        # and its downstream chain in the V_m-function DAG.
        #
        # With VM_FUNCTIONS_PER_REQ = 5, a failure at any function
        # exposes approximately (V_m - 1)/V_m of downstream processing.
        # Standby instances (N_M_STANDBY = 3) absorb the immediate
        # failure but don't prevent downstream data disruption.
        #
        # Loss exposure = (V_m - 1) / V_m = 4/5 = 0.80
        # Slightly reduced by standby coverage:
        # Effective = (V_m - 1) / (V_m + N_M_STANDBY)
        # = 4 / 8 = 0.50
        #
        # Better than Ref[37] (0.75) because standbys provide faster
        # failover, but worse than PLOSHA-RMFR's micro-slot containment.
        return (self.VM_FUNCTIONS_PER_REQ - 1.0) / (
            self.VM_FUNCTIONS_PER_REQ + self.N_M_STANDBY)

    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        SEC communication overhead.

        From Table IV:
          Data Collection:  N_s × |Data|
          Coordination:     |Pred| + |Place|
          Recovery:         m* × |State| (full processing window)

        From paper (Sec V, Exp 6): "Ref. [38] incurs even higher
        communication overhead because service migration and standby
        reconfiguration require transferring larger execution states
        and placement information across distributed cloudlets."
        """
        # Data collection: sensor → cloudlet
        data_collection = num_sensors * self.DATA_SIZE

        # Coordination: prediction output + placement plan
        coordination = num_fog_nodes * (self.PRED_SIZE + self.PLACE_SIZE)

        # Recovery: m* × |State| — full processing window scaling
        # This is the LARGEST recovery communication among all baselines
        failed_nodes = num_fog_nodes * failure_rate
        recovery_comm = num_micro_slots * self.STATE_SIZE * failed_nodes

        # Standby instance maintenance overhead (periodic heartbeats)
        standby_overhead = num_fog_nodes * self.N_M_STANDBY * self.HEARTBEAT_SIZE

        total_bytes = data_collection + coordination + recovery_comm + standby_overhead
        return total_bytes / 1024.0

    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        SEC completeness.

        Active-standby failover: Pr(f_l) = 1 - p_l^{n_m+1} (Eq. 1).
        With n_m standby instances, a function survives if at least
        one of (n_m + 1) instances is alive.

        Per-function survival probability:
          Pr_survive = 1 - failure_rate^(N_M_STANDBY + 1)

        Overall completeness = Pr_survive (since all functions
        independently apply the same standby model).
        """
        # Per Eq. 1: probability all (n_m+1) instances fail simultaneously
        p_all_fail = failure_rate ** (self.N_M_STANDBY + 1)
        return max(0.0, 1.0 - p_all_fail)

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        SEC availability.

        Active-standby failover: system is available as long as at
        least one instance (active or standby) is alive per function.
        Same model as completeness (Eq. 1), but availability also
        accounts for cold-start delay during standby activation.

        Availability = Pr_survive × (1 - cold_start_fraction)
        where cold_start_fraction = D_COLD_START / epoch_duration
        """
        p_all_fail = failure_rate ** (self.N_M_STANDBY + 1)
        pr_survive = 1.0 - p_all_fail
        # Cold-start penalty: standby activation adds D_COLD_START delay
        # relative to a typical epoch (~1s). Only applies when failover occurs.
        cold_start_fraction = self.D_COLD_START * failure_rate
        return max(0.0, pr_survive - cold_start_fraction)
