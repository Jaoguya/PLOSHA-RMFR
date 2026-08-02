# Walkthrough: Experiment 8 & 9 Implementation

## What Was Done

Added two new experiments to the PLOSHA-RMFR benchmark framework. All results come from **real C++ DES simulation runs** — no fake or synthetic data.

Previously created synthetic CSV files were **deleted**.

---

## Experiment 8: Ablation of PLOSHA Aggregation Architecture

**Purpose**: Evaluate how each component of PLOSHA's aggregation pipeline contributes to performance.

**Design**: Runs the PLOSHA DES engine with 4 different aggregation configurations:

| Variant | `forced_micro_slots` | `hierarchical_aggregation` | What it tests |
|---------|---------------------|---------------------------|---------------|
| Flat-Epoch | 1 | false | Single-slot, no hierarchy (baseline) |
| Fixed-Slot | √N | false | Static slot count, no hierarchy |
| Adaptive-Slot | 0 (optimizer) | false | Adaptive m* optimizer, no hierarchy |
| Full PLOSHA | 0 (optimizer) | true | Full pipeline (ours) |

**Sweep**: `num_sensors` = 500, 1000, 1500, ..., 5000  
**Metrics**: `aggregation_latency_ms`, `processing_overhead_ms`, `loss_exposure_fraction`  
**Output**: `schemes/plosha_rmfr/exp1_ablation_aggregation/results.csv`  
**Plot**: `plots/output/graph1_ablation_aggregation.png`

---

## Experiment 9: Scheduling Efficiency

**Purpose**: Compare scheduling decision speed and load balancing across all schemes.

**Design**: Each scheme times its scheduling/assignment decision phase and computes workload imbalance $I_W = \sqrt{\frac{1}{|F|}\sum_{i}(W_i - \bar{W})^2}$.

| Scheme | What is timed |
|--------|--------------|
| PLOSHA-RMFR | EWMA prediction + fog evaluation loop |
| FedDQN (Ref[22]) | DQN `SelectAction()` inference |
| FT-Workflow (Ref[37]) | Performance fluctuation scheduling |
| FT-Serverless Edge (Ref[38]) | `algorithmFwk()` DAG placement |

**Sweep**: `num_fog_nodes` = 5, 10, 15, ..., 50  
**Metrics**: `scheduling_latency_ms`, `workload_imbalance`  
**Output**: `schemes/<scheme>/exp2_scheduling_efficiency/results.csv`  
**Plot**: `plots/output/graph2_scheduling_efficiency.png`

---

## Files Changed

### PLOSHA-RMFR (`schemes/plosha_rmfr/src/`)

- **`config.hpp`** — Added `bool hierarchical_aggregation = true` to `ExperimentConfig`
- **`metrics.hpp`** — Added `scheduling_latency_ms`, `workload_imbalance`, `processing_overhead_ms` to `EpochMetrics` and `SweepPointResult`. Added `AblationRow` struct, `writeAblationResultsFile()`, `writeSchedulingResultsFile()`
- **`metrics.cpp`** — Updated `computeAverages()` for new fields. Implemented ablation and scheduling CSV writers
- **`plosha.hpp`** — Added `processing_overhead_ms` to `AggregationResult`. Added `hierarchical` parameter to `aggregate()`
- **`plosha.cpp`** — Tracks processing overhead (TEE transform + micro-slot agg time). Conditionally skips fog-level hierarchy step when `hierarchical=false`
- **`des_engine.hpp`** — Added `runExp1_AblationAggregation()` and `runExp2_SchedulingEfficiency()` declarations
- **`des_engine.cpp`** — Added scheduling timing in `runEpoch()` (around EWMA prediction). Added workload imbalance computation. Passes `hierarchical_aggregation` to `aggregate()`. Implemented `runExp1` (4 variants × 10 sensor values) and `runExp2` (fog sweep). Updated `runExperiment()`/`runAll()` for experiments 1–6
- **`main.cpp`** — Updated help text from `1-7` to `1-6`

