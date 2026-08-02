#include "fed_dqn_sim.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>

// ============================================================================
// QTable Implementation
// ============================================================================

uint64_t QTable::HashState(const std::vector<double> &state) {
  // Discretize state and hash it
  uint64_t hash = 0;
  for (size_t i = 0; i < state.size(); i++) {
    // Discretize to integer buckets
    int bucket = static_cast<int>(state[i] * 10.0); // 0.1 resolution
    hash ^= std::hash<int>{}(bucket) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

std::vector<double> &QTable::GetQ(const std::vector<double> &state,
                                  int num_actions) {
  uint64_t key = HashState(state);
  if (table.find(key) == table.end()) {
    table[key] = std::vector<double>(num_actions, 0.0);
  }
  return table[key];
}

int QTable::GetBestAction(const std::vector<double> &state, int num_actions) {
  auto &q = GetQ(state, num_actions);
  return static_cast<int>(std::max_element(q.begin(), q.end()) - q.begin());
}

void QTable::Update(const std::vector<double> &state, int action, double target,
                    double alpha, int num_actions) {
  auto &q = GetQ(state, num_actions);
  // Q(s,a) ← Q(s,a) + α[target - Q(s,a)]
  q[action] += alpha * (target - q[action]);
}

// ============================================================================
// FedDQNSimulation Constructor
// ============================================================================

FedDQNSimulation::FedDQNSimulation()
    : num_fog_nodes_(10), num_vms_per_node_(4), num_sensors_(1000),
      gamma_(0.95), epsilon_(1.0), epsilon_min_(0.01), epsilon_decay_(0.995),
      alpha_(0.001), batch_size_(32), target_update_freq_(10),
      num_episodes_(10), federated_period_(5), failure_rate_(0.0), rng_(42) {}

// ============================================================================
// Crypto Initialization (for fair wall-clock comparison)
// ============================================================================

void FedDQNSimulation::initCrypto() {
  if (crypto_initialized_)
    return;
  aes_key_.resize(32); // AES-256
  RAND_bytes(aes_key_.data(), 32);
  crypto_initialized_ = true;
}

int FedDQNSimulation::aesEncryptInPlace(const uint8_t *plaintext,
                                        int plaintext_len,
                                        uint8_t *ciphertext_out) {
  uint8_t iv[12];
  RAND_bytes(iv, 12);

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
  EVP_EncryptInit_ex(ctx, nullptr, nullptr, aes_key_.data(), iv);
  int out_len = 0;
  EVP_EncryptUpdate(ctx, ciphertext_out, &out_len, plaintext, plaintext_len);
  int final_len = 0;
  EVP_EncryptFinal_ex(ctx, ciphertext_out + out_len, &final_len);
  uint8_t tag[16];
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
  EVP_CIPHER_CTX_free(ctx);
  return out_len + final_len;
}

// ============================================================================
// Dataset Loading
// ============================================================================

bool FedDQNSimulation::LoadDataset(const std::string &csv_path) {
  std::ifstream file(csv_path);
  if (!file.is_open()) {
    std::cerr << "ERROR: Cannot open dataset: " << csv_path << std::endl;
    return false;
  }

  std::string line;
  std::getline(file, line); // Skip header

  all_parsed_tasks_.clear();
  int task_id = 0;

  while (std::getline(file, line)) {
    if (line.empty())
      continue;

    std::stringstream ss(line);
    std::string token;
    Task t;
    t.id = task_id++;
    t.rejected = false;
    t.sla_violated = false;
    t.start_time = 0;
    t.completion_time = 0;

    // Parse CSV columns:
    // Timestamp, Sensor_ID, Fog_Node_ID, Temperature, Pressure, Vibration,
    // Is_Failure, Protocol, Connection_Duration, Flow_Rate, Bytes_Transferred,
    // Packet_Size, Device_Role, Source_IP, Dest_IP, Label

    std::getline(ss, token, ','); // Timestamp
    std::getline(ss, t.sensor_id, ',');
    std::getline(ss, t.fog_node_id, ',');

    std::getline(ss, token, ','); // Temperature (skip for scheduling)
    std::getline(ss, token, ','); // Pressure (skip)
    std::getline(ss, token, ','); // Vibration (skip)
    std::getline(ss, token, ','); // Is_Failure (skip)
    std::getline(ss, token, ','); // Protocol (skip)

    std::getline(ss, token, ','); // Connection_Duration → execution_time
    // Connection_Duration (seconds) models per-reading processing time.
    // For sensor data aggregation, use the raw value as ms (lightweight
    // operation).
    t.execution_time = std::stod(token); // 0.001–2.0 ms per reading
    if (t.execution_time < 0.001)
      t.execution_time = 0.001;

    std::getline(ss, token, ','); // Flow_Rate
    double flow_rate = std::stod(token);

    std::getline(ss, token, ','); // Bytes_Transferred → cpu_requirement
    double bytes = std::stod(token);
    t.cpu_requirement = bytes / 100.0; // Normalize to MIPS-like units

    // Deadline: based on execution time + slack
    // Tasks with high bytes/low connection_duration get tighter deadlines
    double urgency = (bytes > 0) ? (flow_rate / bytes) : 0.001;
    t.deadline = t.execution_time * (1.5 + urgency * 10.0);
    if (t.deadline < t.execution_time * 1.1)
      t.deadline = t.execution_time * 1.1;

    t.arrival_time = 0; // Will be set during simulation
    t.priority = 0;     // Will be set by K-Means

    all_parsed_tasks_.push_back(t);
  }

  file.close();
  std::cout << "Loaded " << all_parsed_tasks_.size() << " tasks from dataset."
            << std::endl;
  return !all_parsed_tasks_.empty();
}

// ============================================================================
// Task Selection and Preparation
// ============================================================================

void FedDQNSimulation::ParseDatasetToTasks(int num_tasks) {
  tasks_.clear();
  tasks_.reserve(num_tasks);

  for (int i = 0; i < num_tasks; i++) {
    Task t = all_parsed_tasks_[i % all_parsed_tasks_.size()];
    t.id = i;
    
    // FAIRNESS (workload imbalance): apply the SAME 25%-to-node-0
    // hotspot formula PLOSHA/FT-Serverless/FT-Workflow use. 
    // Otherwise FedDQN receives perfectly uniform traffic.
    int n_cl = std::max(1, num_fog_nodes_);
    int current_sensors = std::max(1, num_tasks);
    int origin = (i < static_cast<int>(current_sensors * 0.25))
                     ? 0
                     : 1 + (i % std::max(1, n_cl - 1));
    t.fog_node_id = origin;
    
    t.rejected = false;
    t.sla_violated = false;
    t.arrival_time = i * (0.001); // Tight arrival window within epoch
    tasks_.push_back(t);
  }
}

// ============================================================================
// K-Means Clustering for Task Prioritization
// ============================================================================

KMeansResult FedDQNSimulation::RunKMeans(int k, int max_iter, double tol) {
  KMeansResult result;
  int n = static_cast<int>(tasks_.size());
  result.assignments.resize(n, 0);

  // Feature vectors: [execution_time, deadline]
  std::vector<std::vector<double>> features(n);
  for (int i = 0; i < n; i++) {
    features[i] = {tasks_[i].execution_time, tasks_[i].deadline};
  }

  // Normalize features
  double max_exec = 1.0, max_dead = 1.0;
  for (auto &f : features) {
    max_exec = std::max(max_exec, f[0]);
    max_dead = std::max(max_dead, f[1]);
  }
  for (auto &f : features) {
    f[0] /= max_exec;
    f[1] /= max_dead;
  }

  // Initialize centroids randomly from data
  result.centroids.resize(k);
  std::uniform_int_distribution<int> dist(0, n - 1);
  for (int c = 0; c < k; c++) {
    result.centroids[c] = features[dist(rng_)];
  }

  // Iterate
  for (int iter = 0; iter < max_iter; iter++) {
    // Assignment step
    for (int i = 0; i < n; i++) {
      double min_dist = std::numeric_limits<double>::max();
      for (int c = 0; c < k; c++) {
        double d = 0;
        for (size_t f = 0; f < features[i].size(); f++) {
          double diff = features[i][f] - result.centroids[c][f];
          d += diff * diff;
        }
        if (d < min_dist) {
          min_dist = d;
          result.assignments[i] = c;
        }
      }
    }

    // Update step
    std::vector<std::vector<double>> new_centroids(k,
                                                   std::vector<double>(2, 0.0));
    std::vector<int> counts(k, 0);
    for (int i = 0; i < n; i++) {
      int c = result.assignments[i];
      new_centroids[c][0] += features[i][0];
      new_centroids[c][1] += features[i][1];
      counts[c]++;
    }

    double max_shift = 0;
    for (int c = 0; c < k; c++) {
      if (counts[c] > 0) {
        new_centroids[c][0] /= counts[c];
        new_centroids[c][1] /= counts[c];
      }
      double shift = 0;
      for (size_t f = 0; f < 2; f++) {
        double diff = new_centroids[c][f] - result.centroids[c][f];
        shift += diff * diff;
      }
      max_shift = std::max(max_shift, shift);
    }

    result.centroids = new_centroids;

    if (max_shift < tol)
      break;
  }

  return result;
}

void FedDQNSimulation::AssignPriorities(const KMeansResult &km) {
  int k = static_cast<int>(km.centroids.size());

  // Compute average execution_time and deadline per cluster (on normalized
  // features) High priority: high exec_time + low deadline (urgent, heavy
  // tasks) Low priority: low exec_time + high deadline (non-urgent, light
  // tasks)

  struct ClusterInfo {
    double avg_exec;
    double avg_dead;
    int original_idx;
  };
  std::vector<ClusterInfo> info(k);

  // Count tasks and accumulate
  std::vector<int> counts(k, 0);
  std::vector<double> sum_exec(k, 0), sum_dead(k, 0);
  for (size_t i = 0; i < tasks_.size(); i++) {
    int c = km.assignments[i];
    sum_exec[c] += tasks_[i].execution_time;
    sum_dead[c] += tasks_[i].deadline;
    counts[c]++;
  }

  for (int c = 0; c < k; c++) {
    info[c].avg_exec = (counts[c] > 0) ? sum_exec[c] / counts[c] : 0;
    info[c].avg_dead = (counts[c] > 0) ? sum_dead[c] / counts[c] : 0;
    info[c].original_idx = c;
  }

  // Sort by urgency score: high exec / low deadline → high priority
  // Score = avg_exec / avg_dead (higher = more urgent)
  std::sort(info.begin(), info.end(),
            [](const ClusterInfo &a, const ClusterInfo &b) {
              double score_a = (a.avg_dead > 0) ? a.avg_exec / a.avg_dead : 0;
              double score_b = (b.avg_dead > 0) ? b.avg_exec / b.avg_dead : 0;
              return score_a > score_b; // Descending
            });

  // Map cluster index → priority (0=high, 1=medium, 2=low → remap to 2=high,
  // 1=med, 0=low)
  std::map<int, int> cluster_to_priority;
  for (int rank = 0; rank < k; rank++) {
    // rank 0 = most urgent → priority 2 (High)
    // rank 1 = medium → priority 1
    // rank 2 = least urgent → priority 0 (Low)
    cluster_to_priority[info[rank].original_idx] = (k - 1) - rank;
  }

  for (size_t i = 0; i < tasks_.size(); i++) {
    tasks_[i].priority = cluster_to_priority[km.assignments[i]];
  }
}

// ============================================================================
// Configuration
// ============================================================================

void FedDQNSimulation::Configure(int num_fog_nodes, int num_vms_per_node,
                                 int num_sensors, int num_episodes,
                                 double failure_rate,
                                 double heterogeneity_ratio) {
  num_fog_nodes_ = num_fog_nodes;
  num_vms_per_node_ = num_vms_per_node;
  num_sensors_ = num_sensors;
  num_episodes_ = num_episodes;
  failure_rate_ = failure_rate;
  heterogeneity_ratio_ = heterogeneity_ratio;

  // Initialize fog nodes
  fog_nodes_.clear();
  fog_nodes_.resize(num_fog_nodes);

  std::uniform_real_distribution<double> mem_dist(512.0, 4096.0);
  std::uniform_real_distribution<double> pow_dist(50.0, 200.0);
  
  double mean_cpu = 1250.0;
  double r = heterogeneity_ratio_;
  int F = num_fog_nodes;

  for (int i = 0; i < num_fog_nodes; i++) {
    fog_nodes_[i].id = i;
    fog_nodes_[i].tasks_assigned = 0;
    fog_nodes_[i].tasks_rejected = 0;
    fog_nodes_[i].sla_violations = 0;
    fog_nodes_[i].total_makespan = 0;
    fog_nodes_[i].max_replay_size = 1000;

    // Initialize real DQN network: Input[3] → Dense[128] → ReLU → Dense[64] → ReLU → Dense[num_vms]
    fog_nodes_[i].dqn.init(3, num_vms_per_node, rng_);

    // Per-node capability scale under modelled heterogeneity. Identical formula
    // to PLOSHA (des_engine.cpp) and FT-Workflow (ft_engine.cpp): capacity is
    // spread linearly from mean*2/(1+r) to mean*2r/(1+r) so the strongest node
    // is r times the weakest, while TOTAL system capacity is unchanged from the
    // homogeneous case. r = 1.0 leaves every node identical, so experiments
    // that do not model heterogeneity are bit-identical to before.
    double hscale = 1.0;
    if (heterogeneity_ratio_ > 1.0 && num_fog_nodes > 1) {
      double frac = static_cast<double>(i) / (num_fog_nodes - 1);
      hscale = (2.0 / (1.0 + heterogeneity_ratio_)) *
               (1.0 + frac * (heterogeneity_ratio_ - 1.0));
    }

    fog_nodes_[i].vms.resize(num_vms_per_node);

    for (int v = 0; v < num_vms_per_node; v++) {
      fog_nodes_[i].vms[v].id = v;
      // Scale CPU/memory by node capability so a weak node's VMs really are
      // weaker; power scales inversely (a weak node is less efficient).
      // Per-VM randomness is retained and then scaled, so VMs still differ
      // within a node while the node's overall capability follows hscale.
      fog_nodes_[i].vms[v].cpu_capacity = mean_cpu * hscale;
      fog_nodes_[i].vms[v].memory_mb = mem_dist(rng_) * hscale;
      fog_nodes_[i].vms[v].power_watts = pow_dist(rng_) / hscale;
      fog_nodes_[i].vms[v].available_time = 0;
      fog_nodes_[i].vms[v].tasks_completed = 0;
      fog_nodes_[i].vms[v].total_energy = 0;
    }
  }
}

void FedDQNSimulation::SetHyperparameters(double gamma, double epsilon,
                                          double epsilon_min,
                                          double epsilon_decay, double alpha,
                                          int batch_size,
                                          int target_update_freq,
                                          int federated_period) {
  gamma_ = gamma;
  epsilon_ = epsilon;
  epsilon_min_ = epsilon_min;
  epsilon_decay_ = epsilon_decay;
  alpha_ = alpha;
  batch_size_ = batch_size;
  target_update_freq_ = target_update_freq;
  federated_period_ = federated_period;
}

// ============================================================================
// DQN Methods
// ============================================================================

std::vector<double> FedDQNSimulation::GetState(const FogNode &node,
                                               const Task &task) {
  // State = [average_queue_length, cpu_requirement, priority]
  double avg_queue = 0;
  for (const auto &vm : node.vms) {
    avg_queue += vm.task_queue.size();
  }
  avg_queue /= node.vms.size();

  return {
      avg_queue / 10.0,             // Normalized queue length
      task.cpu_requirement / 100.0, // Normalized CPU req
      task.priority / 2.0           // Normalized priority (0-1)
  };
}

int FedDQNSimulation::SelectAction(const FogNode &node,
                                   const std::vector<double> &state) {
  // Always run the DNN forward pass to compute Q-values.
  // In a real DQN deployment, inference always executes regardless of
  // the exploration decision. The epsilon-greedy policy selects AFTER
  // the network produces its output.
  int best_action = const_cast<DQNNetwork &>(node.dqn).getBestAction(state);

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  if (dist(rng_) < epsilon_) {
    // Explore: override with random VM (but DNN inference already ran)
    std::uniform_int_distribution<int> vm_dist(0, num_vms_per_node_ - 1);
    return vm_dist(rng_);
  } else {
    // Exploit: use the DNN's recommendation
    return best_action;
  }
}

double FedDQNSimulation::ComputeReward(const Task &task,
                                       const VirtualMachine &vm) {
  if (task.rejected) {
    return -10.0; // Rejected due to resource constraints
  }
  if (task.sla_violated) {
    return -5.0; // SLA deadline missed
  }
  // Completed within deadline: +10, with energy penalty
  double energy_penalty = (vm.power_watts * task.execution_time) / 10000.0;
  return 10.0 - energy_penalty;
}

void FedDQNSimulation::TrainFromReplay(FogNode &node) {
  if ((int)node.replay_buffer.size() < batch_size_)
    return;

  // Sample a mini-batch and train via real backpropagation
  std::uniform_int_distribution<int> dist(0,
                                          (int)node.replay_buffer.size() - 1);
  for (int b = 0; b < batch_size_; b++) {
    const Experience &exp = node.replay_buffer[dist(rng_)];

    // Target: r + γ * max_a' Q(s', a') using real DNN forward pass
    auto next_q = node.dqn.forward(exp.next_state);
    double max_next_q = *std::max_element(next_q.begin(), next_q.end());
    double target = exp.reward + gamma_ * max_next_q;

    // Train via backpropagation through 3-layer MLP
    node.dqn.train(exp.state, exp.action, target, alpha_);
  }
}

// ============================================================================
// Federated Averaging
// ============================================================================

void FedDQNSimulation::FederatedAggregate() {
  if (fog_nodes_.empty())
    return;

  int num_params = fog_nodes_[0].dqn.numParameters();
  std::vector<double> global_weights(num_params, 0.0);
  double total_weight = 0.0;

  for (const auto &node : fog_nodes_) {
    double w = std::max(1, node.tasks_assigned);
    std::vector<double> local_weights = node.dqn.getWeights();
    
    for (int p = 0; p < num_params; ++p) {
      global_weights[p] += local_weights[p] * w;
    }
    total_weight += w;
  }

  // Normalize to get weighted average
  if (total_weight > 0) {
    for (int p = 0; p < num_params; ++p) {
      global_weights[p] /= total_weight;
    }
  }

  // Distribute global model back to all nodes
  for (auto &node : fog_nodes_) {
    node.dqn.setWeights(global_weights);
  }
}

// ============================================================================
// Failure Simulation
// ============================================================================

void FedDQNSimulation::SimulateFailures(std::vector<bool> &failed_nodes) {
  failed_nodes.resize(num_fog_nodes_, false);
  if (failure_rate_ <= 0)
    return;

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  for (int i = 0; i < num_fog_nodes_; i++) {
    if (dist(rng_) < failure_rate_) {
      failed_nodes[i] = true;
    }
  }
  // Ensure at least one node is alive
  bool all_failed = true;
  for (bool f : failed_nodes)
    if (!f) {
      all_failed = false;
      break;
    }
  if (all_failed)
    failed_nodes[0] = false;
}

void FedDQNSimulation::RecoverFromFailure(int node_id, int &recovery_count,
                                          double &total_recovery_latency) {
  auto start_time = std::chrono::high_resolution_clock::now();

  auto &node = fog_nodes_[node_id];

  int lost_tasks = 0;
  for (auto &vm : node.vms) {
    lost_tasks += static_cast<int>(vm.task_queue.size());
    while (!vm.task_queue.empty()) {
      vm.task_queue.pop();
    }
    vm.available_time = 0;
  }

  // Real DQN re-initialization: the failed node's learned network weights are lost
  node.dqn.init(3, num_vms_per_node_, rng_);

  // Actual dispatch overhead: push tasks to a temporary vector to simulate re-dispatch work
  std::vector<int> redispatch_buffer;
  redispatch_buffer.reserve(lost_tasks);
  for (int i = 0; i < lost_tasks; i++) {
    redispatch_buffer.push_back(i);
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  total_recovery_latency += std::chrono::duration<double, std::milli>(end_time - start_time).count();
  recovery_count++;
}

// ============================================================================
// Main Simulation Loop
// ============================================================================

FedDQNMetrics FedDQNSimulation::Run() {
  FedDQNMetrics metrics = {};

  // Initialize real crypto (AES-256-GCM)
  initCrypto();

  // Prepare tasks
  ParseDatasetToTasks(num_sensors_);

  // Run K-Means for task prioritization (K=3: High, Medium, Low)
  KMeansResult km = RunKMeans(3);
  AssignPriorities(km);

  // Sort tasks by priority (high first) then by deadline
  std::sort(tasks_.begin(), tasks_.end(), [](const Task &a, const Task &b) {
    if (a.priority != b.priority)
      return a.priority > b.priority;
    return a.deadline < b.deadline;
  });

  // Reset fog node state
  for (auto &node : fog_nodes_) {
    node.tasks_assigned = 0;
    node.tasks_rejected = 0;
    node.sla_violations = 0;
    node.total_makespan = 0;
    node.replay_buffer.clear();
    for (auto &vm : node.vms) {
      while (!vm.task_queue.empty())
        vm.task_queue.pop();
      vm.available_time = 0;
      vm.tasks_completed = 0;
      vm.total_energy = 0;
    }
  }

  // Reset epsilon for this run
  double current_epsilon = epsilon_;

  int total_tasks = static_cast<int>(tasks_.size());
  int total_rejected = 0;
  int total_sla_violations = 0;
  int recovery_count = 0;
  double sim_min_start = std::numeric_limits<double>::max();
  double sim_max_completion = 0;
  double total_energy = 0;
  double total_queue_len = 0;
  int queue_samples = 0;
  double total_task_latency =
      0; // Sum of (completion - arrival) for simulated metric
  int completed_task_count = 0; // Number of non-rejected tasks
  int federated_sync_count = 0; // Number of federated averaging rounds
  double total_recovery_latency =
      0; // Simulated recovery cost across all episodes
  double total_scheduling_time_ms = 0; // Wall-clock scheduling decision time
  int scheduling_decisions = 0;        // Number of scheduling decisions made

  // R11 FIX: real per-episode convergence tracking (was a hardcoded 5.0 in
  // exp9_main.cpp). Mirrors PLOSHA-RMFR's des_engine.cpp check: first
  // episode at/after the burst (episode >= 12) whose own task distribution
  // (not the cumulative one) has workload imbalance I_W < 0.1.
  std::vector<int> tasks_before_episode(num_fog_nodes_, 0);
  double convergence_epoch = -1.0;

  for (int episode = 0; episode < num_episodes_; episode++) {
    for (int f = 0; f < num_fog_nodes_; f++) {
      tasks_before_episode[f] = fog_nodes_[f].tasks_assigned;
    }

    // Simulate fog node failures
    std::vector<bool> failed_nodes;
    SimulateFailures(failed_nodes);

    // Recover failed nodes
    for (int i = 0; i < num_fog_nodes_; i++) {
      if (failed_nodes[i]) {
        RecoverFromFailure(i, recovery_count, total_recovery_latency);
      }
    }

    int scheduling_step = 0;

    int tasks_to_process = tasks_.size();
    if (episode >= 12) {
      tasks_to_process = static_cast<int>(tasks_.size() * 1.5); // 50% burst
    }

    for (int t_idx = 0; t_idx < tasks_to_process; ++t_idx) {
      Task task = tasks_[t_idx % tasks_.size()];
      task.rejected = false;
      task.sla_violated = false;

      // Select fog node (hotspot assignment: 25% to Node 0, rest evenly)
      int node_idx;
      if (t_idx < tasks_to_process * 0.25) {
          node_idx = 0;
      } else {
          node_idx = 1 + (t_idx % std::max(1, num_fog_nodes_ - 1));
      }
      int attempts = 0;
      while (failed_nodes[node_idx] && attempts < num_fog_nodes_) {
        node_idx = (node_idx + 1) % num_fog_nodes_;
        attempts++;
      }
      if (failed_nodes[node_idx]) {
        task.rejected = true;
        total_rejected++;
        continue;
      }

      FogNode &node = fog_nodes_[node_idx];

      // Get state
      std::vector<double> state = GetState(node, task);

      // Time the DQN scheduling decision
      auto sched_start = std::chrono::high_resolution_clock::now();

      // Select VM action (epsilon-greedy)
      int vm_idx = SelectAction(node, state);

      auto sched_end = std::chrono::high_resolution_clock::now();
      total_scheduling_time_ms +=
          std::chrono::duration<double, std::milli>(sched_end - sched_start)
              .count();
      scheduling_decisions++;

      VirtualMachine &vm = node.vms[vm_idx];

      // Check if VM has capacity
      if (vm.task_queue.size() >= 50) { // Max queue depth
        task.rejected = true;
        total_rejected++;
        node.tasks_rejected++;

        // Store experience with rejection penalty
        std::vector<double> next_state = GetState(node, task);
        Experience exp = {state, vm_idx, -10.0, next_state};
        node.replay_buffer.push_back(exp);
        if ((int)node.replay_buffer.size() > node.max_replay_size) {
          node.replay_buffer.pop_front();
        }
        continue;
      }

      // Schedule task on VM
      task.start_time = std::max(task.arrival_time, vm.available_time);

      // Real crypto: AES-GCM encrypt the sensor reading data
      // This models the encrypted task processing that each VM performs
      uint32_t sensor_value =
          static_cast<uint32_t>(task.cpu_requirement * 1000);
      uint8_t plaintext[sizeof(uint32_t)];
      std::memcpy(plaintext, &sensor_value, sizeof(uint32_t));
      uint8_t ciphertext[sizeof(uint32_t) + 16]; // room for padding

      auto crypto_start = std::chrono::high_resolution_clock::now();
      aesEncryptInPlace(plaintext, sizeof(uint32_t), ciphertext);
      auto crypto_end = std::chrono::high_resolution_clock::now();
      double crypto_ms =
          std::chrono::duration<double, std::milli>(crypto_end - crypto_start)
              .count();

      // Execution time: real crypto cost plus heterogeneous task CPU requirement, scaled by VM capacity
      // Slower VMs (lower MIPS) take proportionally longer
      double capacity_factor = 1000.0 / vm.cpu_capacity;
      if (episode >= 21 && node_idx < std::max(1, static_cast<int>(num_fog_nodes_ * 0.20))) {
        capacity_factor *= 3.0; // Artificial degradation increases processing time
      }
      double actual_exec = (task.cpu_requirement + crypto_ms) * capacity_factor;
      task.completion_time = task.start_time + actual_exec;
      node.total_makespan += actual_exec;

      // Update VM
      vm.available_time = task.completion_time;
      vm.task_queue.push(task.id);
      vm.tasks_completed++;

      // Energy = power * execution_time (in seconds)
      double energy = vm.power_watts * (actual_exec / 1000.0);
      vm.total_energy += energy;

      // Check SLA
      if (task.completion_time > task.arrival_time + task.deadline) {
        task.sla_violated = true;
        total_sla_violations++;
        node.sla_violations++;
      }

      // Track makespan and per-task response time
      sim_min_start = std::min(sim_min_start, task.start_time);
      sim_max_completion = std::max(sim_max_completion, task.completion_time);
      total_task_latency += (task.completion_time - task.arrival_time);
      completed_task_count++;

      node.tasks_assigned++;

      // Compute reward
      double reward = ComputeReward(task, vm);

      // Next state
      std::vector<double> next_state = GetState(node, task);

      // Store experience
      Experience exp = {state, vm_idx, reward, next_state};
      node.replay_buffer.push_back(exp);
      if ((int)node.replay_buffer.size() > node.max_replay_size) {
        node.replay_buffer.pop_front();
      }

      // Train from replay periodically
      scheduling_step++;
      if (scheduling_step % batch_size_ == 0) {
        TrainFromReplay(node);
      }

      // Sample queue utilization
      for (const auto &v : node.vms) {
        total_queue_len += v.task_queue.size();
        queue_samples++;
      }

      // Federated averaging periodically
      int fed_interval =
          std::max(1, federated_period_ * total_tasks / num_episodes_);
      if (scheduling_step % fed_interval == 0) {
        FederatedAggregate();
        federated_sync_count++;
      }

      // Pop completed tasks from queues
      for (auto &v : node.vms) {
        while (!v.task_queue.empty()) {
          v.task_queue.pop();
        }
      }
    }

    // Decay epsilon
    current_epsilon = std::max(epsilon_min_, current_epsilon * epsilon_decay_);
    // BUGFIX: SelectAction reads the member epsilon_; decaying only the local
    // left it pinned at 1.0 (100% explore, learned policy never used).
    epsilon_ = current_epsilon;

    // R11 FIX: this episode's own imbalance, from the delta against the
    // pre-episode snapshot (fog_nodes_[f].tasks_assigned is cumulative
    // across the whole run, so a delta is required to isolate one episode).
    if (episode >= 12 && convergence_epoch < 0.0) {
      double episode_total = 0.0;
      std::vector<double> episode_delta(num_fog_nodes_, 0.0);
      for (int f = 0; f < num_fog_nodes_; f++) {
        episode_delta[f] = static_cast<double>(fog_nodes_[f].tasks_assigned -
                                               tasks_before_episode[f]);
        episode_total += episode_delta[f];
      }
      if (episode_total > 0.0) {
        double w_bar = 0.0;
        for (int f = 0; f < num_fog_nodes_; f++) {
          w_bar += episode_delta[f] / episode_total;
        }
        w_bar /= num_fog_nodes_;
        double var_sum = 0.0;
        for (int f = 0; f < num_fog_nodes_; f++) {
          double w_i = episode_delta[f] / episode_total;
          double diff = w_i - w_bar;
          var_sum += diff * diff;
        }
        double episode_imbalance = std::sqrt(var_sum / num_fog_nodes_) / (w_bar + 1e-9);
        // BUGFIX: flat 0.1 (same as PLOSHA); 0.1*num_fog was trivially true
        if (episode_imbalance < 0.1) {
          convergence_epoch = episode - 12;
        }
      }
    }
  }
  if (convergence_epoch < 0.0) {
    convergence_epoch = num_episodes_ - 12;
  }
  metrics.convergence_time_epochs = convergence_epoch;

  // Compute metrics (average over episodes)
  double ep = static_cast<double>(num_episodes_);

  // Simulated aggregation latency: average per-task response time
  // (completion_time - arrival_time) across all scheduled tasks
  double avg_response = (completed_task_count > 0)
                            ? total_task_latency / completed_task_count
                            : 0;

  // Federated synchronization overhead: each sync round transfers DNN weights
  // across all nodes. Cost proportional to number of nodes × neural network
  // parameters that must be transmitted and aggregated.
  double avg_params = (fog_nodes_.empty()) ? 0.0
      : static_cast<double>(fog_nodes_[0].dqn.numParameters());
  // Each sync round: all nodes send their weight matrices → aggregate → redistribute
  // Overhead per round scales with (num_nodes * num_parameters *
  // parameter_transfer_cost)
  double sync_overhead_per_round =
      num_fog_nodes_ * avg_params * 0.0001; // ms
  double total_sync_overhead = federated_sync_count * sync_overhead_per_round;

  metrics.aggregation_latency_ms = avg_response + (total_sync_overhead / ep);

  double makespan = sim_max_completion - sim_min_start;
  metrics.makespan_ms = (makespan > 0) ? makespan / ep : 0;

  int tasks_completed = (total_tasks * num_episodes_) - total_rejected;
  metrics.throughput = (makespan > 0) ? tasks_completed / (makespan / ep) : 0;

  // Total energy across all fog nodes
  for (const auto &node : fog_nodes_) {
    for (const auto &vm : node.vms) {
      total_energy += vm.total_energy;
    }
  }
  metrics.energy_total = total_energy / ep;

  metrics.sla_violation_rate = static_cast<double>(total_sla_violations) /
                               static_cast<double>(total_tasks * num_episodes_);
  metrics.task_rejection_rate =
      static_cast<double>(total_rejected) /
      static_cast<double>(total_tasks * num_episodes_);
  metrics.avg_queue_utilization = (queue_samples > 0)
                                      ? total_queue_len / queue_samples /
                                            50.0 // Normalize to max queue depth
                                      : 0;
  metrics.recovery_count = recovery_count;
  metrics.recovery_latency_ms =
      (recovery_count > 0) ? total_recovery_latency /
                                 recovery_count // Average per-recovery latency
                           : 0;

  // Scheduling latency: average wall-clock time per scheduling decision
  metrics.scheduling_latency_ms =
      (scheduling_decisions > 0)
          ? total_scheduling_time_ms / scheduling_decisions
          : 0;

  // Workload imbalance: Capacity-normalized
  {
    double total_w = 0.0, total_cap = 0.0;
    for (int f = 0; f < num_fog_nodes_; f++) {
      // Count assigned + rejected. VM queues are capped, so under the 25%
      // hotspot the excess load aimed at a node is REJECTED rather than
      // queued. Counting only accepted tasks would hide that burden and make
      // the scheme look artificially balanced; the offered load is what the
      // imbalance metric must reflect, matching the other schemes.
      total_w += static_cast<double>(fog_nodes_[f].tasks_assigned + fog_nodes_[f].tasks_rejected);
      double node_cap = 0;
      for (const auto& vm : fog_nodes_[f].vms) {
          node_cap += vm.cpu_capacity;
      }
      total_cap += std::max(1.0, node_cap);
    }
    double w_bar = 1.0;
    double var_sum = 0.0;
    for (int f = 0; f < num_fog_nodes_; f++) {
      double node_load = static_cast<double>(fog_nodes_[f].tasks_assigned + fog_nodes_[f].tasks_rejected);
      double load_share = (total_w > 0) ? (node_load / total_w) : 0.0;
      double node_cap = 0;
      for (const auto& vm : fog_nodes_[f].vms) {
          node_cap += vm.cpu_capacity;
      }
      double cap_share = (total_cap > 0) ? (std::max(1.0, node_cap) / total_cap) : (1.0 / num_fog_nodes_);
      double w_i = (cap_share > 0) ? (load_share / cap_share) : 0.0;
      double diff = w_i - w_bar;
      var_sum += diff * diff;
    }
    metrics.workload_imbalance = std::sqrt(var_sum / num_fog_nodes_) / (w_bar + 1e-9);
  }

  return metrics;
}
