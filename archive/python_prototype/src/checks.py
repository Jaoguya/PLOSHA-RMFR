"""
Boundary Checks and Normalization Protocol
============================================
Validates that all state variables remain in [0,1] at every epoch.

Experiment Plan Section 8:
  "All state variables must stay in [0,1] throughout the simulation.
   Out-of-range values are not just mathematical errors — they silently
   corrupt the EWMA predictor and cause Cap, Risk, and RU to drift
   outside their proofs."
"""

import logging

logger = logging.getLogger(__name__)


def check_state(node, t: int):
    """
    Assert all state variables are in [0, 1] at epoch t.

    Called at the end of every epoch for every fog node.
    Raises AssertionError if any variable is out of range.

    Experiment Plan §8, Table 7:
        Assert 0 <= W <= 1 every epoch
        Assert 0 <= Q <= 1
        Assert 0 <= L <= 1
        Assert 0 <= Rel <= 1
        Assert 0 <= Cap <= 1
        Assert 0 <= Risk <= 1
        Assert 0 <= V <= 1
    """
    assert 0 <= node.workload <= 1, \
        f'W out of range at t={t}: {node.workload}'
    assert 0 <= node.queue_util <= 1, \
        f'Q out of range at t={t}: {node.queue_util}'
    assert 0 <= node.latency <= 1, \
        f'L out of range at t={t}: {node.latency}'
    assert 0 <= node.reliability <= 1, \
        f'Rel out of range at t={t}: {node.reliability}'
    assert 0 <= node.cap <= 1, \
        f'Cap out of range at t={t}: {node.cap}'
    assert 0 <= node.risk <= 1, \
        f'Risk out of range at t={t}: {node.risk}'
    assert 0 <= node.completeness <= 1, \
        f'V out of range at t={t}: {node.completeness}'

    # Log any warnings for derived quantities
    if not (0 <= node.failure_exposure <= 1):
        logger.warning(
            f'FE out of range at t={t}, node={node.node_id}: '
            f'{node.failure_exposure} — simulation bug')
    if not (0 <= node.recovery_urgency <= 1):
        logger.warning(
            f'RU out of range at t={t}, node={node.node_id}: '
            f'{node.recovery_urgency} — simulation bug')
    if not (0 <= node.quality_score <= 1):
        logger.warning(
            f'Score out of range at t={t}, node={node.node_id}: '
            f'{node.quality_score} — simulation bug')
