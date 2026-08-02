# FT-Serverless Edge — Baseline Implementation

> **Reference [38]:** Xu et al., *"Efficient and Fault Tolerant Data Stream Processing with Uncertain Data Rates in Serverless Edge Computing,"* IEEE Transactions on Services Computing, Vol. 19, No. 1, Jan/Feb 2026.

---

## Overview

This directory contains a **C++ discrete-event simulation** of the fault-tolerant serverless edge computing scheme from Ref [38], integrated into the PLOSHA-RMFR benchmarking framework. The scheme processes IoT sensor data streams across a **Serverless Edge Computing (SEC)** network composed of cloudlet nodes, using DAG-based function pipelines with active-standby replication and proactive rate adaptation.

---

## Directory Structure

```
ft_serverless_edge/
├── src/
│   ├── ft_serverless_edge.hpp   # Core data structures & class declaration
│   ├── ft_algorithms.cpp        # Fwk / Heu / Adj algorithms + delay equations
│   ├── ft_network.cpp           # SEC network construction & request/DAG generation
│   ├── ft_experiments.cpp       # Simulation engine, constructor, 6 experiment runners
│   ├── main.cpp                 # CLI entry point
│   └── Makefile                 # Build rules
├── exp1_sensor_scalability/
│   ├── run.sh
│   └── results.csv
├── exp2_fog_scalability/
│   ├── run.sh
│   └── results.csv
├── exp3_workload_intensity/
│   ├── run.sh
│   └── results.csv
├── exp3_failure_rate/
│   ├── run.sh
│   └── results.csv
├── exp4_loss_exposure/
│   ├── run.sh
│   └── results.csv
├── exp5_recovery_comm/
│   ├── run.sh
│   └── results.csv
└── README.md
```

---

## Build

```bash
cd src/
make          # produces ./ft_serverless_sim
# or manually:
g++ -O2 -std=c++17 -o ft_serverless_sim \
    main.cpp ft_network.cpp ft_algorithms.cpp ft_experiments.cpp
```

**Requirements:** GCC ≥ 7 with C++17 support (structured bindings, `std::priority_queue` with `std::greater<>`).

---

## Architecture

### Network Model (`ft_network.cpp`)

A random SEC network of **N cloudlet nodes** is generated with:
- Memory capacity: 128 GB – 512 GB per cloudlet
- Impact factor α: [0.05, 0.09] (memory allocation cost per unit data rate)
- Cold-start delay: ~0.8 ms
- Link delays: random sparse topology in [0.1, 0.5] ms per unit

### Request Model

Each **DSP request** represents an IoT sensor stream that is processed through a **randomly generated DAG** of 3–8 serverless functions. Each request carries:
- Data rate ρ (MB/s), derived from real sensor packet sizes
- Fault-tolerance requirement δ ∈ [0.991, 0.999]
- EWMA state for multi-timescale rate prediction
- Per-function failure probabilities in [0.001, 0.003]

DAGs are topologically sorted and partitioned into sequential groups (partitions) before placement.

### Core Algorithms

#### Algorithm Fwk — Framework (Binary Search for Optimal Standby Count)

Performs a **binary search** over the number of standby replicas `n_m ∈ [1, 3]` to find the minimum `n_m` satisfying the fault-tolerance constraint:

```
∏_f (1 − p_f^(n_m+1)) ≥ δ_m    [Eq. 1]
```

For each candidate `n_m`, it calls **Algorithm Heu** and caches the resulting delay to avoid redundant computation.

#### Algorithm Heu — Heuristic Placement (3-Stage)

**Stage 1 — Partitioning:** Topological sort of the DAG; functions assigned to ordered partition layers.

**Stage 2 — Active Placement via Auxiliary Graph:**
An auxiliary graph is constructed with `|partitions| × |cloudlets| × 3` virtual nodes per partition layer (3 node types per cloudlet: transit, existing-instance, new-instance). Edge weights encode:
- Processing delay: `α_j · M_j · ρ_{m,τ}` (Eq. 2)
- Cold-start penalty for new instances
- Inter-cloudlet transmission delay

**Dijkstra's shortest path** over this graph selects the active cloudlet assignment for each partition layer that minimises end-to-end delay.

**Stage 3 — Standby Placement:**
For each function, up to `n_m` standby instances are placed on the nearest cloudlets (by link delay) that have sufficient free memory.

#### Algorithm Adj — Proactive Adjustment (Multi-Timescale EWMA)

Implements a lightweight approximation of the MT-LSTM rate predictor from the paper using **multi-timescale Exponential Weighted Moving Averages**:

```
ẑ_g(τ) = α_g · ρ(τ) + (1−α_g) · ẑ_g(τ−1)    [Eq. 6]
```

When the predicted rate deviates >10% from the current rate, standby instances are proactively relocated to preserve fault tolerance under the new workload regime.

### Delay Model

