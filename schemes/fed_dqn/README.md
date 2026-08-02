# FedDQN — Ref[22] Baseline Scheme

**Paper**: Choppara & Mangalampalli, "Adaptive Task Scheduling in Fog Computing Using Federated DQN and K-Means Clustering," IEEE Access, Vol. 13, 2025.

## Overview

This folder implements a **discrete-event simulation** of the Federated Deep Q-Network (FLDQN) task scheduling framework described in Ref[22]. The simulation faithfully reproduces K-Means task prioritization, tabular Q-learning with experience replay, and federated averaging — all running as real algorithms, not mocked.

## Architecture

```
Dataset Tasks → K-Means Clustering (K=3) → Priority Assignment
                                                  │
                                                  ▼
                           ┌──────────────────────────────────────┐
                           │  Fog Node 1    Fog Node 2    ...     │
                           │  ┌─────────┐  ┌─────────┐           │
                           │  │ VM1 VM2 │  │ VM1 VM2 │           │
                           │  │ VM3 VM4 │  │ VM3 VM4 │           │
                           │  └────┬────┘  └────┬────┘           │
                           │       │             │                │
                           │  Q-Table        Q-Table              │
                           │  (local DQN)    (local DQN)          │
                           └──────┬──────────────┬────────────────┘
                                  │              │
                                  ▼              ▼
                           ┌─────────────────────────┐
                           │  Federated Averaging     │
                           │  θ_global = (1/N) Σ θi   │
                           └─────────────────────────┘
```

## What Is Real

| Component | Implementation | Details |
|-----------|---------------|---------|
| K-Means Clustering | Full iterative K-Means on [exec_time, deadline] | K=3, convergence-based |
| Task Prioritization | Urgency scoring: exec_time/deadline ratio | High/Medium/Low priority |
| Q-Learning | Tabular Q-learning with Bellman equation updates | α=0.001, γ=0.95 |
| ε-Greedy Exploration | Starts at ε=1.0, decays per episode | ε_min=0.01 |
| Experience Replay | Deque-based replay buffer (1000 capacity) | Batch size 32 |
| Federated Averaging | Weighted averaging of Q-tables across all fog nodes | Every 5 scheduling periods |
| VM Scheduling | Queue-based with CPU capacity, memory, power constraints | Max queue depth 50 |
| Energy Tracking | `Power_watts × execution_time` per task per VM | Real accumulation |
| SLA Monitoring | `completion_time > arrival_time + deadline` | Real checking |
| Failure Simulation | Probabilistic fog node failure per episode | With queue-flush recovery |
| Timing | Wall-clock `std::chrono::high_resolution_clock` | Real measurement |

## Source Files

```
src/
├── fed_dqn_sim.hpp    # DES engine header (Task, FogNode, VM, QTable structs)
├── fed_dqn_sim.cpp    # Full scheduling simulation implementation
├── exp1_main.cpp      # Experiment 1: Sensor Scalability (500–5000)
├── exp2_main.cpp      # Experiment 2: Fog Node Scalability (5–50)
├── exp3_main.cpp      # Experiment 3: Workload Intensity
├── exp4_main.cpp      # Experiment 4: Failure Rate (2%–20%)
└── Makefile           # Build targets for each experiment
```

## Experiments

| # | Experiment | Independent Variable | Primary Metric | Secondary Metrics |
|---|-----------|---------------------|---------------|-------------------|
| 1 | Sensor Scalability | Number of sensors (500–5000) | Aggregation latency (ms) | — |
| 2 | Fog Node Scalability | Number of fog nodes (5–50) | Aggregation latency (ms) | — |
| 3 | Workload Intensity | Task multiplier (0.5x–5x) | Aggregation latency (ms) | Queue utilization, recovery count |
| 4 | Failure Rate | Fog failure rate (2%–20%) | Recovery latency (ms) | Aggregation completeness, availability |

## Build & Run

**Prerequisites**: Ubuntu 22.04, g++ with C++17. No external libraries required.

```bash
# Build all experiments
cd src/
make all

# Run individual experiments
./exp1_sensor_scalability
./exp2_fog_scalability
./exp3_workload_intensity
./exp3_failure_rate
```

## Output Format

All `results.csv` files follow the project standard:
```csv
variable_value,primary_metric,secondary_metric_1,secondary_metric_2
500,15.2300,,
1000,28.4500,,
```

## Hyperparameters (from paper)

| Parameter | Value | Description |
|-----------|-------|-------------|
| K (clusters) | 3 | High, Medium, Low priority |
| α (learning rate) | 0.001 | Q-value update step size |
| γ (discount factor) | 0.95 | Future reward weighting |
| ε (exploration) | 1.0 → 0.01 | Decays by 0.995 per episode |
| Batch size | 32 | Experience replay sample size |
| Replay buffer | 1000 | Max stored experiences per node |
| Federated period | 5 | Aggregate every 5 scheduling periods |
| Reward: SLA met | +10 | Task completed within deadline |
| Reward: SLA violated | -5 | Task exceeded deadline |
| Reward: Rejected | -10 | Task rejected (no resources) |

## Dataset Mapping

The `plosha_dataset.csv` columns are mapped to task attributes:

| CSV Column | Task Attribute | Mapping |
|-----------|---------------|---------|
| `Connection_Duration` | `execution_time` | × 1000 (scale to ms) |
| `Bytes_Transferred` | `cpu_requirement` | ÷ 100 (normalize) |
| `Flow_Rate` / `Bytes_Transferred` | `deadline` | exec_time × (1.5 + urgency×10) |
| — | `priority` | Set by K-Means on [exec_time, deadline] |
| — | `arrival_time` | Staggered by 0.1 per task |

## Dependencies

- Dataset: `dataset/plosha_dataset.csv`
- No external crypto libraries (pure C++ scheduling)
