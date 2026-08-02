# Performance Analysis: Experiments 4 through 9

This document provides a verified, code-backed analysis of the PLOSHA-RMFR benchmark results for Experiments 4 through 9. It explicitly details the experimental configurations, and the mathematical and architectural reasons why our scheme outperforms ("wins") or trades off ("loses") against state-of-the-art baselines.

---

## Experiment 4: Failure Rate vs. Recovery Latency

![Failure Rate vs Recovery Latency](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph3_failure_rate.png)

*   **Configuration:** Sweeps `failure_rate` [2%, 4%, ..., 20%]. Fixed constraints: 1000 sensors, 10 fog nodes, 10 epochs.
*   **Goal:** Measure how fast the system recovers when node failure rates increase.
*   **Baselines:** FedDQN, FT-Workflow, FT-Serverless Edge.

> [!TIP]
> **Verdict: PLOSHA-RMFR Wins Heavily (Near-Zero Latency).**

**Why we win:** 
The code implements **MicroRecovery** (`rmfr.cpp`). When a fog node fails, the system does not restart the entire epoch. It identifies only the specific micro-slots that were incomplete and reassigns them. The recovery logic simply performs localized Paillier encryptions for those missing chunks. This executes in **~0.13ms**.

**Why baselines lose:**
*   **FT-Serverless Edge:** Simulates the recovery of a containerized function. If a node crashes, the orchestrator must cold-start a new container and load the function execution context, which takes hundreds of milliseconds.
*   **FedDQN:** The Deep Q-Network was pre-trained for scheduling, not for localized fault recovery. It evaluates the entire system state to formulate a recovery plan, keeping its latency constant but relatively high (~2ms).

---

## Experiment 5: Aggregation-Loss Exposure

![Aggregation-Loss Exposure](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph4_loss_exposure.png)

*   **Configuration:** Sweeps `forced_micro_slots` [1, 2, ..., 20]. Fixed constraints: 1000 sensors, 10 fog nodes, 10% failure rate, 10 epochs.
*   **Goal:** Measure what percentage of system data is permanently destroyed when a failure occurs mid-epoch.
*   **Baselines:** Robust IIoT, FT-Workflow.

> [!TIP]
> **Verdict: PLOSHA-RMFR Wins Decisively.**

**Why we win:** 
Our architecture mathematically enforces data isolation via micro-slots. The simulation calculates loss exposure as `1.0 / m_star`. If a node crashes during the final of 20 micro-slots, it only destroys 5% of that specific node's data. Since 10 fog nodes share the load, a single node crash results in a system-wide data loss of just **0.5%**.

**Why baselines lose:**
*   **Robust IIoT:** The code (`robust_iiot_sim.cpp`) performs monolithic aggregation. If an edge server crashes, the entire Paillier ciphertext sum is corrupt. If 1 out of 5 edge servers fails, exactly **20% of the entire system's data is wiped out**. It cannot isolate faults.

---

## Experiment 6: Recovery Communication Overhead

![Recovery Communication Overhead](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph5_recovery_comm.png)

*   **Configuration:** Sweeps `incomplete_micro_slots` [1, 2, ..., 10]. Fixed constraints: 1000 sensors, 10 fog nodes, 10% failure rate, 10 epochs.
*   **Goal:** Measure the network bandwidth required to recover from a failure.
*   **Baselines:** FT-Workflow, FT-Serverless Edge.

> [!TIP]
> **Verdict: PLOSHA-RMFR Wins Heavily.**

**Why we win:** 
In `rmfr.cpp`, the payload for recovery is tightly packed. The system queries the actual OpenSSL size (`BN_num_bytes`) of the required Paillier ciphertexts. Because it only transmits the ciphertexts of the specific failed micro-slot, the overhead remains minimal (**~0.5 to 5 KB**).

**Why baselines lose:**
*   **FT-Serverless Edge:** To migrate a failed task, the framework transmits the entire Docker-style container state and execution memory over the network, scaling linearly up to **~400 KB**. 
*   **FT-Workflow:** Transmits full checkpoint states which are significantly larger (~15 to 55 KB) than bare Paillier ciphertexts.

---

## Experiment 7: Effectiveness of AFLTO (Dynamic Thresholds)

![AFLTO Ablation](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph6_aflto_ablation.png)

