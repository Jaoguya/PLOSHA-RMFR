# Evaluation Graphs — Explanation and Rationale

This document explains the purpose, methodology, and key findings behind each of the **6 evaluation graphs** produced by the PLOSHA-RMFR benchmark.

## Overview

| # | Graph | File | Type | Key Finding |
|---|-------|------|------|-------------|
| 1 | Ablation of Aggregation Architecture | `graph1_ablation_aggregation.png` | Grouped bar chart | Every PLOSHA layer reduces latency |
| 2 | Scheduling Efficiency | `graph2_scheduling_efficiency.png` | Line plot (log Y) | PLOSHA scales sub-ms across fog nodes |
| 3 | Failure Rate vs. Recovery Latency | `graph3_failure_rate.png` | Line plot | PLOSHA achieves near-zero recovery |
| 4 | Loss Exposure Fraction | `graph4_loss_exposure.png` | Line plot | Micro-slots reduce loss as 1/K |
| 5 | Recovery Communication Overhead | `graph5_recovery_comm.png` | Line plot | PLOSHA recovery is bandwidth-efficient |
| 6 | AFLTO Ablation Study | `graph6_aflto_ablation.png` | Grouped bar chart | AFLTO adds +8.2% completeness |

---

## Graph 1 — Ablation of PLOSHA Aggregation Architecture

**File:** `graph1_ablation_aggregation.png`
**Type:** Grouped bar chart (PLOSHA-only, no baselines)
**X-axis:** Number of Sensors (1000–5000)
**Y-axis:** Aggregation Latency (ms)

### Purpose

This graph answers: *"Does every component of the PLOSHA hierarchical slot aggregation architecture actually contribute to reducing latency, or could a simpler design achieve the same result?"*

It isolates the contribution of each architectural layer by comparing four increasingly complete variants:

| Variant | Description |
|---------|-------------|
| **Flat-Epoch** | No hierarchical structure; all sensors report in a single flat epoch. |
| **Fixed-Slot** | Introduces fixed-size time slots within epochs, but without adaptive sizing. |
| **Adaptive-Slot** | Adds EWMA-based predictive slot sizing that adapts to workload patterns. |
| **Full PLOSHA** | Complete architecture with adaptive slots, micro-slot subdivision, and risk-aware scheduling. |

### Key Finding

Each successive layer reduces aggregation latency. Full PLOSHA consistently achieves the lowest latency across all sensor scales, justifying the multi-layer design.

---

## Graph 2 — Scheduling Efficiency (Workload Balancing)

**File:** `graph2_scheduling_efficiency.png`
**Type:** Line plot (Linear scale on Y-axis)
**X-axis:** Number of Fog Nodes (5–50)
**Y-axis:** Workload Imbalance ($I_W$)
**Schemes:** PLOSHA-RMFR (Ours), Ref[22] (FedDQN), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

Evaluates how efficiently each scheme balances the incoming sensor workload across fog nodes when the nodes have **heterogeneous processing capacities**. Perfect efficiency means nodes receive tasks strictly proportional to their processing capability ($I_W \approx 0$).

### Key Findings

- **PLOSHA-RMFR** maintains near-zero workload imbalance ($I_W < 0.1$) across all fog node counts. By integrating predictive states into its scheduling logic, it actively migrates tasks from weaker, congested nodes to highly capable nodes.
- **Ref[22] (FedDQN), Ref[37] (FT-Workflow), and Ref[38] (FT-Serverless Edge)** all exhibit high workload imbalance ($I_W > 0.4$) that scales poorly. Because they lack capacity-aware predictive routing, they blindly overload weaker nodes while leaving highly capable nodes underutilized.

---

## Graph 3 — Failure Rate vs. Recovery Latency

**File:** `graph3_failure_rate.png`
**Type:** Line plot
**X-axis:** Failure Rate (2%–20%)
**Y-axis:** Recovery Latency (ms)
**Schemes:** PLOSHA-RMFR (Ours), Ref[22] (FedDQN), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

Measures how quickly each scheme recovers from fog-node failures under increasing failure rates. This directly validates the **RMFR (Risk-Aware Multi-Layer Fault Recovery)** component.

### Key Findings

- **PLOSHA-RMFR** achieves near-zero recovery latency (~0.13 ms) across all failure rates. Its micro-slot architecture means only a small fraction of work is ever at risk — failed micro-slots are reassigned instantly without restarting the entire epoch.
- **Ref[38] (FT-Serverless Edge)** shows the worst recovery performance, with latency increasing sharply as failure rate rises — reflecting the cost of container-based cold-start recovery.
- **Ref[22] (FedDQN)** maintains constant but relatively high recovery latency (~2 ms).
- **Ref[37] (FT-Workflow)** performs well but cannot match PLOSHA's micro-slot-level granularity.

### Why Some Lines Are Constant

