"""
Baseline Base Class
====================
Abstract base class providing the common interface and shared timing
constants for all baseline evaluation simulators.

Timing constants are taken from Table 2 (Computation Cost Notations)
and the PLOSHA-RMFR config module.
"""

from abc import ABC, abstractmethod
from typing import Dict, List, Any
import numpy as np

from src.config import SystemConfig


class BaselineScheme(ABC):
    """
    Abstract baseline scheme interface.

    Every baseline must produce the same 6 metrics as PLOSHA-RMFR so
    that experiments can plot them on the same axes:
        - agg_latency      : seconds per epoch
        - recovery_latency  : seconds per recovery event
        - completeness      : ratio [0, 1]
        - loss_exposure     : fraction [0, 1]
        - comm_overhead_kb  : kilobytes exchanged
        - availability      : ratio [0, 1]
    """

    # Scheme display name (override in subclass)
    name: str = "BaselineScheme"

    def __init__(self, config: SystemConfig = None):
        self.config = config or SystemConfig()

    # ------------------------------------------------------------------
    # Abstract interface — each baseline must implement these
    # ------------------------------------------------------------------
    @abstractmethod
    def aggregation_latency(self, num_sensors: int, num_fog_nodes: int,
                            failure_rate: float, workload_intensity: float,
                            **kwargs) -> float:
        """Return aggregation latency (seconds) for one epoch."""

    @abstractmethod
    def recovery_latency(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """Return recovery latency (seconds) for one recovery event."""

    @abstractmethod
    def loss_exposure(self, num_micro_slots: int, **kwargs) -> float:
        """Return aggregation-loss exposure for the scheme."""

    @abstractmethod
    def comm_overhead_kb(self, num_sensors: int, num_fog_nodes: int,
                         failure_rate: float, num_micro_slots: int = 10,
                         **kwargs) -> float:
        """Return communication overhead in kilobytes."""

    # ------------------------------------------------------------------
    # Derived metrics with sensible defaults (can be overridden)
    # ------------------------------------------------------------------
    def completeness(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        Aggregation completeness = fraction of sensor reports
        successfully aggregated.
        Default: (1 - failure_rate) — schemes without recovery lose
        all reports from failed nodes.
        """
        return max(0.0, 1.0 - failure_rate)

    def availability(self, num_sensors: int, num_fog_nodes: int,
                     failure_rate: float, **kwargs) -> float:
        """
        System availability = fraction of epochs completed successfully.
        Default: (1 - failure_rate).
        """
        return max(0.0, 1.0 - failure_rate)

    # ------------------------------------------------------------------
    # Convenience: run a full metric sweep
    # ------------------------------------------------------------------
    def run_epoch(self, num_sensors: int = 1000, num_fog_nodes: int = 10,
                  failure_rate: float = 0.05, workload_intensity: float = 1.0,
                  num_micro_slots: int = 10, **kwargs) -> Dict[str, float]:
        """Return all 6 metrics for one simulated epoch."""
        return {
            'agg_latency': self.aggregation_latency(
                num_sensors, num_fog_nodes, failure_rate,
                workload_intensity, **kwargs),
            'recovery_latency': self.recovery_latency(
                num_sensors, num_fog_nodes, failure_rate,
                num_micro_slots, **kwargs),
            'completeness': self.completeness(
                num_sensors, num_fog_nodes, failure_rate, **kwargs),
            'loss_exposure': self.loss_exposure(num_micro_slots, **kwargs),
            'comm_overhead_kb': self.comm_overhead_kb(
                num_sensors, num_fog_nodes, failure_rate,
                num_micro_slots, **kwargs),
            'availability': self.availability(
                num_sensors, num_fog_nodes, failure_rate, **kwargs),
        }