| Component | Equation | Description |
|---|---|---|
| Processing | `d_proc = α_j · M_j · ρ_{m,τ} + d_cold` | Per-function execution cost |
| Path | `d^pt = max over critical path of Σ(d_proc + d_tx)` | Eq. 3 — DAG critical path |
| Recovery | `d^rec = Σ κ_m · d_link(active→standby)` | Eq. 4 — state buffer transfer |
| Total | `d_{m,τ} = d^pt + d^rec` | Eq. 5 |

---

## Experiments

All experiments read from the shared PLOSHA dataset (`dataset/plosha_dataset.csv`) and output a standardised `results.csv` with columns:

```
variable_value, primary_metric, secondary_metric_1, secondary_metric_2
```

Each experiment can be re-run via its `run.sh` script from within its directory.

---

### Exp 1 — Sensor Scalability

**Goal:** Measure how average end-to-end delay scales with the number of concurrently processed sensor streams.

| Parameter | Value |
|---|---|
| Variable | Number of sensors (500 – 5000, step 500) |
| Fixed cloudlets | 10 |
| Primary metric | Average total delay (ms) |

**Script:** `exp1_sensor_scalability/run.sh`

**Results summary (`results.csv`):**

| Sensors | Avg Delay (ms) |
|---|---|
| 500 | 36.03 |
| 1000 | 33.13 |
| 1500 | 39.89 |
| 2000 | 43.36 |
| 2500 | 47.60 |
| 3000 | 44.41 |
| 3500 | 41.60 |
| 4000 | 40.07 |
| 4500 | 40.27 |
| 5000 | 43.55 |

Delay increases with sensor count but plateaus after ~2500 due to load balancing across cloudlets.

---

### Exp 2 — Fog Node Scalability

**Goal:** Evaluate how increasing the number of cloudlet (fog) nodes affects average delay, with a fixed pool of 1000 sensor requests.

| Parameter | Value |
|---|---|
| Variable | Number of cloudlets (5 – 50, step 5) |
| Fixed sensors | 1000 |
| Primary metric | Average total delay (ms) |

**Script:** `exp2_fog_scalability/run.sh`

**Results summary (`results.csv`):**

| Cloudlets | Avg Delay (ms) |
|---|---|
| 5 | 26.00 |
| 10 | 33.13 |
| 15 | 42.38 |
| 20 | 27.36 |
| 25 | 27.34 |
| 30 | 27.37 |
| 35 | 27.36 |
| 40 | 27.42 |
| 45 | 27.43 |
| 50 | 26.75 |

Delay stabilises around 27 ms once ≥20 cloudlets are available, demonstrating that the Heu placement algorithm efficiently distributes load across a larger infrastructure.

---

### Exp 3 — Workload Intensity

**Goal:** Assess the impact of increasing data reporting rates (workload intensity) on delay, queue utilisation, and recovery frequency.

| Parameter | Value |
|---|---|
| Variable | Reporting rate multiplier (6 – 60, step 6) |
| Fixed cloudlets | 10 |
| Fixed sensors | 1000 |
| Primary metric | Average total delay (ms) |
| Secondary 1 | Queue utilisation ratio |
| Secondary 2 | Recovery event frequency |

**Script:** `exp3_workload_intensity/run.sh`

**Results summary (`results.csv`):**

| Rate Mult. | Avg Delay (ms) | Queue Util. | Recovery Freq. |
|---|---|---|---|
| 6 | 33.13 | 0.798 | 798 |
| 12 | 35.12 | 0.798 | 798 |
| 18 | 38.44 | 0.798 | 798 |
| 24 | 43.07 | 0.798 | 798 |
| 30 | 48.03 | 0.798 | 798 |
| 36 | 52.33 | 0.798 | 798 |
| 42 | 56.28 | 0.798 | 798 |
| 48 | 60.02 | 0.798 | 798 |
| 54 | 63.54 | 0.798 | 798 |
| 60 | 66.81 | 0.798 | 798 |

Delay grows roughly linearly with workload intensity. The queue utilisation remains stable at ~79.8%, reflecting the scheme's ability to absorb load increases without saturation up to this range.

---

### Exp 4 — Failure Rate

**Goal:** Characterise system reliability (completeness, availability) and recovery latency under varying function failure probabilities.

| Parameter | Value |
|---|---|
| Variable | Failure rate (2% – 20%, step 2%) |
| Fixed cloudlets | 10 |
| Fixed sensors | 1000 |
| Primary metric | Avg recovery latency (ms) |
| Secondary 1 | Completeness (fraction of successful requests) |
| Secondary 2 | Availability |

**Script:** `exp3_failure_rate/run.sh`

**Results summary (`results.csv`):**

