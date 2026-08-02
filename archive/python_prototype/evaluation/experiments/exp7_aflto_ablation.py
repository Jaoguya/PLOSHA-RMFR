"""
Experiment 7: Effectiveness of AFLTO (Ablation Study)
=======================================================
Evaluates the contribution of the AFLTO mechanism via ablation.

Paper Reference: Section V, Experiment 7 (Fig. 8)
  - X-axis: Epoch index (time)
  - Y-axis: Aggregation completeness and system availability
  - Compare: PLOSHA-RMFR (full) vs PLOSHA-RMFR (AFLTO disabled)
  - "An ablation study is conducted by comparing the complete PLOSHA-RMFR
     framework with a variant in which AFLTO is disabled and all thresholds
     remain static throughout system operation."
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from src.config import SystemConfig
from src.framework import PLOSHARMFRFramework


def run_experiment(output_dir: str = "results"):
    """
    Run Experiment 7: AFLTO ablation study.

    Paper: "An ablation study comparing the complete PLOSHA-RMFR framework
           with a variant in which AFLTO is disabled."
           "The workload intensity and fog-node failure rate are
            dynamically varied over time to emulate realistic IIoT
            environments with changing operational conditions."
    """
    print("=" * 60)
    print("Experiment 7: Effectiveness of AFLTO (Ablation Study)")
    print("=" * 60)

    num_epochs = 60
    num_runs = 3

    # Results accumulators
    full_completeness = np.zeros(num_epochs)
    full_availability = np.zeros(num_epochs)
    static_completeness = np.zeros(num_epochs)
    static_availability = np.zeros(num_epochs)

    for run in range(num_runs):
        # --- Full PLOSHA-RMFR (with AFLTO) ---
        config_full = SystemConfig(
            num_sensors=500,
            num_fog_nodes=10,
            num_epochs=num_epochs,
            failure_rate=0.05,
            random_seed=42 + run
        )
        fw_full = PLOSHARMFRFramework(config=config_full, static_aflto=False)
        fw_full.initialize()

        for e in range(num_epochs):
            # Dynamic conditions: vary failure rate over time
            if e < 20:
                fw_full.config.failure_rate = 0.05
                fw_full.config.workload_intensity = 1.0
            elif e < 40:
                fw_full.config.failure_rate = 0.15   # Stress period
                fw_full.config.workload_intensity = 2.0
            else:
                fw_full.config.failure_rate = 0.08   # Recovery period
                fw_full.config.workload_intensity = 1.2

            metrics = fw_full.run_epoch()
            full_completeness[e] += metrics['completeness']
            full_availability[e] += metrics['availability']

        # --- Static Thresholds (AFLTO disabled) ---
        config_static = SystemConfig(
            num_sensors=500,
            num_fog_nodes=10,
            num_epochs=num_epochs,
            failure_rate=0.05,
            random_seed=42 + run
        )
        fw_static = PLOSHARMFRFramework(config=config_static, static_aflto=True)
        fw_static.initialize()

        for e in range(num_epochs):
            if e < 20:
                fw_static.config.failure_rate = 0.05
                fw_static.config.workload_intensity = 1.0
            elif e < 40:
                fw_static.config.failure_rate = 0.15
                fw_static.config.workload_intensity = 2.0
            else:
                fw_static.config.failure_rate = 0.08
                fw_static.config.workload_intensity = 1.2

            metrics = fw_static.run_epoch()
            static_completeness[e] += metrics['completeness']
            static_availability[e] += metrics['availability']

    # Average across runs
    full_completeness /= num_runs
    full_availability /= num_runs
    static_completeness /= num_runs
    static_availability /= num_runs

    # Print summary
    for phase, start, end in [("Stable", 0, 20), ("Stress", 20, 40), ("Recovery", 40, 60)]:
        fc = np.mean(full_completeness[start:end])
        sc = np.mean(static_completeness[start:end])
        fa = np.mean(full_availability[start:end])
        sa = np.mean(static_availability[start:end])
        print(f"  {phase}: Full AFLTO comp={fc:.3f}, avail={fa:.3f} | "
              f"Static comp={sc:.3f}, avail={sa:.3f}")

    # Plot results (Fig. 8)
    os.makedirs(output_dir, exist_ok=True)
    epochs = range(num_epochs)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=True)

    # Completeness subplot
    ax1.plot(epochs, full_completeness, '-', color='#2196F3',
             linewidth=2, label='PLOSHA-RMFR (Full)')
    ax1.plot(epochs, static_completeness, '--', color='#F44336',
             linewidth=2, label='PLOSHA-RMFR (Static Thresholds)')
    ax1.axvspan(20, 40, alpha=0.1, color='red', label='Stress Period')
    ax1.set_ylabel('Aggregation Completeness', fontsize=13)
    ax1.set_title('Experiment 7: AFLTO Ablation Study', fontsize=14)
    ax1.legend(fontsize=11, loc='lower left')
    ax1.grid(True, alpha=0.3)
    ax1.set_ylim(0.5, 1.05)

    # Availability subplot
    ax2.plot(epochs, full_availability, '-', color='#2196F3',
             linewidth=2, label='PLOSHA-RMFR (Full)')
    ax2.plot(epochs, static_availability, '--', color='#F44336',
             linewidth=2, label='PLOSHA-RMFR (Static Thresholds)')
    ax2.axvspan(20, 40, alpha=0.1, color='red', label='Stress Period')
    ax2.set_xlabel('Epoch', fontsize=13)
    ax2.set_ylabel('System Availability', fontsize=13)
    ax2.legend(fontsize=11, loc='lower left')
    ax2.grid(True, alpha=0.3)
    ax2.set_ylim(0.3, 1.05)

    plt.tight_layout()
    filepath = os.path.join(output_dir, 'exp7_aflto_ablation.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'epochs': list(epochs),
        'full_completeness': full_completeness.tolist(),
        'full_availability': full_availability.tolist(),
        'static_completeness': static_completeness.tolist(),
        'static_availability': static_availability.tolist()
    }


if __name__ == "__main__":
    run_experiment()
