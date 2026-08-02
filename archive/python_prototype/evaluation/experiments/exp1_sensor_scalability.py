"""
Experiment 1: Scalability With Number of Sensors
==================================================
Evaluates aggregation scalability under increasing IIoT data volume.

Paper Reference: Section V, Experiment 1 (Fig. 2)
  - X-axis: Number of sensors (500 to 5000)
  - Y-axis: Aggregation latency (seconds)
  - "Number of fog nodes, aggregation epoch length, and network
     settings are fixed"
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
    Run Experiment 1: Aggregation latency vs number of sensors.

    Paper: "The number of sensors is varied from 500 to 5000 while the
           number of fog nodes, aggregation epoch length, and network
           settings are fixed."
    """
    print("=" * 60)
    print("Experiment 1: Scalability With Number of Sensors")
    print("=" * 60)

    sensor_counts = [500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000]
    num_epochs = 20
    num_runs = 3
    fog_nodes = 10
    failure_rate = 0.05

    # --- PLOSHA-RMFR ---
    plosha_latencies = []
    for n_sensors in sensor_counts:
        run_results = []
        for run in range(num_runs):
            config = SystemConfig(
                num_sensors=n_sensors,
                num_fog_nodes=fog_nodes,
                num_epochs=num_epochs,
                failure_rate=failure_rate,
                random_seed=42 + run
            )
            summary = run_single_configuration(config, num_epochs)
            run_results.append(summary['agg_latency']['mean'])
        avg = np.mean(run_results)
        plosha_latencies.append(avg)
        print(f"  Sensors={n_sensors}: PLOSHA-RMFR latency = {avg:.4f}s")

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
        for n_sensors in sensor_counts:
            lat = scheme.aggregation_latency(
                n_sensors, fog_nodes, failure_rate, workload_intensity=1.0
            )
            latencies.append(lat)
        baseline_results[scheme.name] = latencies
        print(f"  {scheme.name} latencies computed.")

    # --- Plot (Fig. 2) ---
    os.makedirs(output_dir, exist_ok=True)

    plt.figure(figsize=(10, 7))
    plt.plot(sensor_counts, plosha_latencies, 'o-', color='#2196F3',
             linewidth=2, markersize=8, label='PLOSHA-RMFR (Ours)')

    for (scheme, marker, color) in baselines:
        plt.plot(sensor_counts, baseline_results[scheme.name],
                 marker, color=color, linewidth=1.5, markersize=6,
                 label=scheme.name)

    plt.xlabel('Number of Sensors', fontsize=13)
    plt.ylabel('Aggregation Latency (s)', fontsize=13)
    plt.title('Experiment 1: Aggregation Latency vs. Number of Sensors', fontsize=14)
    plt.legend(fontsize=11, loc='upper left')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'exp1_latency_sensors.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'sensor_counts': sensor_counts,
        'plosha': plosha_latencies,
        **{k: v for k, v in baseline_results.items()},
    }


if __name__ == "__main__":
    run_experiment()