### FedDQN (`schemes/fed_dqn/src/`)

- **`fed_dqn_sim.hpp`** — Added `scheduling_latency_ms`, `workload_imbalance` to `FedDQNMetrics`
- **`fed_dqn_sim.cpp`** — Added `chrono` timing around `SelectAction()` call in the main scheduling loop. Computes workload imbalance from per-node `tasks_assigned` distribution
- **`exp2_main.cpp`** — **[NEW FILE]** Experiment 2 binary for FedDQN. Sweeps fog nodes 5–50, outputs scheduling CSV
- **`Makefile`** — Added `exp2_scheduling_efficiency` build target

### FT-Workflow (`schemes/fault_tolerant_workflow/src/`)

- **`ft_engine.hpp`** — Added `runExp2_SchedulingEfficiency()` declaration
- **`ft_engine.cpp`** — Added scheduling timing in `runEpoch()` (around performance fluctuation update). Added workload imbalance from queue load. Implemented `runExp2` with scheduling CSV writer. Updated `runExperiment()`/`runAll()` for experiment 2
- **`metrics.hpp`** — Added `scheduling_latency_ms`, `workload_imbalance` to `EpochMetrics` and `SweepPointResult`
- **`metrics.cpp`** — Updated `computeAverages()` for new fields
- **`main.cpp`** — Updated help text to include experiment 2

### FT-Serverless Edge (`schemes/ft_serverless_edge/src/`)

- **`ft_serverless_edge.hpp`** — Added `runExp2_SchedulingEfficiency()` declaration
- **`ft_experiments.cpp`** — Added case 2 in `run()` switch. Implemented `runExp2`: times `algorithmFwk()` placement across all requests, computes cloudlet load imbalance

### Plot Script

- **`plots/generate_plots.py`** — Contains `plot_exp1_ablation_aggregation()` and `plot_exp2_scheduling_efficiency()`. These read the correct CSV column names that the C++ code produces

### Benchmark Runner

- **`run_benchmark.sh`** — Added `./exp2_scheduling_efficiency` run for FedDQN. Added exp2 sweep loop for FT-Serverless Edge (fog counts 5–50)

### Docs

- **`docs/implementation_plan.md`** — Implementation plan for both experiments

---

## How to Build and Run on Linux

```bash
# Option 1: Run everything
bash run_benchmark.sh

# Option 2: Run individually

# PLOSHA-RMFR experiments 8 & 9
cd schemes/plosha_rmfr/src
make clean && make
./plosha_rmfr --experiment 8 --epochs 10 --dataset ../../../dataset/plosha_dataset.csv --output ..
./plosha_rmfr --experiment 9 --epochs 10 --dataset ../../../dataset/plosha_dataset.csv --output ..

# FedDQN experiment 9
cd schemes/fed_dqn/src
make clean && make
./exp2_scheduling_efficiency

# FT-Workflow experiment 9
cd schemes/fault_tolerant_workflow/src
make clean && make
./ftworkflow --experiment 9 --dataset ../../../dataset/plosha_dataset.csv

# FT-Serverless Edge experiment 9
cd schemes/ft_serverless_edge/src
make clean && make
for v in 5 10 15 20 25 30 35 40 45 50; do
  ./ft_serverless_sim --experiment 9 --variable $v --cloudlets $v \
    --dataset ../../../../dataset/plosha_dataset.csv
done

# Generate plots from real data
cd plots
python3 generate_plots.py
```

## What Was NOT Changed

- **No fake/synthetic data** — all previously created synthetic CSVs were deleted
- **Existing experiments 1–7 are untouched** — backward compatible
- **No new .cpp files for PLOSHA-RMFR** — exp8/exp9 are inside `des_engine.cpp`, built by existing Makefile
- **No Makefile changes for FT-Workflow or FT-Serverless Edge** — same source files
