"""
Evaluation Metrics Module
===========================
Collects and computes all evaluation metrics from the paper.

Gramine-SGX Edition — adds recovery invocation count and
system availability (V >= 0.95) per Experiment Plan §9, Table 8.

Paper Reference: Section V - Performance Analysis
  1. Aggregation latency
  2. Recovery latency
  3. Aggregation completeness (V_i)
  4. Aggregation-loss exposure (L_agg = 1/m*)
  5. Communication overhead (bytes)
  6. System availability (% epochs with V >= 0.95)
  7. Recovery invocation count
"""

import numpy as np
from typing import List, Dict


class MetricsCollector:
    """Collects and aggregates evaluation metrics across epochs."""

    def __init__(self):
        self.agg_latencies: List[float] = []
        self.rec_latencies: List[float] = []
        self.completeness_scores: List[float] = []
        self.loss_exposures: List[float] = []
        self.comm_overheads: List[float] = []
        self.availabilities: List[float] = []
        self.quality_scores: List[float] = []
        self.reliabilities: List[float] = []
        self.recovery_invocations: List[int] = []

    def record_epoch(self, metrics: dict):
        """Record metrics from a single epoch."""
        self.agg_latencies.append(metrics.get('agg_latency', 0.0))
        self.rec_latencies.append(metrics.get('recovery_latency', 0.0))
        self.completeness_scores.append(metrics.get('completeness', 0.0))
        self.loss_exposures.append(metrics.get('loss_exposure', 1.0))
        self.comm_overheads.append(metrics.get('comm_overhead_kb', 0.0))
        self.availabilities.append(metrics.get('availability', 0.0))
        self.quality_scores.append(metrics.get('quality_score', 0.0))
        self.reliabilities.append(metrics.get('reliability', 0.0))
        self.recovery_invocations.append(
            metrics.get('recovery_invocations', 0))

    def get_summary(self) -> dict:
        """Get summary statistics across all recorded epochs."""
        return {
            'agg_latency': {
                'mean': np.mean(self.agg_latencies) if self.agg_latencies else 0.0,
                'std': np.std(self.agg_latencies) if self.agg_latencies else 0.0,
                'values': list(self.agg_latencies)
            },
            'recovery_latency': {
                'mean': np.mean(self.rec_latencies) if self.rec_latencies else 0.0,
                'std': np.std(self.rec_latencies) if self.rec_latencies else 0.0,
                'values': list(self.rec_latencies)
            },
            'completeness': {
                'mean': np.mean(self.completeness_scores) if self.completeness_scores else 0.0,
                'std': np.std(self.completeness_scores) if self.completeness_scores else 0.0,
                'values': list(self.completeness_scores)
            },
            'loss_exposure': {
                'mean': np.mean(self.loss_exposures) if self.loss_exposures else 1.0,
                'std': np.std(self.loss_exposures) if self.loss_exposures else 0.0,
                'values': list(self.loss_exposures)
            },
            'comm_overhead_kb': {
                'mean': np.mean(self.comm_overheads) if self.comm_overheads else 0.0,
                'std': np.std(self.comm_overheads) if self.comm_overheads else 0.0,
                'values': list(self.comm_overheads)
            },
            'availability': {
                'mean': np.mean(self.availabilities) if self.availabilities else 0.0,
                'std': np.std(self.availabilities) if self.availabilities else 0.0,
                'values': list(self.availabilities)
            },
            'quality_score': {
                'mean': np.mean(self.quality_scores) if self.quality_scores else 0.0,
                'values': list(self.quality_scores)
            },
            'reliability': {
                'mean': np.mean(self.reliabilities) if self.reliabilities else 0.0,
                'values': list(self.reliabilities)
            },
            'recovery_invocations': {
                'total': int(np.sum(self.recovery_invocations)) if self.recovery_invocations else 0,
                'mean_per_epoch': float(np.mean(self.recovery_invocations)) if self.recovery_invocations else 0.0,
                'values': list(self.recovery_invocations)
            }
        }

    def reset(self):
        """Clear all recorded metrics."""
        self.__init__()


def compute_aggregation_loss_exposure(m_star: int) -> float:
    """
    Compute aggregation-loss exposure.

    L_agg(m*) = 1/m*

    Paper: Phase III - "the maximum fraction of the aggregation epoch
           that may be lost when a single micro-slot becomes unavailable"
    """
    return 1.0 / max(1, m_star)


def compute_recovery_complexity(d_miss: int) -> int:
    """
    Compute recovery complexity.

    O(|D_i^{miss}|) rather than O(m*)

    Paper: Phase IV - "recovery complexity is proportional to the
           number of incomplete micro-slots"
    """
    return d_miss
