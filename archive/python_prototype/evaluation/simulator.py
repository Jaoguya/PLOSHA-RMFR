"""
Simulation Engine
==================
Provides the main simulation runner for PLOSHA-RMFR evaluation experiments.

Handles:
  - Configurable system scale (sensors, fog nodes)
  - Workload generation with adjustable intensity
  - Failure injection at configurable rates
  - Multi-run averaging for statistical significance
  - Metric collection per experiment configuration

Paper Reference: Section V - Experimental Setup
  "Sensors continuously generate time-series industrial measurements"
  "Number of sensors: 500 to 5000"
  "Number of fog nodes: 5 to 50"
  "Failure rate: 2% to 20%"
  "Each experiment is repeated multiple times, and the average value is reported"
"""

import copy
import dataclasses
from typing import List, Dict, Optional

from src.config import SystemConfig
from src.framework import PLOSHARMFRFramework
from evaluation.metrics import MetricsCollector


def run_single_configuration(config: SystemConfig,
                              num_epochs: int = 20,
                              failure_injection: bool = True,
                              static_aflto: bool = False,
                              dataset_path: Optional[str] = None) -> dict:
    """
    Run a single simulation configuration and return aggregated metrics.

    Args:
        config: System configuration for this run.
        num_epochs: Number of epochs to simulate.
        failure_injection: Whether to inject fog-node failures.
        static_aflto: Whether to disable AFLTO (for ablation study).
        dataset_path: Path to the dataset for workload simulation.

    Returns:
        Dictionary with aggregated metrics.
    """
    framework = PLOSHARMFRFramework(
        config=config,
        use_real_crypto=False,
        static_aflto=static_aflto,
        dataset_path=dataset_path
    )
    framework.initialize()

    collector = MetricsCollector()

    for _ in range(num_epochs):
        epoch_metrics = framework.run_epoch(failure_injection=failure_injection)
        collector.record_epoch(epoch_metrics)

    summary = collector.get_summary()
    summary['epoch_history'] = framework.epoch_metrics
    return summary


def run_experiment_sweep(base_config: SystemConfig,
                          param_name: str,
                          param_values: list,
                          num_epochs: int = 20,
                          num_runs: int = 3,
                          failure_injection: bool = True,
                          static_aflto: bool = False,
                          dataset_path: Optional[str] = None) -> Dict[str, list]:
    """
    Run a parameter sweep experiment with multi-run averaging.

    Args:
        base_config: Base configuration to modify.
        param_name: Configuration field to sweep.
        param_values: List of values for the swept parameter.
        num_epochs: Epochs per run.
        num_runs: Number of runs per parameter value (for averaging).
        failure_injection: Whether to inject failures.
        static_aflto: Whether to disable AFLTO.
        dataset_path: Path to the dataset for workload simulation.

    Returns:
        Dictionary with parameter values and corresponding metric lists.
    """
    results = {
        'param_values': list(param_values),
        'agg_latency': [],
        'recovery_latency': [],
        'completeness': [],
        'loss_exposure': [],
        'comm_overhead_kb': [],
        'availability': [],
    }

    for val in param_values:
        run_latencies = []
        run_rec_latencies = []
        run_completeness = []
        run_loss = []
        run_comm = []
        run_avail = []

        for run in range(num_runs):
            config = copy.deepcopy(base_config)
            setattr(config, param_name, val)
            config.random_seed = base_config.random_seed + run

            try:
                config.validate()
            except AssertionError:
                pass  # Some sweeps may temporarily violate constraints

            summary = run_single_configuration(
                config, num_epochs, failure_injection, static_aflto, dataset_path=dataset_path)

            run_latencies.append(summary['agg_latency']['mean'])
            run_rec_latencies.append(summary['recovery_latency']['mean'])
            run_completeness.append(summary['completeness']['mean'])
            run_loss.append(summary['loss_exposure']['mean'])
            run_comm.append(summary['comm_overhead_kb']['mean'])
            run_avail.append(summary['availability']['mean'])

        # Average across runs
        results['agg_latency'].append(
            sum(run_latencies) / len(run_latencies))
        results['recovery_latency'].append(
            sum(run_rec_latencies) / len(run_rec_latencies))
        results['completeness'].append(
            sum(run_completeness) / len(run_completeness))
        results['loss_exposure'].append(
            sum(run_loss) / len(run_loss))
        results['comm_overhead_kb'].append(
            sum(run_comm) / len(run_comm))
        results['availability'].append(
            sum(run_avail) / len(run_avail))

    return results
