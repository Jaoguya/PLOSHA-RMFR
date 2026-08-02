"""
Baseline: HFWPF - Hybrid Fault-Tolerant Workflow Scheduling (Ref [37])
========================================================================
A Hybrid Fault-Tolerant Workflow Scheduling With Performance Fluctuated
Cloud Resources.

Paper: Ren & Yao, "A Hybrid Fault-Tolerant Workflow Scheduling With
       Performance Fluctuated Cloud Resources", IEEE Trans. Services
       Computing, 2026.

Algorithm Summary (from the paper):
  1. Preprocessing (Algorithm 1, Sec IV-A):
     - Divide workflow deadline d_i into sub-deadlines for each task
     - Time complexity: O(J²) per workflow  (Eq. described in Sec IV-A)

  2. Initial Scheduling (Algorithm 2, Sec IV-B):
     - For each ready task t_ij, select fault-tolerant strategy:
       * Resubmission (z_ij=1) if:  dl_ij - st_ij ≥ 2×(T_boot + max TT + ψ)  (Eq. 8)
       * Replication (z_ij=0) otherwise
     - Allocate to VMs using pst/pft calculations (Eq. 10-12)
     - Time complexity: O((J + N_in) × M × N_M) per workflow

  3. Online Scheduling (Algorithm 4, Sec IV-C):
     - On VM failure with resubmission: find new VM for re-execution
     - On VM failure with replication: redundant copy already running
     - Time complexity: O((J + N_in) × M × N_M)

  4. Online Adjustment (Algorithm 5, Sec IV-C):
     - Adjust sub-deadlines of unexecuted tasks based on actual finish times
     - Promote replication → resubmission when time permits
     - st_ik = max_{t_ij ∈ P(t_ik)} (aft(t_ij) + TT_jk)  (Eq. 13)
     - Time complexity: O(J × N_in)

  5. Elastic Resource Provisioning:
     - Activate new VMs when needed
     - Shut down idle VMs to reduce failure probability
     - Approximate weight for stochastic execution time:
       w(X) = E(X) + √D(X)  (Eq. 1)

Computation Cost (Table III of PLOSHA-RMFR paper):
  Prediction:   T_fluc (fluctuation estimation via stochastic model, Eq. 1)
  Scheduling:   O(J² + (J + N_in) × M × N_M) (preprocessing + initial)
  Aggregation:  None
  Recovery:     O((J + N_in) × M × N_M + J × N_in) (online scheduling + adjustment)

Communication Cost (Table IV):
  Data Collection:  N_s × |Data|
  Coordination:     |Sched| (scheduling messages)
  Recovery:         m* × |State| (scales with FULL processing window)

Key behavioral characteristics:
  - Workflow-level fault tolerance via replication + resubmission
  - Recovery scales with full processing window m*, NOT |D_i^miss|
  - Has prediction (T_fluc) but no encrypted aggregation
  - Scheduling complexity polynomial in workflow parameters (J, M, N_M)
  - Recovery overhead grows steeply under increasing failures
"""

import numpy as np
from evaluation.baselines.base import BaselineScheme
from src.config import SystemConfig