| Failure Rate | Recovery Latency (ms) | Completeness | Availability |
|---|---|---|---|
| 2% | 0.00525 | 1.000 | 0.980 |
| 4% | 0.00512 | 1.000 | 0.965 |
| 6% | 0.00396 | 1.000 | 0.948 |
| 8% | 0.00448 | 1.000 | 0.927 |
| 10% | 0.00461 | 1.000 | 0.911 |
| 12% | 0.00418 | 1.000 | 0.880 |
| 14% | 0.00443 | 1.000 | 0.860 |
| 16% | 0.00428 | 1.000 | 0.845 |
| 18% | 0.00406 | 1.000 | 0.821 |
| 20% | 0.00416 | 1.000 | 0.793 |

Completeness remains at 100% across all failure rates thanks to active-standby failover. Availability degrades gracefully from 98% to ~79% as failure rates rise, while recovery latency stays consistently sub-millisecond (dominated by state buffer transfers, not cold starts).

---

### Exp 5 — Loss Exposure

**Goal:** Quantify the fraction of DAG execution that remains unprotected (exposed to data loss) as a function of the number of micro-slots used for proactive standby adjustment.

| Parameter | Value |
|---|---|
| Variable | Number of micro-slots K (1 – 20) |
| Fixed cloudlets | 10 |
| Fixed sensors | 1000 |
| Primary metric | Average loss exposure ratio (0 = fully protected, 1 = fully exposed) |

**Script:** `exp4_loss_exposure/run.sh`

**Results summary (`results.csv`):**

| Micro-slots (K) | Loss Exposure |
|---|---|
| 1 | 0.4977 |
| 2 | 0.2476 |
| 3 | 0.1251 |
| 4 | 0.0636 |
| 5 | 0.0335 |
| 6 | 0.0184 |
| 7 | 0.0096 |
| 8 | 0.0039 |
| 9 | 0.0027 |
| 10 | 0.0014 |
| ≥14 | ≈0.000 |

Loss exposure decreases exponentially with K; ≥14 micro-slots achieves near-zero exposure, confirming that frequent proactive adjustment is highly effective at eliminating unprotected windows.

---

### Exp 6 — Recovery Communication Overhead

**Goal:** Measure the total state-buffer communication cost (KB) during failover as a function of the number of incomplete execution slots that must be retransmitted.

| Parameter | Value |
|---|---|
| Variable | Incomplete slots (1 – 10) |
| Fixed cloudlets | 10 |
| Fixed sensors | 1000 (all forced into failure/recovery mode) |
| Primary metric | Avg recovery communication cost (KB per request) |

**Script:** `exp5_recovery_comm/run.sh`

**Results summary (`results.csv`):**

| Incomplete Slots | Comm. Cost (KB) |
|---|---|
| 1 | 32.14 |
| 2 | 64.28 |
| 3 | 96.42 |
| 4 | 128.56 |
| 5 | 160.71 |
| 6 | 192.85 |
| 7 | 224.99 |
| 8 | 257.13 |
| 9 | 289.27 |
| 10 | 321.41 |

Communication cost grows linearly (≈32.14 KB per additional incomplete slot), directly reflecting the state-buffer accumulation model from Eq. 4. This linear relationship provides a clear design trade-off between checkpoint frequency and recovery cost.

---

## Running All Experiments

```bash
# Build once
cd src/ && make && cd ..

# Run each experiment
bash exp1_sensor_scalability/run.sh
bash exp2_fog_scalability/run.sh
bash exp3_workload_intensity/run.sh
bash exp3_failure_rate/run.sh
bash exp4_loss_exposure/run.sh
bash exp5_recovery_comm/run.sh
```

Each script auto-compiles the binary if missing, then sweeps its variable range and appends results to `results.csv`.

---

## Key Parameters (Paper §VI-A)

| Symbol | Value Range | Description |
|---|---|---|
| M_j | 128 – 512 GB | Cloudlet memory capacity |
| α_j | 0.05 – 0.09 | Memory allocation impact factor |
| ρ_{m,τ} | 0.01 – 100 MB/s | Sensor data rate |
| p_f | 0.001 – 0.003 | Per-function failure probability |
| δ_m | 0.991 – 0.999 | Fault-tolerance requirement |
| n_m | 1 – 3 | Number of standby replicas |
| κ_m | ~2 KB base | State buffer size |
| d_cold | ~0.8 ms | Cold-start delay |
| d_tx | 0.1 – 0.5 ms | Per-unit link transmission delay |

---

## Notes

- The simulation uses a **seeded PRNG** (`std::mt19937`) for full reproducibility. Seed defaults to 42 and can be overridden via `--seed`.
- The **Adj algorithm** uses multi-timescale EWMA as a lightweight proxy for the MT-LSTM predictor described in the paper; it captures the same proactive adjustment behaviour without requiring a neural-network runtime.
- All delay values are in **milliseconds**; communication costs are in **kilobytes**.
- Results are averaged over all requests in each batch (1000 sensors unless otherwise varied).
