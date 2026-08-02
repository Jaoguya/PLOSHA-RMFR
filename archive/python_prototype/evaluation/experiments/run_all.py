"""
Run All Experiments
=====================
Executes all 7 evaluation experiments from the PLOSHA-RMFR paper
and generates comparison plots.

Paper Reference: Section V - Performance Analysis
  Experiment 1: Scalability with number of sensors (Fig. 2)
  Experiment 2: Scalability with number of fog nodes (Fig. 3)
  Experiment 3: Impact of workload intensity (Fig. 4)
  Experiment 4: Impact of failure rate (Fig. 5)
  Experiment 5: Aggregation-loss exposure (Fig. 6)
  Experiment 6: Recovery communication overhead (Fig. 7)
  Experiment 7: AFLTO ablation study (Fig. 8)
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from evaluation.experiments import (
    exp1_sensor_scalability,
    exp2_fog_scalability,
    exp3_workload_intensity,
    exp4_failure_rate,
    exp5_loss_exposure,
    exp6_recovery_comm,
    exp7_aflto_ablation,
)


def run_all(output_dir: str = "results"):
    """Run all 7 experiments and generate plots."""
    os.makedirs(output_dir, exist_ok=True)

    experiments = [
        ("Experiment 1: Sensor Scalability",   exp1_sensor_scalability),
        ("Experiment 2: Fog Scalability",      exp2_fog_scalability),
        ("Experiment 3: Workload Intensity",   exp3_workload_intensity),
        ("Experiment 4: Failure Rate",         exp4_failure_rate),
        ("Experiment 5: Loss Exposure",        exp5_loss_exposure),
        ("Experiment 6: Recovery Comm",        exp6_recovery_comm),
        ("Experiment 7: AFLTO Ablation",       exp7_aflto_ablation),
    ]

    results = {}
    total_start = time.time()

    for name, module in experiments:
        print(f"\n{'#' * 60}")
        print(f"# Running: {name}")
        print(f"{'#' * 60}")

        exp_start = time.time()
        try:
            result = module.run_experiment(output_dir=output_dir)
            results[name] = result
            elapsed = time.time() - exp_start
            print(f"\n  ✓ {name} completed in {elapsed:.1f}s")
        except Exception as e:
            elapsed = time.time() - exp_start
            print(f"\n  ✗ {name} failed after {elapsed:.1f}s: {e}")
            results[name] = {'error': str(e)}

    total_elapsed = time.time() - total_start
    print(f"\n{'=' * 60}")
    print(f"All experiments completed in {total_elapsed:.1f}s")
    print(f"Plots saved to: {os.path.abspath(output_dir)}/")
    print(f"{'=' * 60}")

    return results


if __name__ == "__main__":
    output = "results"
    if len(sys.argv) > 1:
        output = sys.argv[1]
    run_all(output_dir=output)
