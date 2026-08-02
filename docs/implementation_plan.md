# Implementation Plan: Experiment 8 (Ablation) & Experiment 9 (Scheduling Efficiency)

## Overview

Add two new experiments from `docs/new.md` to the PLOSHA-RMFR DES simulation framework.
All data is produced by real C++ DES simulation runs.

## Experiment 8: Ablation of PLOSHA Aggregation Architecture

**PLOSHA-only** — compares 4 aggregation variants:

| Variant       | Micro-slots            | Hierarchical Aggregation |
|---------------|------------------------|--------------------------|
| Flat-Epoch    | forced = 1             | No                       |
| Fixed-Slot    | forced = √N            | No                       |
| Adaptive-Slot | optimizer (existing)   | No                       |
| Full PLOSHA   | optimizer (existing)   | Yes                      |

- **Sweep**: num_sensors = 500..5000
- **Metrics**: aggregation_latency_ms, processing_overhead_ms, loss_exposure_fraction
- **Output**: `schemes/plosha_rmfr/exp1_ablation_aggregation/results.csv`

## Experiment 9: Scheduling Efficiency

Compares PLOSHA-RMFR vs FedDQN, FT-Workflow, FT-Serverless-Edge.

- **Sweep**: num_fog_nodes = 5..50
- **Metrics**: scheduling_latency_ms, workload_imbalance (I_W)
- **Output**: `schemes/<scheme>/exp2_scheduling_efficiency/results.csv`

## Files Modified

### PLOSHA-RMFR
- `config.hpp` — add `hierarchical_aggregation` flag
- `metrics.hpp` / `metrics.cpp` — add scheduling_latency_ms, workload_imbalance, processing_overhead_ms; add ablation CSV writer
- `plosha.hpp` / `plosha.cpp` — add processing_overhead_ms, hierarchical flag
- `des_engine.hpp` / `des_engine.cpp` — add runExp1 (ablation), runExp2 (scheduling); add scheduling timing in runEpoch
- `main.cpp` — update to support experiments 1–6

### FedDQN
- `fed_dqn_sim.hpp` / `fed_dqn_sim.cpp` — add scheduling_latency_ms, workload_imbalance to metrics
- `exp2_main.cpp` — new experiment binary
- `Makefile` — add exp2 target

### Fault-Tolerant Workflow
- `ft_engine.hpp` / `ft_engine.cpp` — add runExp2, scheduling timing
- `main.cpp` — add case 2

### FT-Serverless Edge
- `ft_serverless_edge.hpp` — add runExp2
- `ft_experiments.cpp` — add runExp2
- `main.cpp` — add case 2

### Plots
- `generate_plots.py` — update exp1 for variant CSV format

### Benchmark
- `run_benchmark.sh` — add exp1/exp2 runs
