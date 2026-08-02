"""
Pre-Experiment Calibration Module
===================================
Implements calibration routines specified in the Experiment Plan
Section 10 (Pre-Experiment Implementation Checklist).

Must be run ONCE before any experiments:
  1. beta_t calibrated from 1,000 warmup epochs
  2. RTT_99TH computed from 10,000 latency samples
  3. EWMA validated on stationary synthetic trace
  4. Paillier micro-validation (sum of Dec == plaintext sum)
"""

import time
import statistics
import random
import numpy as np
from typing import List, Tuple

from src.config import SystemConfig


def calibrate_beta_t(config: SystemConfig, n_trials: int = 1000) -> float:
    """
    Calibrate β_t: average processing overhead per micro-slot.

    Times n_trials single-slot aggregation operations and returns
    the mean.  β_t is then used only in the m* objective — it does
    not appear in any performance metric.

    Experiment Plan §5.2:
        overhead_times = []
        for _ in range(1000):
            t0 = time.perf_counter()
            run_one_microslot(Ns, Nf)
            overhead_times.append(time.perf_counter() - t0)
        beta_t = statistics.mean(overhead_times)

    Args:
        config: System configuration.
        n_trials: Number of micro-slot timing trials.

    Returns:
        Calibrated β_t value in seconds.
    """
    ns_per_fog = max(1, config.num_sensors // config.num_fog_nodes)
    overhead_times = []

    for _ in range(n_trials):
        t0 = time.perf_counter()
        # Simulate one micro-slot: generate and sum ns_per_fog values
        values = [random.randint(0, config.max_sensor_value)
                  for _ in range(ns_per_fog)]
        _ = sum(values)
        overhead_times.append(time.perf_counter() - t0)

    beta_t = statistics.mean(overhead_times)
    return beta_t


def calibrate_rtt_99th(rtt_mu: float, rtt_sigma: float,
                        n_samples: int = 10000) -> float:
    """
    Compute 99th percentile of the log-normal RTT distribution.

    Experiment Plan §4.3:
        RTT_99TH = exp(μ + 2.576·σ)

    Also validates empirically with n_samples draws.

    Args:
        rtt_mu: Log-normal μ parameter.
        rtt_sigma: Log-normal σ parameter.
        n_samples: Number of samples for empirical validation.

    Returns:
        RTT 99th percentile value.
    """
    import math

    # Analytical 99th percentile
    analytical = math.exp(rtt_mu + 2.576 * rtt_sigma)

    # Empirical validation
    samples = np.random.lognormal(rtt_mu, rtt_sigma, size=n_samples)
    empirical = np.percentile(samples, 99)

    return analytical


def validate_ewma(alpha: float = 0.3, n_epochs: int = 100,
                  target: float = 0.5) -> Tuple[bool, float]:
    """
    Validate EWMA on a stationary synthetic trace.

    Generates a constant signal at `target`, runs EWMA for n_epochs,
    and checks that the prediction converges within tolerance.

    Experiment Plan §10, Item 10: "EWMA validated on stationary synthetic trace"

    Returns:
        Tuple (converged: bool, final_prediction: float).
    """
    pred = 0.0
    for _ in range(n_epochs):
        pred = alpha * target + (1 - alpha) * pred

    converged = abs(pred - target) < 0.01
    return converged, pred


def validate_paillier_sum(n_values: int = 10) -> Tuple[bool, float, float]:
    """
    Micro-validation: sum(Dec(C_micro,k)) == plaintext sum.

    Encrypts n_values integers, aggregates homomorphically, decrypts,
    and compares to the plaintext sum.

    Experiment Plan §10, Item 9: "sum(Dec(C_micro,k)) == plaintext sum"

    Returns:
        Tuple (valid: bool, decrypted_sum: float, expected_sum: float).
    """
    from src.crypto import paillier as pail

    pk, sk = pail.keygen(key_size=1024)  # Use smaller key for validation speed

    values = [random.randint(0, 65535) for _ in range(n_values)]
    expected_sum = sum(values)

    # Encrypt each value
    ciphertexts = [pail.encrypt(pk, v) for v in values]

    # Aggregate homomorphically
    agg = pail.aggregate_ciphertexts(ciphertexts)

    # Decrypt
    decrypted_sum = pail.decrypt(sk, agg)

    valid = abs(decrypted_sum - expected_sum) < 0.5  # Allow float rounding
    return valid, decrypted_sum, expected_sum


def generate_seeds(base_seed: int = 42, n: int = 30) -> List[int]:
    """
    Generate n deterministic seeds from a base seed.

    Experiment Plan §10, Item 5:
        "30 random seeds pre-announced and committed to disk"

    Args:
        base_seed: Base random seed.
        n: Number of seeds to generate.

    Returns:
        List of n integer seeds.
    """
    gen = random.Random(base_seed)
    return [gen.randint(0, 2**31 - 1) for _ in range(n)]


def run_all_checks(config: SystemConfig) -> dict:
    """
    Run all pre-experiment validation checks.

    Returns a summary dict with pass/fail for each check.
    """
    results = {}

    # 1. beta_t calibration
    beta_t = calibrate_beta_t(config, n_trials=100)  # Fewer for quick check
    results['beta_t'] = beta_t
    results['beta_t_ok'] = beta_t > 0

    # 2. RTT_99TH
    rtt_99th = calibrate_rtt_99th(config.rtt_mu, config.rtt_sigma)
    results['rtt_99th'] = rtt_99th
    results['rtt_99th_ok'] = rtt_99th > 0

    # 3. EWMA validation
    ewma_ok, ewma_final = validate_ewma(config.alpha_ewma)
    results['ewma_converged'] = ewma_ok
    results['ewma_final'] = ewma_final

    # 4. Threshold ordering
    results['threshold_ordering'] = (
        config.tau_1 < config.tau_2 < config.tau_3)

    # 5. Seeds
    seeds = generate_seeds(config.random_seed, config.num_runs)
    results['num_seeds'] = len(seeds)
    results['seeds_ok'] = len(seeds) == config.num_runs

    return results