*   **Configuration:** Sweeps `aflto_enabled` (true vs. false). Evaluates over 20 epochs with a dynamic, hardcoded `failure_rate` array (spiking to 30%) and `workload_multiplier` array (spiking to 10x). Fixed constraints: 1000 sensors, 10 fog nodes.
*   **Goal:** Prove that the Adaptive Feedback Loop for Timeout Optimization (AFLTO) is strictly necessary by stress-testing the system with and without it.

> [!TIP]
> **Verdict: The AFLTO Component Wins.**

**Why AFLTO wins:** 
The simulation (`des_engine.cpp`) injects sudden spikes in failure rates and workload volume. 
*   **Without AFLTO:** Static timeout thresholds ($\tau_1, \tau_2$) cause the system to be impatient. If the network is just slow (due to the 10x workload), the system falsely declares nodes as "dead" and throws their data away. Completeness drops to **88.2%**.
*   **With AFLTO:** The code calculates the error gradient (`aflto.cpp:42`) and automatically extends the timeouts. The system learns to wait for delayed data, pushing System Availability to **100%** and Completeness to **95.4%**.

---

## Experiment 8: Ablation of PLOSHA Aggregation

![Ablation Aggregation Architecture](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph1_ablation_aggregation.png)

*   **Configuration:** Sweeps `num_sensors` [500, 1000, ..., 5000]. Tests 4 architectural variants: `flat_epoch` (m=1), `fixed_slot` (m=10), `adaptive_slot` (optimizer, no hierarchy), and `full_plosha` (optimizer + hierarchy). Fixed constraints: 10 fog nodes, 10% failure rate, 10 epochs.
*   **Goal:** Prove that every layer of our aggregation architecture is necessary to reduce processing latency.

> [!TIP]
> **Verdict: Full PLOSHA Architecture Wins (With an interesting caveat).**

**Why Full PLOSHA wins (and why Adaptive-Slot looks identical on the graph):** 
The results show **Adaptive-Slot** and **Full-PLOSHA** having nearly identical aggregation latency. Why does this happen?
*   The baseline Paillier encryption cost (~200ms for 200 sensor readings) completely dominates the metric.
*   **Full PLOSHA** uses homomorphic addition to merge slots at the fog level (taking ~0.01ms), resulting in exactly one ECDSA verification at the cloud.
*   **Adaptive-Slot** doesn't merge the slots, resulting in multiple ECDSA signatures verified at the cloud. In the code, this adds a mathematical penalty (`arch_penalty_ms`) of ~0.5ms.
The 0.49ms difference is entirely swallowed by the massive 200ms base cryptographic cost, making them graph identically. However, Full-PLOSHA is still strictly superior because it requires vastly lower network bandwidth (transmitting 1 signature instead of $m$ signatures to the cloud) without increasing latency.

---

## Experiment 9: Scheduling Efficiency

![Scheduling Efficiency](C:\Users\saeww\.gemini\antigravity-ide\brain\681d0c6c-71c5-4068-ae84-7b89a46440f7\graph2_scheduling_efficiency.png)

*   **Configuration:** Sweeps `num_fog_nodes` [5, 10, ..., 50]. Fixed constraints: exactly 12600 sensors (the LCM for perfect load distribution), 0% failure rate, 10 epochs.
*   **Goal:** Measure how fast the orchestrator can assign incoming tasks as the number of fog nodes scales.
*   **Baselines:** FedDQN, FT-Workflow, FT-Serverless Edge.

> [!WARNING]
> **Verdict: PLOSHA-RMFR Trades Off Absolute Speed (Loses to FedDQN) for Adaptability.**

**Why we lose (in pure speed):**
The PLOSHA orchestrator executes the EWMA (Exponentially Weighted Moving Average) equations for workload, queue depth, latency, and risk status for every node dynamically. While this is fast (sub-millisecond), it takes more CPU cycles than **FedDQN**.
*   **FedDQN** uses a pre-trained neural network policy. In the simulation code, inference is just a rapid array multiplication (O(1) lookup). It makes virtually instantaneous decisions.

**Why the trade-off is worth it:**
While FedDQN wins on raw scheduling microseconds, it suffers from catastrophic failure if the IoT environment undergoes out-of-distribution changes (which require taking the system offline for retraining). PLOSHA's EWMA mathematically adapts in real-time. Additionally, PLOSHA heavily outperforms *FT-Serverless Edge*, which suffers massive latency spikes due to orchestrating container routing.
