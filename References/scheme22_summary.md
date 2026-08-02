# Scheme 22 — FedDQN: Technical Reference Summary

**Paper**: Choppara & Mangalampalli, "Adaptive Task Scheduling in Fog Computing Using Federated DQN and K-Means Clustering," IEEE Access, Vol. 13, 2025.

## System Architecture

- **IoT Devices** → generate tasks with varying execution times, deadlines, CPU requirements
- **Fog Nodes** → each with multiple VMs; perform local DQN-based scheduling
- **Cloud** → aggregates federated model weights

## Key Algorithms

### 1. K-Means Task Prioritization (K=3)
- Feature vector: Ti = [execution_time, deadline]
- Clusters into High (short deadline, long exec), Medium, Low (long deadline, short exec)
- Priority assignment:
  - High: ek high AND dk low
  - Low: ek low AND dk high
  - Medium: otherwise
- Convergence threshold based

### 2. Deep Q-Network (DQN) Task Scheduling
- **State** s = [queue_length_VMi, CPU_requirement_taskj, priority_taskj]
- **Action** a = select VM ∈ {VM1, ..., VMN}
- **Reward**:
  - +10 if task completed within SLA deadline
  - -5 if SLA violated
  - -10 if task rejected (insufficient resources)
- **Q-update**: Q(s,a) ← Q(s,a) + α[r + γ max_a' Q(s',a') - Q(s,a)]

### 3. Federated Averaging
- Simple: θ_global = (1/N) Σ θi
- Weighted: θ_global = (Σ ni·θi) / (Σ nj) where ni = tasks processed by node i
- Aggregation every 5 scheduling periods

## Hyperparameters (from paper)
- Learning rate α = 0.001
- Discount factor γ = 0.95
- ε starts at 1.0, decays to ε_min
- Batch size = 32
- Hidden layers: 128 neurons → 64 neurons (ReLU)
- Target update frequency: periodic

## Simulation Parameters
- Fog nodes: N (varied 5–50 for scalability experiments)
- VMs per node: configurable
- Tasks from Google Cloud Jobs dataset (mapped to plosha_dataset.csv)
- Task lengths and priority levels simulate real-world fog

## Key Metrics
- **Makespan**: max(completion_time) - min(start_time)
- **Throughput**: N_tasks / Makespan
- **Energy**: Σ (P_j × exec_time_i) for all tasks on all fog nodes
- **SLA Violation Rate**: (1/N) Σ I(completion > deadline)
- **Task Rejection Rate**: (1/N) Σ I(available_time < exec_time)
- **Resource Utilization**: Σ exec_time / total_available_time per node

## Aggregation Latency Context
For the PLOSHA benchmark, "aggregation latency" maps to the total time to process
all sensor readings through the scheduling pipeline: task arrival → priority classification
→ VM assignment → execution → completion. This is the makespan of processing N sensor
readings through the fog computing infrastructure.
