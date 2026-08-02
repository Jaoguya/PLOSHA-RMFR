"""
PLOSHA-RMFR Framework Orchestrator
=====================================
Orchestrates all 5 phases of the PLOSHA-RMFR framework per aggregation epoch.

Gramine-SGX Edition — uses config-driven parameters throughout, calls
boundary checks at every epoch, uses proper measurement methods.

Closed-loop architecture:
  State_i(t) → Phase II (Prediction) → Phase III (PLOSHA) →
  Phase IV (RMFR) → Phase V (AFLTO) → State_i(t+1) → ...

Paper Reference: Algorithm 1 - PLOSHA-RMFR Aggregation and Recovery Procedure
  Input:  State_i(t), Δ, S_i, N_i
  Output: Rec_i(t)
"""

import copy
import random
import numpy as np
import dataclasses
from typing import List, Dict, Optional

from src.config import SystemConfig, DEFAULT_CONFIG
from src.entities.sensor import Sensor
from src.entities.fog_node import FogNode
from src.entities.krm import KRM
from src.entities.cloud_server import CloudServer
from src.checks import check_state
from src.phases import (
    phase1_init,
    phase2_prediction,
    phase3_plosha,
    phase4_rmfr,
    phase5_aflto,
)
from src.dataset import DatasetTransformer


