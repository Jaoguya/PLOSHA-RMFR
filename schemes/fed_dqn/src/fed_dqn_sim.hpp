#ifndef FED_DQN_SIM_HPP
#define FED_DQN_SIM_HPP

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <random>
#include <chrono>
#include <deque>
#include <tuple>
#include <cstdint>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <functional>
#include "dqn_network.hpp"

// ============================================================================
// Data structures for the FedDQN task scheduling simulation
// ============================================================================

// A task derived from the dataset
struct Task {
    int id;
    std::string sensor_id;
    std::string fog_node_id;
    double execution_time;    // Derived from Connection_Duration + processing
    double deadline;          // Derived from task characteristics
    double cpu_requirement;   // Derived from Bytes_Transferred
    int priority;             // 0=Low, 1=Medium, 2=High (set by K-Means)
    double arrival_time;      // When the task arrives in the simulation
    double start_time;        // When execution begins
    double completion_time;   // When execution finishes
    bool rejected;
    bool sla_violated;
};

// Virtual Machine on a fog node
struct VirtualMachine {
    int id;
    double cpu_capacity;       // MIPS
    double memory_mb;          // MB
    double power_watts;        // Power consumption rate
    double available_time;     // Next available time for scheduling
    std::queue<int> task_queue; // Task IDs in queue
    int tasks_completed;
    double total_energy;       // Cumulative energy consumed
};

// Experience tuple for DQN replay buffer
struct Experience {
    std::vector<double> state;       // [queue_len, cpu_req, priority]
    int action;                       // VM index chosen
    double reward;                    // Reward received
    std::vector<double> next_state;  // Next state after action
};

// Q-Table entry (tabular Q-learning as faithful DES approximation)
// Key: discretized state → action → Q-value
struct QTable {
    // Maps (state_hash) -> vector of Q-values (one per action/VM)
    std::map<uint64_t, std::vector<double>> table;

    // Hash a state vector into a key
    static uint64_t HashState(const std::vector<double>& state);

    // Get Q-values for a state (initialize if missing)
    std::vector<double>& GetQ(const std::vector<double>& state, int num_actions);

    // Get the best action for a state
    int GetBestAction(const std::vector<double>& state, int num_actions);

    // Update Q-value
    void Update(const std::vector<double>& state, int action, double target,
                double alpha, int num_actions);
};

// Fog Node with VMs and local DQN agent
struct FogNode {
    int id;
    std::vector<VirtualMachine> vms;
    DQNNetwork dqn;                       // Real DNN agent
    std::deque<Experience> replay_buffer; // Experience replay
    int max_replay_size;

    // Metrics
    int tasks_assigned;
    int tasks_rejected;
    int sla_violations;
    double total_makespan;
};

// K-Means cluster result
struct KMeansResult {
    std::vector<int> assignments;         // Cluster assignment per task
    std::vector<std::vector<double>> centroids; // K centroids
};

// Simulation metrics for a single run
struct FedDQNMetrics {
    double aggregation_latency_ms;  // Simulated per-task response time + sync overhead
    double makespan_ms;             // max(completion) - min(start) in simulation time
    double throughput;              // tasks_completed / makespan
    double energy_total;            // Sum of all energy across all fog nodes
    double sla_violation_rate;      // Fraction of tasks violating SLA
    double task_rejection_rate;     // Fraction of tasks rejected
    double avg_queue_utilization;   // Average queue length / capacity
    int recovery_count;             // Number of fog node failures recovered
    double recovery_latency_ms;     // Simulated recovery latency (VM reset + reschedule)
    double scheduling_latency_ms;   // Wall-clock time for DQN action selection
    double workload_imbalance;      // I_W = sqrt(1/|F| * sum((W_i - W_bar)^2))
    double convergence_time_epochs; // Episodes needed to converge after burst
};

// ============================================================================
// Main simulation class
// ============================================================================

class FedDQNSimulation {
private:
    // Configuration
    int num_fog_nodes_;
    int num_vms_per_node_;
    int num_sensors_;
    double gamma_;            // Discount factor
    double epsilon_;          // Exploration rate
    double epsilon_min_;
    double epsilon_decay_;
    double alpha_;            // Learning rate
    int batch_size_;
    int target_update_freq_;
    int num_episodes_;
    int federated_period_;    // Aggregate every N scheduling periods
    double failure_rate_;     // Probability of fog node failure per round
    // Ratio between the most- and least-capable fog node. 1.0 = homogeneous
    // (default). Mirrors the identical field in PLOSHA and FT-Workflow so all
    // four schemes face the same node-capability distribution in Exp2/Exp9.
    double heterogeneity_ratio_ = 1.0;

    // System entities
    std::vector<FogNode> fog_nodes_;
    std::vector<Task> tasks_;
    std::vector<Task> all_parsed_tasks_; // Full dataset

    // RNG
    std::mt19937 rng_;

    // Real crypto state (for fair wall-clock comparison)
    std::vector<uint8_t> aes_key_;      // AES-256-GCM key
    bool crypto_initialized_ = false;
    void initCrypto();
    // AES-GCM encrypt: returns total bytes of ciphertext produced
    int aesEncryptInPlace(const uint8_t* plaintext, int plaintext_len,
                          uint8_t* ciphertext_out);

    // Internal methods
    void ParseDatasetToTasks(int num_tasks);
    KMeansResult RunKMeans(int k, int max_iter = 100, double tol = 1e-4);
    void AssignPriorities(const KMeansResult& km);

    // DQN methods
    std::vector<double> GetState(const FogNode& node, const Task& task);
    int SelectAction(const FogNode& node, const std::vector<double>& state);
    double ComputeReward(const Task& task, const VirtualMachine& vm);
    void TrainFromReplay(FogNode& node);

    // Federated averaging
    void FederatedAggregate();

    // Fog node failure simulation
    void SimulateFailures(std::vector<bool>& failed_nodes);
    void RecoverFromFailure(int node_id, int& recovery_count,
                            double& total_recovery_latency);

public:
    FedDQNSimulation();
    ~FedDQNSimulation() = default;

    // Load and parse dataset
    bool LoadDataset(const std::string& csv_path);

    // Configure simulation parameters
    void Configure(int num_fog_nodes, int num_vms_per_node, int num_sensors,
                   int num_episodes = 10, double failure_rate = 0.0,
                   double heterogeneity_ratio = 1.0);

    // Set hyperparameters (defaults from paper)
    void SetHyperparameters(double gamma = 0.95, double epsilon = 1.0,
                             double epsilon_min = 0.01, double epsilon_decay = 0.995,
                             double alpha = 0.001, int batch_size = 32,
                             int target_update_freq = 10, int federated_period = 5);

    // Run the full DES simulation
    FedDQNMetrics Run();

    // Accessors
    int GetNumFogNodes() const { return num_fog_nodes_; }
    int GetNumSensors() const { return num_sensors_; }
};

#endif // FED_DQN_SIM_HPP
