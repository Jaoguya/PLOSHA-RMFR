"""
Experiment 4: Impact of Failure Rate
=======================================
Evaluates fault resilience under increasing fog-node failures.

Paper Reference: Section V, Experiment 4 (Fig. 5)
  - X-axis: Failure rate (2% to 20%)
  - Y-axis: Recovery latency
  - "The failure rate is varied from 2% to 20% by randomly
     introducing node outages during aggregation epochs"
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from src.config import SystemConfig
from evaluation.simulator import run_single_configuration
from evaluation.baselines import Ref37Scheme, Ref38Scheme


def run_experiment(output_dir: str = "results"):
    """
    Run Experiment 4: Recovery latency vs fog-node failure rate.

    Paper: "The failure rate is varied from 2% to 20% by randomly
           introducing node outages during aggregation epochs."

    Note: Ref[22] and Ref[24] are excluded — neither has a recovery
    mechanism (Table III: Recovery = "—"). Ref[22] performs federated
    model aggregation (not data recovery), and Ref[24] performs
    privacy-preserving aggregation without fault recovery.
    """
    print("=" * 60)
    print("Experiment 4: Impact of Failure Rate")
    print("=" * 60)

    failure_rates = [0.02, 0.04, 0.06, 0.08, 0.10,
                     0.12, 0.14, 0.16, 0.18, 0.20]
    num_epochs = 20
    num_runs = 3
    num_sensors = 1000
    fog_nodes = 10

    # --- PLOSHA-RMFR ---
    plosha_rec_latencies = []
    plosha_completeness = []
    plosha_availability = []

    for fr in failure_rates:
        run_rec = []
        run_comp = []
        run_avail = []
        for run in range(num_runs):
            config = SystemConfig(
                num_sensors=num_sensors,
                num_fog_nodes=fog_nodes,
                num_epochs=num_epochs,
                failure_rate=fr,
                random_seed=42 + run
            )
            summary = run_single_configuration(config, num_epochs)
            run_rec.append(summary['recovery_latency']['mean'])
            run_comp.append(summary['completeness']['mean'])
            run_avail.append(summary['availability']['mean'])

        avg_rec = np.mean(run_rec)
        avg_comp = np.mean(run_comp)
        avg_avail = np.mean(run_avail)
        plosha_rec_latencies.append(avg_rec)
        plosha_completeness.append(avg_comp)
        plosha_availability.append(avg_avail)
        print(f"  Failure={fr*100:.0f}%: RecLatency={avg_rec:.4f}s, "
              f"Comp={avg_comp:.3f}, Avail={avg_avail:.3f}")

    # --- Baselines ---
    # Only Ref[37] and Ref[38] have recovery mechanisms (Table III).
    # Ref[22] and Ref[24] have no recovery — excluded to avoid misleading
    # flat zero-lines on the recovery latency graph.
    baselines = [
        (Ref37Scheme(), 'D--', '#9C27B0'),
        (Ref38Scheme(), 'v--', '#4CAF50'),
    ]

    baseline_rec = {}
    for scheme, _, _ in baselines:
        recs = []
        for fr in failure_rates:
            rec = scheme.recovery_latency(num_sensors, fog_nodes, fr)
            recs.append(rec)
        baseline_rec[scheme.name] = recs
        print(f"  {scheme.name} recovery latencies computed.")

    # --- Plot (Fig. 5) ---
    os.makedirs(output_dir, exist_ok=True)

    fig, ax1 = plt.subplots(figsize=(10, 7))
    failure_pct = [fr * 100 for fr in failure_rates]

    ax1.plot(failure_pct, plosha_rec_latencies, 'o-', color='#2196F3',
             linewidth=2, markersize=8, label='PLOSHA-RMFR (Ours)')

    for (scheme, marker, color) in baselines:
        ax1.plot(failure_pct, baseline_rec[scheme.name],
                 marker, color=color, linewidth=1.5, markersize=6,
                 label=scheme.name)

    ax1.set_xlabel('Fog-Node Failure Rate (%)', fontsize=13)
    ax1.set_ylabel('Recovery Latency (s)', fontsize=13)
    ax1.set_title('Experiment 4: Recovery Latency vs. Fog-Node Failure Rate',
                  fontsize=14)
    ax1.legend(fontsize=11, loc='upper left')
    ax1.grid(True, alpha=0.3)

    plt.tight_layout()
    filepath = os.path.join(output_dir, 'exp4_failure_rate.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'failure_rates': failure_rates,
        'plosha_rec': plosha_rec_latencies,
        'plosha_comp': plosha_completeness,
        'plosha_avail': plosha_availability,
        **{k: v for k, v in baseline_rec.items()},
    }


if __name__ == "__main__":
    run_experiment()