- **FedDQN (~2.05 ms flat):** Its recovery is a fixed per-node procedure (VM reset + task re-dispatch + Q-table re-sync). The failure rate changes *how many* nodes fail, but the average per-recovery cost (`total_latency / recovery_count`) stays constant because each recovery is structurally identical. The DQN policy was not trained for failure recovery optimization.
- **FT-Workflow (two plateaus: ~0.43 ms then ~0.51 ms):** It uses a threshold-based replication model. Below a certain failure count, a fixed checkpoint-based recovery activates (constant cost); above the threshold, a heavier replication recovery path triggers (a different constant cost). Within each regime, the per-recovery cost doesn't vary.

---

## Graph 4 — Loss Exposure Fraction

**File:** `graph4_loss_exposure.png`
**Type:** Line plot
**X-axis:** Number of Micro-slots (1–20)
**Y-axis:** Loss Exposure Fraction
**Schemes:** PLOSHA-RMFR (Ours), Ref[24] (Robust IIoT), Ref[37] (FT-Workflow)
**Excluded:** Ref[38] (FT-Serverless Edge — does not support micro-slot subdivision)

### Purpose

Loss exposure fraction measures the proportion of aggregation work lost when a fog-node failure occurs mid-epoch. This graph shows how increasing micro-slots (finer-grained checkpointing) reduces data at risk.

### Key Findings

- **PLOSHA-RMFR** loss exposure decreases as **1/K** (K = number of micro-slots), dropping from 10% with 1 micro-slot to below 0.5% with 20 micro-slots. A failure can only destroy the current micro-slot's partial work.
- **Ref[24] (Robust IIoT)** maintains a constant ~20% loss exposure.
- **Ref[37] (FT-Workflow)** maintains a constant ~9% loss exposure.

### Why Some Lines Are Constant

- **Robust IIoT (flat at 0.20):** Uses epoch-level Paillier homomorphic encryption with **no sub-epoch checkpointing**. The micro-slot parameter is irrelevant — it doesn't subdivide its aggregation work. When a failure occurs, the entire Paillier-encrypted partial aggregation is lost (~20% of the epoch).
- **FT-Workflow (flat at 0.09):** Its checkpoint granularity is at the fog-node level, not the micro-slot level. Each failed fog is counted as a full unit of loss (`total_loss_exposure += 1.0`), so increasing micro-slots has zero effect.

These constant lines are **architectural limitations of the baselines**, not bugs — they simply lack the micro-slot subdivision mechanism to benefit from finer granularity.

---

## Graph 5 — Recovery Communication Overhead

**File:** `graph5_recovery_comm.png`
**Type:** Line plot
**X-axis:** Incomplete Micro-slots (1–10)
**Y-axis:** Communication Overhead (KB)
**Schemes:** PLOSHA-RMFR (Ours), Ref[37] (FT-Workflow), Ref[38] (FT-Serverless Edge)

### Purpose

Measures network bandwidth consumed during fault recovery as the number of incomplete (failed) micro-slots increases. Answers: *"How much extra network traffic does recovery generate?"*

### Key Findings

- **PLOSHA-RMFR** maintains near-constant and minimal overhead (~0.5–5 KB). Each micro-slot contains only a small fraction of the epoch's data, so recovering one requires minimal retransmission.
- **Ref[38] (FT-Serverless Edge)** shows linearly increasing overhead (reaching ~400 KB at 10 incomplete micro-slots), due to full function state migration including container images and execution context.
- **Ref[37] (FT-Workflow)** exhibits moderate overhead (~15–55 KB), reflecting checkpoint-based recovery where larger workflow segments must be retransmitted.

This confirms PLOSHA's localized micro-slot recovery is bandwidth-efficient — important for resource-constrained IIoT networks.

---

## Graph 6 — AFLTO Ablation Study

**File:** `graph6_aflto_ablation.png`
**Type:** Grouped bar chart (PLOSHA-only, no baselines)
**X-axis:** Metric (Aggregation Completeness, System Availability)
**Y-axis:** Score (0–1)
**Conditions:** AFLTO Disabled vs. AFLTO Enabled

### Purpose

Validates the contribution of the **AFLTO (Adaptive Feedback Loop for Timeout Optimization)** component by toggling it on/off and measuring two key reliability metrics.

### Key Findings

| Metric | AFLTO Disabled | AFLTO Enabled | Improvement |
|--------|---------------|---------------|-------------|
| **Aggregation Completeness** | 0.882 | 0.954 | +8.2% |
| **System Availability** | 0.974 | 1.000 | +2.7% |

- **Aggregation Completeness** improves by ~8% because AFLTO prevents premature timeout-triggered failures from discarding valid partial results.
- **System Availability** reaches a perfect 1.000, meaning the system never experiences a period where it is unable to accept and process new tasks.

This confirms AFLTO is a meaningful component — not just architectural complexity.
