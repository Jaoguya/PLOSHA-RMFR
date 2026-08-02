"""
Experiment 5: Aggregation-Loss Exposure
=========================================
Evaluates PLOSHA-RMFR's ability to reduce aggregation-loss exposure.

Paper Reference: Section V, Experiment 5 (Fig. 6)
  - X-axis: Number of micro-slots (1 to 20)
  - Y-axis: Aggregation-loss exposure
  - "The number of adaptive micro-slots is varied from 1 to 20"
  - Key result: L_{PLOSHA} = 1/m*
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from src.config import SystemConfig
from evaluation.simulator import run_single_configuration
from evaluation.baselines import Ref24Scheme, Ref37Scheme, Ref38Scheme


def run_experiment(output_dir: str = "results"):
    """
    Run Experiment 5: Aggregation-loss exposure vs number of micro-slots.

    Paper: "The number of adaptive micro-slots is varied from 1 to 20
           while maintaining a fixed workload and failure rate."
           "L_{PLOSHA} = 1/m*"

    Note: Ref[22] excluded — it performs federated model aggregation,
    not encrypted data aggregation. See Table III.
    """
    print("=" * 60)
    print("Experiment 5: Aggregation-Loss Exposure")
    print("=" * 60)

    micro_slot_counts = list(range(1, 21))  # 1 to 20
    num_epochs = 20
    num_runs = 3

    # --- PLOSHA-RMFR ---
    plosha_loss = []
    for m in micro_slot_counts:
        run_results = []
        for run in range(num_runs):
            config = SystemConfig(
                num_sensors=1000,
                num_fog_nodes=10,
                num_epochs=num_epochs,
                m_max=m,  # Fix m_max to force this number of micro-slots
                failure_rate=0.10,
                random_seed=42 + run
            )
            summary = run_single_configuration(config, num_epochs)
            run_results.append(summary['loss_exposure']['mean'])
        avg = np.mean(run_results)
        plosha_loss.append(avg)
        print(f"  m*={m}: PLOSHA-RMFR loss exposure = {avg:.4f}")

    # Theoretical PLOSHA loss: 1/m*
    theoretical_loss = [1.0 / m for m in micro_slot_counts]

    # --- Baselines (Ref[24], Ref[37], Ref[38]) ---
    baselines = [
        (Ref24Scheme(), '^--', '#FF9800'),
        (Ref37Scheme(), 'D--', '#9C27B0'),
        (Ref38Scheme(), 'v--', '#4CAF50'),
    ]

    baseline_loss = {}
    for scheme, _, _ in baselines:
        losses = []
        for m in micro_slot_counts:
            loss = scheme.loss_exposure(m)
            losses.append(loss)
        baseline_loss[scheme.name] = losses
        print(f"  {scheme.name} loss exposure computed.")

    # --- Plot (Fig. 6) ---
    os.makedirs(output_dir, exist_ok=True)

    plt.figure(figsize=(10, 7))
    plt.plot(micro_slot_counts, plosha_loss, 'o-', color='#2196F3',
             linewidth=2, markersize=8, label='PLOSHA-RMFR (Ours)')
    plt.plot(micro_slot_counts, theoretical_loss, ':', color='#2196F3',
             linewidth=1.5, alpha=0.6, label='Theoretical (1/m*)')

    for (scheme, marker, color) in baselines:
        plt.plot(micro_slot_counts, baseline_loss[scheme.name],
                 marker, color=color, linewidth=1.5, markersize=6,
                 label=scheme.name)

    plt.xlabel('Number of Micro-Slots (m*)', fontsize=13)
    plt.ylabel('Aggregation-Loss Exposure', fontsize=13)
    plt.title('Experiment 5: Aggregation-Loss Exposure vs. Number of Micro-Slots',
              fontsize=14)
    plt.legend(fontsize=11)
    plt.grid(True, alpha=0.3)
    plt.ylim(-0.05, 1.1)
    plt.tight_layout()

    filepath = os.path.join(output_dir, 'exp5_loss_exposure.png')
    plt.savefig(filepath, dpi=150)
    plt.close()
    print(f"\n  Plot saved: {filepath}")

    return {
        'micro_slots': micro_slot_counts,
        'plosha': plosha_loss,
        'theoretical': theoretical_loss,
        **{k: v for k, v in baseline_loss.items()},
    }


if __name__ == "__main__":
    run_experiment()
