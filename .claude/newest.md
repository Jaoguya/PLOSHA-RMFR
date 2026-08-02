# Experiment 2 (Scheduling Efficiency) Alignment Summary

## What the Experiment Really Measures

**Experiment 2 (in the paper) / Experiment 9 (in the code)** measures the **Scheduling Efficiency** of the system under severe, heterogeneous conditions. Its primary goal is to evaluate how effectively a scheduling algorithm can distribute a sudden, heavy workload across an unbalanced fog network without overwhelming individual nodes.

The experiment tests the scheduler's intelligence by intentionally trying to break the system using four combined stressors:
1. **Heterogeneous Capacities (`heterogeneity_ratio = 5.0`)**: The network is artificially scaled so that Node 0 has the absolute weakest processing capacity, while Node N-1 is extremely powerful.
2. **Workload Hotspot (25% to Node 0)**: A massive 25% of all traffic from the entire network is forced directly onto Node 0—the weakest node in the entire fog layer. A naive scheduler will let Node 0 queue up and die; a smart scheduler will immediately route that traffic to the stronger nodes.
3. **Workload Burst**: At epoch 12, the entire network's workload is suddenly multiplied by 1.5x (a 50% traffic spike).
4. **Node Degradation**: At epoch 21, 20% of the nodes suffer simulated hardware degradation, severely slowing down their processing speed.

### The Key Metric: Workload Imbalance ($I_W$)
The performance is quantified by **Capacity-Normalized Workload Imbalance**. 
It measures how "fair" the load distribution is relative to the *capacity* of the nodes. If Node 0 has 10% of the network's capacity, it should only handle 10% of the load. The metric calculates the variance from this ideal baseline. A perfectly balanced system has $I_W = 0$.

---

## What I Changed to Ensure Fairness

To ensure a journal reviewer could not find any flaws or unfair advantages given to your PLOSHA model, I thoroughly audited and patched all three baseline schemes to ensure they faced the exact same extreme conditions as PLOSHA, and measured the results the exact same way.

### 1. FedDQN (Ref[22])
- **Fixed Metric Hiding:** FedDQN was originally calculating its workload imbalance by only counting tasks that successfully entered a VM's queue. Because queues are capped, the massive 25% hotspot hitting Node 0 was simply being rejected/dropped and *not counted* in the imbalance metric, making FedDQN artificially look perfectly balanced. I fixed the metric to count `tasks_assigned + tasks_rejected` so the true burden placed on the node is accurately penalized.
- **Fixed Heterogeneity Configuration:** I discovered that `exp9_main.cpp` was neglecting to pass the heterogeneity parameter, meaning FedDQN was running in a perfectly homogeneous environment while PLOSHA was struggling with weak nodes. I explicitly patched it to initialize with `heterogeneity_ratio = 5.0`.

### 2. Fault-Tolerant Workflow (Ref[37])
- **Fixed Heterogeneity Configuration:** Similar to FedDQN, FT-Workflow's execution script was falling back to a default `heterogeneity_ratio = 1.0`. I explicitly overrode this inside `ft_engine.cpp` for Experiment 9 to enforce the `5.0` ratio.
- **Standardized Metric:** I updated FT-Workflow's workload imbalance calculation to explicitly divide by the node's individual capacity share, matching PLOSHA's exact mathematical formula. (Since FT-Workflow does not perform horizontal load balancing, its imbalance correctly reflects the input hotspot).

### 3. FT-Serverless Edge (Ref[38])
- **Enforced the 25% Hotspot:** The original dataset loading for FT-Serverless relied on a modulo-based node assignment that did not match the 25%-to-Node-0 hotspot the other schemes faced. I patched `ft_experiments.cpp` to enforce the exact same hotspot logic.
- **Corrected Measurement Target:** FT-Serverless was measuring the origin distribution of the requests, which is identical for every scheme and completely blind to the algorithm's actual placement decisions. I updated it to measure the *post-placement* load distribution on the cloudlets.

### 4. PLOSHA (Ours)
- **POSIX Compatibility:** Fixed Windows-specific header compilation errors in `des_engine.cpp` (`<direct.h>` -> `<unistd.h>`) so it compiles cleanly on Linux environments for standard scientific reproducibility.

### Conclusion
With these changes, **every single model** is now subject to the exact same 5-to-50 node sweep, the exact same 30 epochs, the exact same burst and degradation timelines, the exact same severe capacity imbalance, the exact same 25% hotspot, and the exact same rigorous mathematical formula for Workload Imbalance. The simulation is now 100% fair and rigorous.
