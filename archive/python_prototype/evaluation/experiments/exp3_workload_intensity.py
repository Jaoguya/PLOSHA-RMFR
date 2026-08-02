"""
Experiment 3: Impact of Workload Intensity
============================================
Evaluates robustness under varying workload conditions.

Paper Reference: Section V, Experiment 3 (Fig. 4)
  - X-axis: Workload intensity (Low, Medium, High)
  - Y-axis: Aggregation latency
  - "Sensor reporting rate is gradually increased by reducing the
     reporting interval"
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from src.config import SystemConfig
from evaluation.simulator import run_single_configuration
from evaluation.baselines import Ref22Scheme, Ref24Scheme, Ref37Scheme, Ref38Scheme


def run_experiment(output_dir: str = "results"):
    """
    Run Experiment 3: Impact of workload intensity on aggregation latency.

    Paper: "The sensor reporting rate is gradually increased by reducing
           the reporting interval, thereby generating low, medium, and
           high workload scenarios."
    """
    print("=" * 60)
    print("Experiment 3: Impact of Workload Intensity")
    print("=" * 60)

    workload_levels = [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0]
    workload_labels = ['0.5x', '0.75x', '1.0x', '1.25x', '1.5x',
                       '1.75x', '2.0x', '2.5x', '3.0x']
    num_epochs = 20
    num_runs = 3
    num_sensors = 1000
    fog_nodes = 10
    failure_rate = 0.05

    # --- PLOSHA-RMFR ---
    plosha_latencies = []
    for intensity in workload_levels:
        run_results = []
        # Scale effective sensor count to reflect workload intensity.
        # The framework processes all assigned sensors per epoch, so
        # varying num_sensors is the correct way to increase processing
        # load. Baselines compute N_s = int(num_sensors * workload_intensity),
        # so effective_sensors with workload_intensity=1.0 gives the
        # same result.
        effective_sensors = max(100, int(num_sensors * intensity))
        for run in range(num_runs):
            config = SystemConfig(
                num_sensors=effective_sensors,
                num_fog_nodes=fog_nodes,
                num_epochs=num_epochs,
                workload_intensity=1.0,
                failure_rate=failure_rate,
                random_seed=42 + run
            )
            summary = run_single_configuration(config, num_epochs)
            run_results.append(summary['agg_latency']['mean'])
        avg = np.mean(run_results)
        plosha_latencies.append(avg)
        print(f"  Workload={intensity}x: PLOSHA-RMFR latency = {avg:.4f}s")

    # --- Baselines ---
    baselines = [
        (Ref22Scheme(), 's--', '#E91E63'),
        (Ref24Scheme(), '^--', '#FF9800'),
        (Ref37Scheme(), 'D--', '#9C27B0'),
        (Ref38Scheme(), 'v--', '#4CAF50'),
    ]

    baseline_results = {}
    for scheme, _, _ in baselines:
        latencies = []
        for intensity in workload_levels:
            # Use same effective sensor count as PLOSHA for consistency
            effective_sensors = max(100, int(num_sensors * intensity))
            lat = scheme.aggregation_latency(
                effective_sensors, fog_nodes, failure_rate,
                workload_intensity=1.0
            )
            latencies.append(lat)
        baseline_results[scheme.name] = latencies
        print(f"  {scheme.name} latencies computed.")

    # --- Plot (Fig. 4) ---
    os.makedirs(output_dir, exist_ok=True)

    plt.figure(figsize=(10, 7))
    x_pos = range(len(workload_levels))

    plt.plot(x_pos, plosha_latencies, 'o-', color='#2196F3',
             linewidth=2, markersize=8, label='PLOSHA-RMFR (Ours)')

    for (scheme, marker, color) in baselines:
        plt.plot(x_pos, baseline_results[scheme.name],
                 marker, color=color, linewidth=1.5, markersize=6,
                 label=scheme.name)

    plt.xticks(x_pos, workload_labels)
    plt.xlabel('Workload Intensity', fontsize=13)
    plt.ylabel('Aggregation Latency (s)', fontsize=13)
    plt.title('Experiment 3: Impact of Workload Intensity on Aggregation Latency',
              fontsize=14)
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'exp3_workload_intensity.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'workload_levels': workload_levels,
        'plosha': plosha_latencies,
        **{k: v for k, v in baseline_results.items()},
    }


if __name__ == "__main__":
    run_experiment()
