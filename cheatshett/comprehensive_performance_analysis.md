# Comprehensive Performance Analysis: Experiments 1 through 9

This document details the configuration setup, focus metrics, and mathematical/architectural reasons for performance outcomes across all 9 experiments in the PLOSHA-RMFR benchmark.

---

## Experiment 1: Sensor Scalability
*   **Configuration:** Sweeps `num_sensors` [500, 1000, 1500, ..., 5000]. Fixed: 10 fog nodes, 0% failure rate.
*   **Focus Metric:** Aggregation Latency.
*   **Performance Analysis:** Tests the raw cryptographic processing power of the system. As sensors scale up, the number of Paillier homomorphic additions scales linearly. PLOSHA efficiently distributes this across the 10 fog nodes. The absence of failure injection allows us to measure pure baseline cryptographic overhead.

## Experiment 2: Fog Node Scalability
*   **Configuration:** Sweeps `num_fog_nodes` [5, 10, 15, ..., 50]. Fixed: 2000 sensors, 0% failure rate.
*   **Focus Metric:** Aggregation Latency & Scheduling Latency.
*   **Performance Analysis:** As fog nodes increase, the load per node (2000/N) decreases. We observe an inverse relationship where aggregation latency drops significantly as more fog nodes are added to parallelize the TEE-enclave Paillier encryptions.

## Experiment 3: Workload Intensity
*   **Configuration:** Sweeps `workload_multiplier` [1x, 2x, ..., 10x]. Fixed: 1000 sensors, 10 fog nodes, 5% failure rate.
*   **Focus Metric:** Aggregation Latency under network stress.
*   **Performance Analysis:** This mimics sensor bursts. At 10x, each fog node processes 1000 cryptographic operations. The system maintains stability because the EWMA predictor automatically adjusts scheduling weights as queues fill up, distributing the burst away from congested nodes.

## Experiment 4: Failure Rate vs. Recovery Latency
*   **Configuration:** Sweeps `failure_rate` [2%, 4%, ..., 20%]. Fixed: 1000 sensors, 10 fog nodes.
*   **Baselines:** FedDQN, FT-Workflow, FT-Serverless Edge.
*   **Performance Analysis (Why We Win):** PLOSHA-RMFR achieves near-zero recovery latency (~0.13ms) regardless of the failure rate. This is because **MicroRecovery** physically isolates failures. Instead of restarting the epoch or spinning up a new container (like *FT-Serverless Edge*), the framework simply re-aggregates the localized micro-slots that went missing. *FedDQN* maintains a constant ~2ms because it has to re-evaluate the whole system state rather than fixing just the missing chunk.

## Experiment 5: Aggregation-Loss Exposure
*   **Configuration:** Sweeps `forced_micro_slots` [1, 2, ..., 20]. Fixed: 1000 sensors, 10 fog nodes, 10% failure rate.
*   **Baselines:** Robust IIoT, FT-Workflow.
*   **Performance Analysis (Why We Win):** 
    *   **Robust IIoT:** Uses monolithic aggregation. If an edge server crashes, all Paillier ciphertext data on that server is destroyed. If 1 of 5 servers crashes, the system instantly loses 20% of its data.
    *   **PLOSHA-RMFR:** By chunking data into up to 20 micro-slots, a crash only destroys the active, un-aggregated slot (5% of that node's data). Since 10 nodes share the load, a node crash only results in a system-wide data loss of **0.5%**. The loss mathematically drops following $1/m^*$.

## Experiment 6: Recovery Communication Overhead
*   **Configuration:** Sweeps `incomplete_micro_slots` [1, 2, ..., 10]. Fixed: 1000 sensors, 10 fog nodes, 10% failure rate.
*   **Baselines:** FT-Workflow, FT-Serverless Edge.
*   **Performance Analysis (Why We Win):** In `rmfr.cpp`, the payload for recovery transmits the exact byte sizes of the OpenSSL `BIGNUM` Paillier ciphertexts. Because we only transmit the ciphertexts of the specific failed micro-slot, the overhead remains minimal (**~0.5 to 5 KB**). *FT-Serverless Edge* transmits entire container states and memory execution contexts, scaling up to **~400 KB**.

## Experiment 7: Effectiveness of AFLTO
*   **Configuration:** Compares AFLTO enabled vs. disabled. Evaluates over 20 epochs with a dynamic, hardcoded `failure_rate` (spiking to 30%) and `workload_multiplier` (spiking to 10x) array. Fixed: 1000 sensors, 10 fog nodes.
*   **Performance Analysis (Why We Win):** Without AFLTO, static timeouts cause impatient behavior; if the network is just slow (due to the 10x workload), valid data is prematurely dropped. AFLTO mathematically adjusts thresholds ($\tau_1, \tau_2, \tau_3$), learning to wait for delayed data. This pushes System Availability to **100%** and Completeness to **95.4%**.

## Experiment 8: Ablation of PLOSHA Aggregation
*   **Configuration:** Sweeps `num_sensors` [500 to 5000]. Tests 4 variants: `flat_epoch` (m=1), `fixed_slot` (m=10), `adaptive_slot` (optimizer, no hierarchy), `full_plosha` (optimizer + hierarchy). Fixed: 10 fog nodes, 10% failure rate.
*   **Performance Analysis:**
    *   **Full PLOSHA vs Adaptive Slot (The Latency Convergence):** The results show Adaptive-Slot and Full-PLOSHA having nearly identical latency. Why? The baseline Paillier encryption cost (~200ms for 200 readings) dominates the metric. Full-PLOSHA uses homomorphic addition to merge slots (taking ~0.01ms), while Adaptive-Slot doesn't merge them, resulting in a mathematical ECDSA verification penalty (taking ~0.5ms). The 0.49ms difference is swallowed by the 200ms base cost, making them graph identically. However, Full-PLOSHA is still strictly superior because it requires far less network bandwidth (1 signature instead of $m$ signatures sent to the cloud).

## Experiment 9: Scheduling Efficiency
*   **Configuration:** Sweeps `num_fog_nodes` [5 to 50]. Fixed: exactly 12600 sensors (LCM for perfect load distribution), 0% failure rate.
*   **Baselines:** FedDQN, FT-Workflow, FT-Serverless Edge.
*   **Performance Analysis (The Trade-Off):** 
    *   **Why we lose to FedDQN in pure speed:** PLOSHA's orchestrator calculates EWMA equations (Workload, Queue, Latency, Risk) dynamically. *FedDQN* executes a pre-trained Deep Q-Network, where inference is virtually instantaneous (an O(1) array multiplication). 
    *   **Why we still win the architecture war:** FedDQN completely fails if the IoT environment undergoes out-of-distribution shifts (which require retraining). PLOSHA mathematically adapts in real-time. Additionally, PLOSHA heavily outperforms *FT-Serverless Edge*, which suffers massive latency spikes due to orchestrating container routing.
