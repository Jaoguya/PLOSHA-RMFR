"""
Experiment 2: Scalability With Number of Fog Nodes
=====================================================
Evaluates aggregation latency as the number of fog nodes increases.

Paper Reference: Section V, Experiment 2 (Fig. 3)
  - X-axis: Number of fog nodes (5 to 50)
  - Y-axis: Average aggregation latency per epoch
  - Fixed number of sensors and aggregation workload
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
    Run Experiment 2: Aggregation latency vs number of fog nodes.

    Paper: "The number of fog nodes is varied from 5 to 50 while
           maintaining a fixed number of sensors and aggregation workload."
    """
    print("=" * 60)
    print("Experiment 2: Scalability With Number of Fog Nodes")
    print("=" * 60)

    fog_counts = [5, 10, 15, 20, 25, 30, 35, 40, 45, 50]
    num_sensors = 2000
    num_epochs = 20
    num_runs = 3
    failure_rate = 0.05

    # --- PLOSHA-RMFR ---
    plosha_latencies = []
    for n_fogs in fog_counts:
        run_results = []
        for run in range(num_runs):
            config = SystemConfig(
                num_sensors=num_sensors,
                num_fog_nodes=n_fogs,
                num_epochs=num_epochs,
                failure_rate=failure_rate,
                random_seed=42 + run
            )
            summary = run_single_configuration(config, num_epochs)
            run_results.append(summary['agg_latency']['mean'])
        avg = np.mean(run_results)
        plosha_latencies.append(avg)
        print(f"  Fog Nodes={n_fogs}: PLOSHA-RMFR latency = {avg:.4f}s")

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
        for n_fogs in fog_counts:
            lat = scheme.aggregation_latency(
                num_sensors, n_fogs, failure_rate, workload_intensity=1.0
            )
            latencies.append(lat)
        baseline_results[scheme.name] = latencies
        print(f"  {scheme.name} latencies computed.")

    # --- Plot (Fig. 3) ---
    os.makedirs(output_dir, exist_ok=True)

    plt.figure(figsize=(10, 7))
    plt.plot(fog_counts, plosha_latencies, 'o-', color='#2196F3',
             linewidth=2, markersize=8, label='PLOSHA-RMFR (Ours)')

    for (scheme, marker, color) in baselines:
        plt.plot(fog_counts, baseline_results[scheme.name],
                 marker, color=color, linewidth=1.5, markersize=6,
                 label=scheme.name)

    plt.xlabel('Number of Fog Nodes', fontsize=13)
    plt.ylabel('Aggregation Latency (s)', fontsize=13)
    plt.title('Experiment 2: Aggregation Latency vs. Number of Fog Nodes', fontsize=14)
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'exp2_fog_scalability.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'fog_counts': fog_counts,
        'plosha': plosha_latencies,
        **{k: v for k, v in baseline_results.items()},
    }


if __name__ == "__main__":
    run_experiment()