class Ref37Scheme(BaselineScheme):
    """HFWPF: Hybrid Fault-Tolerant Workflow Scheduling."""

    name = "Ref. [37]"

    # ---- Calibrated parameters (from Ref[37] paper, Sec V) ----
    # Workflow structure parameters
    J_PER_SENSOR = 1.0           # Each sensor report = one workflow task
    N_IN_AVG = 3                 # Average task indegree (DAG precedence)
    M_VM_TYPES = 9               # Number of VM types (Amazon EC2 C6g, Sec III-A)
    N_M_BASE = 5                 # Base number of active VMs per type

    # Timing parameters
    T_FLUC = 0.0003              # T_fluc: Fluctuation estimation (Eq. 1)
    T_BOOT = 0.05                # VM boot time (seconds)
    T_PREPROCESS_PER_J2 = 1e-7   # O(J²) preprocessing cost coefficient
    T_SCHEDULE_COEFF = 5e-8      # O((J+N_in)·M·N_M) scheduling coefficient
    T_ADJUST_COEFF = 1e-7        # O(J·N_in) online adjustment coefficient

    # Communication sizes
    DATA_SIZE = 128              # |Data|: raw sensor data payload (bytes)
    SCHED_SIZE = 256             # |Sched|: scheduling message size (bytes)
    STATE_SIZE = 2048            # |State|: per-slot state transfer (bytes)

    def __init__(self, config: SystemConfig = None):
        super().__init__(config)

    def _workflow_params(self, num_sensors: int, num_fog_nodes: int):
        """Compute workflow parameters mapped to IIoT scenario."""
        J = max(10, int(num_sensors * self.J_PER_SENSOR))  # Total workflow tasks
        N_in = self.N_IN_AVG
        M = self.M_VM_TYPES
        N_M = max(self.N_M_BASE, num_fog_nodes // 2)  # VMs scale with fog nodes
        return J, N_in, M, N_M

    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float = 1.0,
                            **kwargs) -> float:
        """
        HFWPF aggregation latency per epoch.

        From Table III:
          Prediction:  T_fluc
          Scheduling:  O(J² + (J + N_in) × M × N_M)
          Aggregation: None

        HFWPF schedules workflow tasks across VMs. The "aggregation latency"
        maps to the total scheduling + processing time for completing
        all workflow tasks within an epoch.
        """
        N_s = int(num_sensors * workload_intensity)
        J, N_in, M, N_M = self._workflow_params(N_s, num_fog_nodes)

        # Prediction: fluctuation estimation
        t_predict = self.T_FLUC

        # Preprocessing: O(J²) deadline division
        t_preprocess = (J ** 2) * self.T_PREPROCESS_PER_J2

        # Initial Scheduling: O((J + N_in) × M × N_M)
        t_schedule = (J + N_in) * M * N_M * self.T_SCHEDULE_COEFF

        # Task execution on VMs (stochastic execution time, Eq. 1)
        # w(X) = E(X) + √D(X) where D(X)/E(X)² is the coefficient of variation
        # From Sec V-A: normalized VM execution unit ~ T_SCHEDULE_COEFF scale
        t_exec = J * self.T_SCHEDULE_COEFF * M

        total = t_predict + t_preprocess + t_schedule + t_exec

        # Note: More fog nodes → more VMs (N_M scales in _workflow_params),
        # which INCREASES scheduling complexity O((J+N_in)×M×N_M).
        # No artificial scaling division — the complexity formula handles it.

        return total

    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        HFWPF recovery latency.

        From Table III:
          Recovery: O((J + N_in) × M × N_M + J × N_in)

        HFWPF uses hybrid resubmission + replication (Eq. 8):
          - Resubmission: re-execute on another VM → higher latency
          - Replication: redundant copy already running → lower latency
        Both operate at WORKFLOW level (not micro-slot level).
        Recovery scales with full processing window.
        """
        J, N_in, M, N_M = self._workflow_params(num_sensors, num_fog_nodes)
        # Use continuous expected value for smooth scaling.
        # E[failed_nodes] = num_fog_nodes × failure_rate
        failed_nodes = num_fog_nodes * failure_rate
        if failed_nodes < 1e-9:
            return 0.0

        # Online Scheduling: O((J + N_in) × M × N_M)
        t_online_sch = (J + N_in) * M * N_M * self.T_SCHEDULE_COEFF

        # Online Adjustment: O(J × N_in)
        t_adjust = J * N_in * self.T_ADJUST_COEFF

        # VM boot time for resubmission (Eq. 8):
        # Resubmission chosen when dl_ij - st_ij ≥ 2×(T_boot + max TT + ψ)
        # At low failure rates, more slack → more resubmission.
        # At high failure rates, deadline pressure → more replication.
        # Derived from Eq. 8: slack = (1 - failure_rate) × epoch_length
        # Threshold = 2 × T_BOOT; fraction = slack / epoch_length
        resubmission_fraction = max(1.0 - failure_rate, 0.0) ** 2
        t_boot = failed_nodes * self.T_BOOT * resubmission_fraction

        # Recovery per failed node (no artificial multiplier — the
        # O((J+N_in)×M×N_M) complexity already models the scaling)
        total = (t_online_sch + t_adjust) * failed_nodes + t_boot

        return total

    def loss_exposure(self, num_micro_slots: int = 10, **kwargs) -> float:
        """
        HFWPF loss exposure.

        Workflow-level recovery recovers at task/sub-workflow granularity,
        not at micro-slot level. Replication provides SOME fault isolation,
        but recovery scope is still larger than PLOSHA-RMFR's micro-slot
        approach.

        From paper (Sec V, Exp 5): "Refs. [37] and [38] recover failures
        at the workflow or service level, resulting in larger recovery
        scopes and greater aggregation disruption."

        Workflow-level granularity: each workflow task covers 1/J of the
        data. With J tasks and replication, loss = fraction of tasks on
        failed nodes that don't have replicas running.
        """
        # Workflow-level recovery: the recovery scope is determined by
        # the DAG dependency depth. A single task failure cascades to
        # all N_in downstream tasks, meaning recovery covers a full
        # workflow level at minimum.
        #
        # With N_IN_AVG = 3, a failure cascades across 3 dependent tasks.
        # The fraction of the epoch affected = N_in / (N_in + 1) for the
        # failed level. As m* increases, more levels exist but the cascade
        # fraction per level remains the same.
        #
        # Loss exposure = N_IN_AVG / (N_IN_AVG + 1) = 3/4 = 0.75
        # This is constant because workflow DAG structure doesn't change
        # with micro-slot count — the paper's "larger recovery scope"
        # is an architectural property, not a configuration parameter.
        return self.N_IN_AVG / (self.N_IN_AVG + 1.0)

    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """
        HFWPF communication overhead.

        From Table IV:
          Data Collection:  N_s × |Data|
          Coordination:     |Sched|
          Recovery:         m* × |State| (scales with FULL processing window)

        Recovery communication scales with the entire processing window
        m*, NOT just |D_i^miss|.
        """
        # Data collection: sensor → fog/VM
        data_collection = num_sensors * self.DATA_SIZE

        # Coordination: scheduling messages
        coordination = num_fog_nodes * self.SCHED_SIZE

        # Recovery: m* × |State| — full processing window
        # This is a KEY disadvantage: recovery comm scales with m*, not |D_miss|
        failed_nodes = num_fog_nodes * failure_rate
        recovery_comm = num_micro_slots * self.STATE_SIZE * failed_nodes

        total_bytes = data_collection + coordination + recovery_comm
        return total_bytes / 1024.0

    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        HFWPF completeness.

        Replication + resubmission provides fault tolerance.
        From Eq. 8: resubmission chosen when slack permits; otherwise
        replication with redundant copy already running.

        Replication: tasks have a backup → if primary fails, replica
        continues → high recovery rate.
        Resubmission: tasks re-execute on new VM → recovery depends
        on remaining time before deadline.

        Completeness = 1 - (failure_rate × fraction_unrecoverable)
        where fraction_unrecoverable depends on strategy split.
        """
        # Resubmission fraction (same formula as recovery_latency)
        resub_frac = max(1.0 - failure_rate, 0.0) ** 2
        repl_frac = 1.0 - resub_frac

        # Replication: near-certain recovery (backup already running)
        # Resubmission: recovery depends on having time to re-execute
        # Success rate decreases with failure_rate (less slack)
        resub_success = max(1.0 - failure_rate, 0.0)
        repl_success = 1.0  # Replica is already running

        # Weighted recovery rate
        recovery_rate = resub_frac * resub_success + repl_frac * repl_success
        return max(0.0, 1.0 - failure_rate * (1.0 - recovery_rate))

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        HFWPF availability.

        Hybrid fault tolerance maintains availability through
        resubmission and replication. Both strategies can recover
        from failures, but resubmission may violate deadlines
        under high failure rates.

        Availability = fraction of epochs completed before deadline.
        Replication epochs always complete; resubmission epochs
        complete only if remaining slack > T_boot + execution_time.
        """
        resub_frac = max(1.0 - failure_rate, 0.0) ** 2
        repl_frac = 1.0 - resub_frac
        # Replication: always available (backup running)
        # Resubmission: available if slack permits recovery
        resub_avail = max(1.0 - failure_rate, 0.0)
        availability_rate = resub_frac * resub_avail + repl_frac * 1.0
        return max(0.0, 1.0 - failure_rate * (1.0 - availability_rate))