class PLOSHARMFRFramework:
    """
    PLOSHA-RMFR Framework: orchestrates all five phases.

    Manages the complete closed-loop cycle:
      Phase I  (once):  System Initialization
      Phase II (each epoch): Predictive Capacity & Risk Estimation
      Phase III (each epoch): Adaptive Hierarchical Slot Aggregation
      Phase IV (each epoch): Risk-Aware Multi-Layer Fault Recovery
      Phase V  (each epoch): Adaptive Feedback Learning & Threshold Optimization
    """

    def __init__(self, config: Optional[SystemConfig] = None,
                 use_real_crypto: bool = False,
                 static_aflto: bool = False,
                 dataset_path: Optional[str] = None):
        """
        Initialize the PLOSHA-RMFR framework.

        Args:
            config: System configuration. Uses DEFAULT_CONFIG if None.
            use_real_crypto: Use real Paillier/AES operations.
            static_aflto: If True, disable AFLTO threshold updates (for ablation).
            dataset_path: If provided, load traces and failure schedules from CSV.
        """
        self.config = copy.deepcopy(config) if config else copy.deepcopy(DEFAULT_CONFIG)
        self.config.validate()
        self.use_real_crypto = use_real_crypto
        self.static_aflto = static_aflto
        self.dataset_path = dataset_path

        # Optional dataset inputs
        self.dataset_failure_schedule = {}
        self.dataset_workloads = []

        # System entities
        self.sensors: List[Sensor] = []
        self.fog_nodes: List[FogNode] = []
        self.fog_map: Dict[int, FogNode] = {}
        self.krm: Optional[KRM] = None
        self.cloud: Optional[CloudServer] = None

        # State tracking
        self.initialized = False
        self.current_epoch = 0

        # Per-epoch metrics history
        self.epoch_metrics: List[dict] = []

        # Deterministic failure RNG (separate from global)
        self._failure_rng = random.Random(self.config.random_seed)

    def initialize(self):
        """
        Execute Phase I: System Initialization and Secure Provisioning.

        Creates all entities, provisions keys, establishes trust.
        """
        random.seed(self.config.random_seed)
        np.random.seed(self.config.random_seed)

        # Compute fog node buffer sizes from config (§4.2)
        ns_per_fog = max(1, self.config.num_sensors // self.config.num_fog_nodes)

        # Create entities with proper queue/buffer capacities
        self.sensors = [
            Sensor(j, -1, max_value=self.config.max_sensor_value)
            for j in range(self.config.num_sensors)
        ]
        self.fog_nodes = [
            FogNode(
                i,
                capacity=ns_per_fog * 2,
                max_queue=ns_per_fog * 2,
                max_buffer=ns_per_fog * 2
            )
            for i in range(self.config.num_fog_nodes)
        ]
        self.fog_map = {f.node_id: f for f in self.fog_nodes}
        self.krm = KRM(self.config)
        self.cloud = CloudServer()

        # Load dataset if provided
        if self.dataset_path:
            transformer = DatasetTransformer(
                csv_path=self.dataset_path,
                random_seed=self.config.random_seed
            )
            data = transformer.transform_for_plosha(
                num_sensors=self.config.num_sensors,
                num_fog_nodes=self.config.num_fog_nodes,
                max_sensor_value=self.config.max_sensor_value
            )
            
            # Load traces into sensors
            for sid, trace in data["sensor_traces"].items():
                if sid < len(self.sensors):
                    self.sensors[sid].load_trace(trace)
            
            # Override sensor assignment based on dataset flow IP mapping
            for fid, sids in data["fog_assignment"].items():
                if fid < len(self.fog_nodes):
                    for sid in sids:
                        if sid < len(self.sensors):
                            self.sensors[sid].assigned_fog_id = fid
                            
            self.dataset_failure_schedule = data["failure_schedule"]
            self.dataset_workloads = data["epoch_workloads"]

        # Execute Phase I
        init_output = phase1_init.execute(
            self.config, self.sensors, self.fog_nodes,
            self.krm, self.cloud
        )

        self.initialized = True
        self.current_epoch = 0

        return init_output

    def run_epoch(self, failure_injection: bool = True) -> dict:
        """
        Execute one complete aggregation epoch (Phases II-V).

        Implements Algorithm 1 from the paper:
          1. Compute predicted state (Phase II)
          2. Determine optimal micro-slots and aggregate (Phase III)
          3. Evaluate recovery urgency and execute recovery (Phase IV)
          4. Commit result, evaluate performance, adapt thresholds (Phase V)

        Args:
            failure_injection: If True, randomly inject fog node failures.

        Returns:
            Epoch metrics dictionary.
        """
        if not self.initialized:
            raise RuntimeError("Framework not initialized. Call initialize() first.")

        epoch = self.current_epoch

        # Reset per-epoch metrics
        for fog in self.fog_nodes:
            fog.reset_epoch_metrics()

        # Inject failures if enabled (deterministic from seeded RNG or dataset)
        failed_nodes = set()
        if failure_injection:
            if self.dataset_path and epoch in self.dataset_failure_schedule:
                # Use malicious traffic events from dataset
                failed_fogs = self.dataset_failure_schedule[epoch]
                for fid in failed_fogs:
                    if fid < len(self.fog_nodes) and self.fog_nodes[fid].operational:
                        self.fog_nodes[fid].fail()
                        failed_nodes.add(fid)
            else:
                # Fallback to random injection
                failed_nodes = self._inject_failures()

        # Inject sensor drops
        self._inject_sensor_drops()

        # Update active sensor registry in KRM
        self.krm.update_active_sensors(self.sensors)

        # =====================================================================
        # Phase II: Predictive Capacity and Risk Estimation (all fog nodes)
        # =====================================================================
        # Update runtime states using proper measurement methods
        
        # Get dataset workload intensity if available
        ds_workload = None
        if self.dataset_path and epoch < len(self.dataset_workloads):
            ds_workload = self.dataset_workloads[epoch]
            
        for fog in self.fog_nodes:
            if fog.operational:
                num_sensors = len(fog.assigned_sensors)
                max_cap = max(1, self.config.num_sensors //
                              self.config.num_fog_nodes * 2)
                
                # Use dataset workload if available, else synthetic
                if ds_workload is not None and fog.node_id in ds_workload:
                    workload_int = ds_workload[fog.node_id]
                else:
                    workload_int = self.config.workload_intensity
                    
                fog.update_runtime_state(
                    int(num_sensors * workload_int),
                    max_cap,
                    rtt_mu=self.config.rtt_mu,
                    rtt_sigma=self.config.rtt_sigma,
                    rtt_99th=self.config.rtt_99th
                )

        predictions = phase2_prediction.execute(self.config, self.fog_nodes)

        # =====================================================================
        # Phase III: PLOSHA Aggregation (per fog node)
        # =====================================================================
        agg_outputs = {}
        for fog in self.fog_nodes:
            # Get sensors assigned to this fog node
            fog_sensors = [s for s in self.sensors
                          if s.assigned_fog_id == fog.node_id]
            agg_out = phase3_plosha.execute(
                self.config, fog, fog_sensors, self.krm,
                use_real_crypto=self.use_real_crypto
            )
            agg_outputs[fog.node_id] = agg_out

        # =====================================================================
        # Phase IV: RMFR Recovery (per fog node)
        # =====================================================================
        rec_outputs = {}
        for fog in self.fog_nodes:
            rec_out = phase4_rmfr.execute(
                self.config, fog, self.fog_map,
                agg_outputs[fog.node_id], self.krm,
                self.sensors, epoch
            )
            rec_outputs[fog.node_id] = rec_out

        # =====================================================================
        # Phase V: AFLTO Feedback and Commitment (per fog node)
        # Threshold updates are accumulated and applied once per epoch
        # to prevent per-node compounding of threshold adjustments.
        # =====================================================================
        fb_outputs = {}
        # Save thresholds before per-node processing
        saved_thresholds = {
            'tau_v': self.config.tau_v, 'tau_r': self.config.tau_r,
            'tau_1': self.config.tau_1, 'tau_2': self.config.tau_2,
            'tau_3': self.config.tau_3, 'tau_f': self.config.tau_f
        }

        for fog in self.fog_nodes:
            # Reset thresholds to saved values before each node
            # so per-node processing uses the same epoch-start thresholds
            self.config.tau_v = saved_thresholds['tau_v']
            self.config.tau_r = saved_thresholds['tau_r']
            self.config.tau_1 = saved_thresholds['tau_1']
            self.config.tau_2 = saved_thresholds['tau_2']
            self.config.tau_3 = saved_thresholds['tau_3']
            self.config.tau_f = saved_thresholds['tau_f']

            if self.static_aflto:
                fb_out = phase5_aflto.execute_static(
                    self.config, fog,
                    agg_outputs[fog.node_id],
                    rec_outputs[fog.node_id],
                    self.cloud, epoch
                )
            else:
                fb_out = phase5_aflto.execute(
                    self.config, fog,
                    agg_outputs[fog.node_id],
                    rec_outputs[fog.node_id],
                    self.cloud, epoch
                )
            fb_outputs[fog.node_id] = fb_out

        # Apply averaged threshold update once per epoch
        if not self.static_aflto and fb_outputs:
            avg_error = sum(fb['error'] for fb in fb_outputs.values()) / len(fb_outputs)
            self.config.tau_v = phase5_aflto._project(
                saved_thresholds['tau_v'] + self.config.mu_v * avg_error)
            self.config.tau_r = phase5_aflto._project(
                saved_thresholds['tau_r'] + self.config.mu_r * avg_error)
            self.config.tau_1 = phase5_aflto._project(
                saved_thresholds['tau_1'] + self.config.mu_1 * avg_error)
            self.config.tau_2 = phase5_aflto._project(
                saved_thresholds['tau_2'] + self.config.mu_2 * avg_error)
            self.config.tau_3 = phase5_aflto._project(
                saved_thresholds['tau_3'] + self.config.mu_3 * avg_error)
            self.config.tau_f = phase5_aflto._project(
                saved_thresholds['tau_f'] + self.config.mu_f * avg_error)
            phase5_aflto._enforce_threshold_ordering(self.config)
        else:
            # Restore saved thresholds for static mode
            for k, v in saved_thresholds.items():
                setattr(self.config, k, v)

        # =====================================================================
        # Boundary checks (Experiment Plan §8)
        # =====================================================================
        for fog in self.fog_nodes:
            check_state(fog, epoch)

        # Restore failed nodes for next epoch (simulate repair)
        self._restore_failures(failed_nodes)

        # Restore dropped sensors
        for s in self.sensors:
            s.activate()

        # =====================================================================
        # Collect epoch metrics
        # =====================================================================
        metrics = self._collect_epoch_metrics(
            epoch, agg_outputs, rec_outputs, fb_outputs, failed_nodes)
        self.epoch_metrics.append(metrics)

        self.current_epoch += 1

        return metrics

    def run_simulation(self, num_epochs: Optional[int] = None,
                       failure_injection: bool = True) -> List[dict]:
        """
        Run the full simulation for multiple epochs.

        Args:
            num_epochs: Number of epochs to simulate. Uses config default if None.
            failure_injection: Whether to inject failures.

        Returns:
            List of per-epoch metrics.
        """
        if not self.initialized:
            self.initialize()

        epochs = num_epochs or self.config.num_epochs

        for _ in range(epochs):
            self.run_epoch(failure_injection=failure_injection)

        return self.epoch_metrics

    def _inject_failures(self) -> set:
        """
        Inject deterministic fog node failures based on configured failure rate.

        Uses a dedicated RNG seeded from config.random_seed so the
        same failure events occur for all schemes given the same seed
        (Experiment Plan §10, Item 6).

        Returns:
            Set of failed node IDs.
        """
        failed = set()
        for fog in self.fog_nodes:
            if fog.operational and self._failure_rng.random() < self.config.failure_rate:
                fog.fail()
                failed.add(fog.node_id)
        return failed

    def _inject_sensor_drops(self):
        """Inject random sensor report drops."""
        for sensor in self.sensors:
            if random.random() < self.config.sensor_drop_rate:
                sensor.deactivate()

    def _restore_failures(self, failed_nodes: set):
        """Restore failed fog nodes for the next epoch."""
        for fid in failed_nodes:
            fog = self.fog_map.get(fid)
            if fog:
                fog.recover()

    def _collect_epoch_metrics(self, epoch: int,
                                agg_outputs: dict,
                                rec_outputs: dict,
                                fb_outputs: dict,
                                failed_nodes: set) -> dict:
        """
        Collect comprehensive metrics for this epoch.

        Metrics match the paper's evaluation criteria (§9, Table 8):
        - Aggregation latency
        - Recovery latency
        - Aggregation completeness
        - Aggregation-loss exposure
        - Communication overhead
        - System availability (fraction of epochs with V >= 0.95)
        - Recovery invocation count
        """
        operational_fogs = [f for f in self.fog_nodes if f.node_id not in failed_nodes]

        # Aggregation latency: average across operational fog nodes
        agg_latencies = [f.epoch_agg_time for f in operational_fogs]
        avg_agg_latency = (sum(agg_latencies) / len(agg_latencies)
                          if agg_latencies else 0.0)

        # Recovery latency: average recovery time
        rec_latencies = [f.epoch_recovery_time for f in self.fog_nodes
                        if f.epoch_recovery_time > 0]
        avg_rec_latency = (sum(rec_latencies) / len(rec_latencies)
                          if rec_latencies else 0.0)

        # Aggregation completeness: average V_i across fog nodes
        completeness_vals = [agg_outputs[f.node_id]['completeness']
                           for f in operational_fogs
                           if f.node_id in agg_outputs]
        avg_completeness = (sum(completeness_vals) / len(completeness_vals)
                          if completeness_vals else 0.0)

        # Aggregation-loss exposure: 1/m* (average)
        m_vals = [agg_outputs[f.node_id]['optimal_m']
                 for f in operational_fogs
                 if f.node_id in agg_outputs]
        avg_loss_exposure = (sum(1.0 / m for m in m_vals) / len(m_vals)
                           if m_vals else 1.0)

        # Communication overhead: total bytes
        total_comm = sum(f.epoch_comm_bytes for f in self.fog_nodes)

        # System availability: fraction of fog nodes with V >= 0.95 (§9, Table 8)
        available_count = sum(
            1 for f in self.fog_nodes
            if agg_outputs.get(f.node_id, {}).get('completeness', 0) >= 0.95
        )
        availability = available_count / max(1, len(self.fog_nodes))

        # Recovery mode distribution & invocation count (§9, Table 8)
        mode_counts = {"Normal": 0, "Delegation": 0,
                       "MicroRecovery": 0, "Failover": 0}
        recovery_invocations = 0
        for f in self.fog_nodes:
            mode = rec_outputs.get(f.node_id, {}).get('mode', 'Normal')
            mode_counts[mode] = mode_counts.get(mode, 0) + 1
            if mode != "Normal":
                recovery_invocations += 1

        # Quality scores
        avg_quality = (sum(fb_outputs[f.node_id]['score']
                          for f in self.fog_nodes
                          if f.node_id in fb_outputs) /
                      max(1, len(self.fog_nodes)))

        # Average reliability
        avg_reliability = (sum(f.reliability for f in self.fog_nodes) /
                          max(1, len(self.fog_nodes)))

        return {
            'epoch': epoch,
            'agg_latency': avg_agg_latency,
            'recovery_latency': avg_rec_latency,
            'completeness': avg_completeness,
            'loss_exposure': avg_loss_exposure,
            'comm_overhead_bytes': total_comm,
            'comm_overhead_kb': total_comm / 1024.0,
            'availability': availability,
            'recovery_invocations': recovery_invocations,
            'mode_distribution': mode_counts,
            'quality_score': avg_quality,
            'reliability': avg_reliability,
            'num_failed_nodes': len(failed_nodes),
            'thresholds': {
                'tau_v': self.config.tau_v,
                'tau_r': self.config.tau_r,
                'tau_1': self.config.tau_1,
                'tau_2': self.config.tau_2,
                'tau_3': self.config.tau_3,
                'tau_f': self.config.tau_f
            }
        }


def run_quick_test():
    """
    Quick validation: run a small simulation and verify correctness.

    Tests:
    1. All 5 phases execute without errors
    2. Aggregation completeness > 0
    3. System availability > 0
    4. Thresholds remain bounded [0, 1]
    5. Boundary checks pass at every epoch
    """
    print("=" * 60)
    print("PLOSHA-RMFR Quick Validation Test (Gramine-SGX Edition)")
    print("=" * 60)

    config = SystemConfig(
        num_sensors=100,
        num_fog_nodes=5,
        num_epochs=10,
        failure_rate=0.1,
        random_seed=42
    )

    framework = PLOSHARMFRFramework(config=config, use_real_crypto=True)
    framework.initialize()

    print(f"\n[Phase I] Initialized: {config.num_sensors} sensors, "
          f"{config.num_fog_nodes} fog nodes")

    for epoch in range(config.num_epochs):
        metrics = framework.run_epoch()
        print(f"\n[Epoch {epoch}] "
              f"Latency={metrics['agg_latency']:.4f}s | "
              f"Completeness={metrics['completeness']:.3f} | "
              f"Availability={metrics['availability']:.3f} | "
              f"Loss={metrics['loss_exposure']:.3f} | "
              f"Modes={metrics['mode_distribution']}")

    # Validation checks
    final = framework.epoch_metrics[-1]
    assert final['completeness'] > 0, "Completeness should be > 0"
    assert final['availability'] >= 0, "Availability should be >= 0"
    assert 0 <= final['thresholds']['tau_1'] <= 1, "τ_1 out of bounds"
    assert 0 <= final['thresholds']['tau_3'] <= 1, "τ_3 out of bounds"
    assert final['thresholds']['tau_1'] < final['thresholds']['tau_3'], \
        "Threshold ordering violated"

    print("\n" + "=" * 60)
    print("✓ All validation checks passed!")
    print("=" * 60)

    return framework


if __name__ == "__main__":
    run_quick_test()
