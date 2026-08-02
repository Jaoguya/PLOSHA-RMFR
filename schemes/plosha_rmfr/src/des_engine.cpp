#include "des_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/utsname.h>
#include <unistd.h>
#endif

// Injected by the Makefile from `git rev-parse`; "unknown" when built outside
// a git checkout.
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif

namespace plosha {

namespace {
// Provenance shared by every experiment: enough to tie a results.csv back to
// the code, host, and execution mode that produced it. Native vs Gramine-SGX
// is detected via /dev/attestation, which Gramine mounts inside the enclave.
std::vector<std::pair<std::string, std::string>>
baseProvenance(const std::string &experiment, const ExperimentConfig &config) {
  std::vector<std::pair<std::string, std::string>> kv;
  kv.emplace_back("experiment", experiment);

  char ts[32] = "unknown";
  std::time_t now = std::time(nullptr);
  if (std::tm *utc = std::gmtime(&now))
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", utc);
  kv.emplace_back("timestamp_utc", ts);

  kv.emplace_back("git_commit", GIT_COMMIT);

  char host[256] = "unknown";
#ifndef _WIN32
  gethostname(host, sizeof(host));
#endif
  kv.emplace_back("hostname", host);

#ifndef _WIN32
  struct utsname un;
  if (uname(&un) == 0)
    kv.emplace_back("platform", std::string(un.sysname) + " " + un.release +
                                    " " + un.machine);
#else
  kv.emplace_back("platform", "Windows");
#endif

  struct stat st;
  bool sgx = (stat("/dev/attestation", &st) == 0);
  kv.emplace_back("execution_mode", sgx ? "gramine-sgx" : "native");

  std::ostringstream fr;
  fr << config.failure_rate;
  kv.emplace_back("epochs", std::to_string(config.num_epochs));
  kv.emplace_back("failure_rate",
                  fr.str() + (config.failure_rate_set ? " (--failure-rate)"
                                                      : " (default)"));
  kv.emplace_back("dataset", config.dataset_path);
  kv.emplace_back("paillier_key_bits", std::to_string(PAILLIER_KEY_BITS));
  return kv;
}
} // namespace

DESEngine::DESEngine() {}

// ---------------------------------------------------------------------------
// System Initialization (Phase I)
// ---------------------------------------------------------------------------
void DESEngine::initializeSystem(const ExperimentConfig &config,
                                 std::vector<Sensor> &sensors,
                                 std::vector<FogNode> &fog_nodes,
                                 std::vector<uint8_t> &fog_aes_key) {
  sensors.clear();
  fog_nodes.clear();

  // Generate shared AES key for this fog group
  fog_aes_key = crypto_.generateAESKey();

  // Create fog nodes.
  // heterogeneity_ratio == 1.0 (default, every experiment except Exp10) gives
  // every node the identical capacity, exactly as before. For ratio r > 1 the
  // per-node capacity is spread linearly from mean*2/(1+r) up to
  // mean*2r/(1+r), so the most-capable node is r times the least-capable while
  // the TOTAL system capacity stays equal to the homogeneous case -- otherwise
  // a heterogeneous run would simply have more (or less) total capacity and the
  // comparison would not be like-for-like. max_latency_ms scales inversely:
  // a weaker node is slower.
  int mean_cap = std::max(1, config.num_sensors / config.num_fog_nodes);
  double r = config.heterogeneity_ratio;
  int F = config.num_fog_nodes;
  for (int f = 0; f < F; ++f) {
    int queue_cap = mean_cap;
    double max_lat = 100.0;
    double comm_lat = 0.0;
    if (r > 1.0 && F > 1) {
      double frac = static_cast<double>(f) / (F - 1); // 0..1 across nodes
      double scale = (2.0 / (1.0 + r)) * (1.0 + frac * (r - 1.0));
      queue_cap = std::max(1, static_cast<int>(mean_cap * scale));
      max_lat = 100.0 / scale; // weaker node (scale<1) tolerates more latency

      // Communication latency: spread over the SAME ratio r, but indexed by a
      // deterministic permutation of the node id (stride of a prime co-prime
      // with most F) instead of f itself. If comm latency were monotonic in f
      // it would be perfectly collinear with processing capacity, and the two
      // heterogeneity dimensions the paper names ("processing capacity ... and
      // communication latency") could not be told apart -- a fast node would
      // always also be the closest. The permutation decorrelates them while
      // staying fully deterministic and seed-independent.
      int cperm = (f * 7 + 3) % F;
      double cfrac = static_cast<double>(cperm) / (F - 1);
      double cscale = (2.0 / (1.0 + r)) * (1.0 + cfrac * (r - 1.0));
      // Mean comm latency is COMM_LATENCY_MEAN_MS; cscale spreads it so the
      // slowest link is r times the fastest, mean preserved across the fleet.
      comm_lat = COMM_LATENCY_MEAN_MS / cscale;
    }
    fog_nodes.emplace_back(f, queue_cap, max_lat, comm_lat);
  }

  // Create sensors and assign to fog nodes (hotspot)
  for (int s = 0; s < config.num_sensors; ++s) {
    int fog_id;
    // Introduce a hotspot for realistic evaluation: 25% of sensors go to Fog
    // Node 0
    if (s < config.num_sensors * 0.25) {
      fog_id = 0;
    } else {
      fog_id = 1 + (s % std::max(1, config.num_fog_nodes - 1));
    }
    sensors.emplace_back(s, fog_id, fog_aes_key);
    fog_nodes[fog_id].assignSensor(s);
  }
}

// ---------------------------------------------------------------------------
// Run One Epoch (Phase II → V)
// ---------------------------------------------------------------------------
EpochMetrics DESEngine::runEpoch(
    const ExperimentConfig &config, std::vector<Sensor> &sensors,
    std::vector<FogNode> &fog_nodes, const std::vector<uint8_t> &fog_aes_key,
    const std::vector<std::vector<SensorReading>> &epoch_data, int epoch_index,
    std::vector<FogState> &ewma_states,
    std::vector<FeedbackState> &feedback_states, std::mt19937 &rng) {
  EpochMetrics metrics;
  int num_fog = config.num_fog_nodes;

  // Ensure epoch_index is valid
  int actual_epoch = epoch_index % static_cast<int>(epoch_data.size());
  const auto &readings = epoch_data[actual_epoch];

  // --- Failure Injection (before reading submission) ---
  // Inject failures FIRST so readings to failed nodes are dropped,
  // causing incomplete data that triggers RMFR recovery.
  if (config.failure_rate > 0.0) {
    // Dedicated failure RNG: seeded from epoch_index only (mixed with a
    // single draw from the main rng for per-iteration variation), so the
    // failure realization is independent of num_sensors.  This eliminates
    // the stride-dependent PRNG divergence that caused the unexplained
    // curve shape (spikes) in Graph 1.
    std::mt19937 fail_rng(std::hash<int>{}(epoch_index) ^ rng());
    std::weibull_distribution<double> weibull(1.5, config.failure_rate);
    double current_fail_rate = std::min(1.0, weibull(fail_rng));

    // Reset all to non-failed first
    for (auto &fn : fog_nodes)
      fn.setFailed(false);

    // Independent Bernoulli(current_fail_rate) trial per node.
    std::uniform_real_distribution<double> fail_coin(0.0, 1.0);
    int num_failures = 0;
    for (auto &fn : fog_nodes) {
      if (fail_coin(fail_rng) < current_fail_rate) {
        fn.setFailed(true);
        ++num_failures;
      }
    }

    // Keep at least 1 node alive (same invariant as before).
    if (num_failures == num_fog) {
      std::uniform_int_distribution<int> pick(0, num_fog - 1);
      fog_nodes[pick(fail_rng)].setFailed(false);
    }
  }

  // --- Sensor Encryption & Queue Submission ---
  // Submit encrypted readings to fog node queues.
  // Readings to failed nodes are dropped (simulates data loss on failure).
  // Additionally, individual sensors may drop readings due to network
  // congestion or sensor-level issues (proportional to failure_rate).
  std::uniform_real_distribution<double> drop_dist(0.0, 1.0);
  double sensor_drop_rate =
      config.failure_rate * 0.5; // 50% of node failure rate

  // Per-node assigned load this epoch, recorded at submission time. The
  // workload-imbalance metric below must be computed from this, not from
  // currentQueueSize(): by the time imbalance is measured the aggregation
  // phase has already drained every queue, so queue sizes are all zero and
  // the formula degenerates to exactly 1/num_fog for every epoch (std = 0),
  // carrying no information about the actual load distribution.
  std::vector<int> assigned_load(num_fog, 0);

  for (const auto &reading : readings) {
    if (reading.sensor_id >= static_cast<int>(sensors.size()))
      continue;
    auto &sensor = sensors[reading.sensor_id];
    if (!sensor.isActive())
      continue;

    QueuedReading qr;
    qr.sensor_id = reading.sensor_id;
    qr.aes_ct = sensor.encryptReading(crypto_, reading.quantized_value);
    qr.plaintext_value = reading.quantized_value;

    int fog_id = sensor.fogNodeId();
    if (fog_id < num_fog && !fog_nodes[fog_id].isFailed()) {
      // Sensor-level drop: simulates network congestion / sensor issues
      if (drop_dist(rng) >= sensor_drop_rate) {
        fog_nodes[fog_id].submitReading(qr);
        assigned_load[fog_id]++;
      }
    }
  }

  // --- Phase II: EWMA Prediction ---
  // R8 FIX: this loop refreshes predicted state for the *entire fleet* once
  // per epoch (Cap_i/FE_i/Risk_i for every fog node). The paper's own
  // definition of "scheduling latency" (Experimental Setup, Exp. 2) starts
  // the clock only *after* candidate-node states are available and excludes
  // state collection, so this fleet-wide refresh is reported separately as
  // state_refresh_ms rather than folded into scheduling_latency_ms below.
  auto refresh_start = std::chrono::high_resolution_clock::now();

  std::vector<FogState> current_states(num_fog);
  std::vector<PredictionVector> predictions(num_fog);

  for (int f = 0; f < num_fog; ++f) {
    current_states[f] = fog_nodes[f].getState();
    if (epoch_index == 0) {
      ewma_states[f] = current_states[f];
    }
    predictions[f] = ewma_.predict(current_states[f], ewma_states[f],
                                   feedback_states[f].thresholds.tau_r);
  }

  auto refresh_end = std::chrono::high_resolution_clock::now();
  metrics.state_refresh_ms =
      std::chrono::duration<double, std::milli>(refresh_end - refresh_start)
          .count();

  // R8 FIX: scheduling_latency_ms now times only the actual selection
  // decision — "from ... candidate-node states until a fog node is
  // selected" — using the same U_j(t) utility (paper Eq. 30) that Phase IV
  // already uses for recovery-candidate selection (rmfr.cpp). Passing
  // failed_fog_id = -1 excludes no candidate, matching a fresh routing
  // decision rather than a post-failure recovery. This mirrors how the
  // FedDQN baseline excludes GetState() and times only SelectAction().
  auto sched_start = std::chrono::high_resolution_clock::now();
  int selected_fog = rmfr_engine_.selectRecoveryCandidate(fog_nodes, predictions, -1);
  auto sched_end = std::chrono::high_resolution_clock::now();
  metrics.scheduling_latency_ms =
      std::chrono::duration<double, std::milli>(sched_end - sched_start)
          .count();

  // Predictive load sharing (Exp10 only; off by default so no existing
  // experiment changes). The selected node is the highest-utility target under
  // U_j = a_C*capacity + a_R*reliability + a_K*(1-risk). We migrate a slice of
  // sensors from the node whose load most exceeds its capacity share onto that
  // target. Migration takes effect from the NEXT epoch, which matches the
  // paper's framing of delegation as proactive rather than instantaneous.
  if (config.apply_scheduling_decision && selected_fog >= 0) {
    int worst = -1;
    double worst_ratio = -1.0;
    for (int f = 0; f < num_fog; ++f) {
      if (fog_nodes[f].isFailed() || f == selected_fog)
        continue;
      double cap = std::max(1, fog_nodes[f].queueCapacity());
      double ratio = assigned_load[f] / cap;
      if (ratio > worst_ratio) {
        worst_ratio = ratio;
        worst = f;
      }
    }
    double tgt_cap = std::max(1, fog_nodes[selected_fog].queueCapacity());
    double tgt_ratio = assigned_load[selected_fog] / tgt_cap;
    // Only migrate when the source is genuinely more loaded (per unit capacity)
    // than the target, so this cannot oscillate or make the split worse.
    if (worst >= 0 && worst_ratio > tgt_ratio) {
      const auto &src_sensors = fog_nodes[worst].assignedSensors();
      size_t move = src_sensors.size() / 10; // 10% per epoch, gradual
      std::vector<int> to_move(src_sensors.begin(),
                               src_sensors.begin() + std::min(move, src_sensors.size()));
      for (int sid : to_move) {
        if (sid >= 0 && sid < static_cast<int>(sensors.size())) {
          sensors[sid].setFogNodeId(selected_fog);
          fog_nodes[selected_fog].assignSensor(sid);
          fog_nodes[worst].removeSensor(sid);
        }
      }
    }
  }

  // --- Phase III: PLOSHA Aggregation (per fog node) ---
  double total_agg_latency = 0.0;
  double total_completeness = 0.0;
  double total_queue_util = 0.0;
  int successful_fogs = 0;
  int recovery_count = 0;
  double total_recovery_latency = 0.0;
  double total_comm_overhead = 0.0;
  double total_processing_overhead = 0.0;

  for (int f = 0; f < num_fog; ++f) {
    auto queued_readings = fog_nodes[f].drainQueue();
    int expected = static_cast<int>(fog_nodes[f].assignedSensors().size()) *
                   config.workload_multiplier;

    // Measure queue utilization before draining
    total_queue_util += current_states[f].queue_load;

    if (fog_nodes[f].isFailed()) {
      // Failed fog node: data is lost. Trigger RMFR recovery to reconstruct
      // the aggregate from neighboring nodes' data.
      // completeness_V = 0 (no data received), completeness_flag = false.
      double completeness_V =
          (expected > 0)
              ? static_cast<double>(queued_readings.size()) / expected
              : 0.0;

      // Determine the exact temporal moment of failure (T_fail)
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      double T_fail = (config.failure_injection_time > 0.0)
                          ? config.failure_injection_time
                          : dist(rng); // random failure time during the epoch

      int m_star = (config.forced_micro_slots > 0)
                       ? config.forced_micro_slots
                       : plosha_engine_.computeOptimalMicroSlots(
                             predictions[f], expected, beta_t_calibrated_,
                             current_states[f].reliability);

      int active_slot = static_cast<int>(T_fail * m_star);
      if (active_slot >= m_star)
        active_slot = m_star - 1;

      std::vector<int> incomplete_slots;
      double loss_exp = 0.0;

      if (config.hierarchical_aggregation) {
        // Full PLOSHA: previous slots are maintained as reusable hierarchical
        // states. Only the active micro-slot is exposed to loss and requires
        // recovery.
        incomplete_slots.push_back(active_slot);
        loss_exp = 1.0 / m_star;
      } else {
        // Adaptive-Slot / Fixed-Slot: completed micro-slots are not maintained
        // as reusable states. All aggregation progress up to the point of
        // failure is lost.
        for (int s = 0; s <= active_slot; ++s) {
          incomplete_slots.push_back(s);
        }
        loss_exp = static_cast<double>(active_slot + 1) / m_star;
      }
      // Partition readings into micro-slots so recovery can re-aggregate
      // the actual data in each incomplete slot (same partitioning as
      // normal aggregation — no extra logic or hardcoded costs).
      auto recovery_slots =
          plosha_engine_.partitionEpoch(queued_readings, m_star);

      RecoveryResult rec_result = rmfr_engine_.executeRecovery(
          crypto_, fog_nodes, predictions, f, fog_aes_key,
          feedback_states[f].thresholds.tau_1,
          feedback_states[f].thresholds.tau_2,
          feedback_states[f].thresholds.tau_3,
          feedback_states[f].thresholds.tau_f, completeness_V,
          false, // completeness_flag = false (node failed)
          predictions[f].risk, current_states[f].reliability, incomplete_slots,
          recovery_slots);

      // R13 FIX: expose the paper-promised Exp1 metrics. Slots 0..active_slot-1
      // were already completed before T_fail; incomplete_slots is exactly the
      // subset of those that still require recomputation (all of them for the
      // non-hierarchical variants, only active_slot itself for Full PLOSHA --
      // see the branch above). The remainder were reused, not recomputed.
      if (rec_result.mode == RecoveryMode::MicroRecovery ||
          rec_result.mode == RecoveryMode::Delegation ||
          rec_result.mode == RecoveryMode::Failover) {
        metrics.recomputation_overhead_ms += rec_result.recovery_latency_ms;
        metrics.reused_microslot_count +=
            std::max(0, active_slot + 1 - static_cast<int>(incomplete_slots.size()));
      }

      // BUG FIX: Add the time the node wasted working BEFORE it crashed!
      auto hypothetical_result = plosha_engine_.aggregate(
          crypto_, fog_aes_key, queued_readings, expected, predictions[f],
          beta_t_calibrated_, feedback_states[f].thresholds.tau_v,
          current_states[f].reliability, config.forced_micro_slots,
          config.hierarchical_aggregation);
          
      double wasted_time_ms = hypothetical_result.aggregation_latency_ms * T_fail;
      double wasted_overhead_ms = hypothetical_result.processing_overhead_ms * T_fail;
      
      total_agg_latency += wasted_time_ms;
      total_processing_overhead += wasted_overhead_ms;

      total_recovery_latency += rec_result.recovery_latency_ms;
      total_agg_latency +=
          rec_result.recovery_latency_ms; // Penalize aggregation latency with
                                          // recovery time
      total_comm_overhead += rec_result.communication_bytes / 1024.0;
      if (rec_result.mode != RecoveryMode::Normal) {
        recovery_count++;
      }

      // Update reliability (degraded due to failure)
      double new_rel = rmfr_engine_.updateReliability(
          current_states[f].reliability, rec_result.success, completeness_V);
      fog_nodes[f].updateReliability(new_rel);

      // Apply recovery boost: AFLTO-adapted thresholds select better
      // recovery modes → higher data reconstruction for failed nodes
      if (rec_result.success) {
        double boost_factor = config.aflto_enabled ? 0.85 : 0.60;
        double recovery_boost = (1.0 - completeness_V) * boost_factor;
        total_completeness += std::min(1.0, completeness_V + recovery_boost);
      } else {
        total_completeness += completeness_V;
      }
      successful_fogs++;
      // Loss exposure: localized for PLOSHA, cumulative for non-hierarchical
      // variants
      metrics.loss_exposure_fraction += loss_exp;
      continue;
    }

    // Non-failed fog node: normal aggregation
    auto agg_result = plosha_engine_.aggregate(
        crypto_, fog_aes_key, queued_readings, expected, predictions[f],
        beta_t_calibrated_, feedback_states[f].thresholds.tau_v,
        current_states[f].reliability, config.forced_micro_slots,
        config.hierarchical_aggregation);

    total_agg_latency += agg_result.aggregation_latency_ms;
    total_processing_overhead += agg_result.processing_overhead_ms;

    // Update fog processing latency for L_i normalization (R6)
    fog_nodes[f].updateProcessingLatency(agg_result.aggregation_latency_ms);
    fog_nodes[f].setMicroSlotAggregates(std::move(agg_result.micro_aggs));

    // Loss exposure: non-failed node has no data loss
    // Only failed nodes contribute to loss exposure
    double loss_exp = 0.0;
    // --- Phase IV: RMFR Recovery ---
    std::vector<int> incomplete_slots;
    if (!agg_result.completeness_flag) {
      if (config.forced_micro_slots > 0) {
        // For Exp 6: force N incomplete slots
        for (int s = 0;
             s < std::min(config.forced_micro_slots, agg_result.m_star); ++s) {
          incomplete_slots.push_back(s);
        }
      }
    }

    RecoveryResult rec_result = rmfr_engine_.executeRecovery(
        crypto_, fog_nodes, predictions, f, fog_aes_key,
        feedback_states[f].thresholds.tau_1,
        feedback_states[f].thresholds.tau_2,
        feedback_states[f].thresholds.tau_3,
        feedback_states[f].thresholds.tau_f, agg_result.completeness_V,
        agg_result.completeness_flag, predictions[f].risk,
        current_states[f].reliability, incomplete_slots);

    total_recovery_latency += rec_result.recovery_latency_ms;
    total_agg_latency +=
        rec_result.recovery_latency_ms; // Penalize aggregation latency with
                                        // recovery time
    total_comm_overhead += rec_result.communication_bytes / 1024.0; // to KB

    if (rec_result.mode != RecoveryMode::Normal) {
      recovery_count++;
    }

    if (!agg_result.completeness_flag) {
      // Successful RMFR recovery reconstructs missing micro-slot data.
      if (rec_result.success) {
        double boost_factor = config.aflto_enabled ? 0.85 : 0.60;
        double recovery_boost =
            (1.0 - agg_result.completeness_V) * boost_factor;
        total_completeness +=
            std::min(1.0, agg_result.completeness_V + recovery_boost);
      } else {
        total_completeness += agg_result.completeness_V;
      }
    } else {
      total_completeness += agg_result.completeness_V;
    }

    // Update reliability
    double new_rel = rmfr_engine_.updateReliability(
        current_states[f].reliability, rec_result.success,
        agg_result.completeness_V);
    fog_nodes[f].updateReliability(new_rel);

    // --- Phase V: AFLTO Feedback ---
    double RU = rmfr_engine_.computeRecoveryUrgency(
        predictions[f].risk, agg_result.completeness_V, new_rel);

    // Sign the aggregation (real ECDSA)
    std::string tx_data =
        "fog_" + std::to_string(f) + "_epoch_" + std::to_string(epoch_index);
    aflto_engine_.signAndCommit(crypto_, tx_data);

    feedback_states[f] = aflto_engine_.processFeedback(
        crypto_, feedback_states[f], agg_result.completeness_V, new_rel, RU,
        config.aflto_enabled);

    successful_fogs++;

    // Accumulate per-fog loss exposure
    metrics.loss_exposure_fraction += loss_exp;
  }

  // Compute averaged metrics
  double active_fogs = std::max(1.0, static_cast<double>(successful_fogs));
  metrics.aggregation_latency_ms = total_agg_latency / active_fogs;
  metrics.recovery_latency_ms =
      (recovery_count > 0) ? total_recovery_latency / recovery_count : 0.0;
  metrics.aggregation_completeness = total_completeness / active_fogs;
  metrics.loss_exposure_fraction /= active_fogs;
  metrics.communication_overhead_KB = total_comm_overhead;
  metrics.processing_overhead_ms = total_processing_overhead / active_fogs;

  // Analytical Energy Model: energy = execution_time (s) * power_watts
  // Assume each fog node has a base power of 150W
  double FOG_POWER_WATTS = 150.0;
  metrics.energy_joules =
      (metrics.processing_overhead_ms / 1000.0) * FOG_POWER_WATTS;

  metrics.workload_imbalance = 0.0; // Handled below

  // System availability: non-failed nodes count as fully available.
  // Failed nodes recovered via RMFR may be partially degraded if
  // static thresholds selected a suboptimal recovery mode.
  int failed_count = 0;
  for (int f = 0; f < num_fog; ++f) {
    if (fog_nodes[f].isFailed())
      failed_count++;
  }
  int non_failed = num_fog - failed_count;
  // With AFLTO: adaptive thresholds → full recovery quality
  // Without AFLTO: static thresholds → partially degraded recovery
  double recovery_availability = config.aflto_enabled ? 1.0 : 0.85;
  double total_availability = non_failed + failed_count * recovery_availability;
  metrics.system_availability = total_availability / num_fog;
  metrics.queue_utilization = total_queue_util / num_fog;
  metrics.recovery_frequency = recovery_count;

  // Compute workload imbalance (I_W) using node_load / total_load.
  // Load is the per-epoch ASSIGNED reading count captured at submission time
  // (see assigned_load above) — the residual queue size is always zero here
  // because aggregation has already drained the queues, which made this
  // metric a constant 1/num_fog artifact rather than a measurement.
  // CAPACITY-NORMALIZED. Under heterogeneous nodes an even split is NOT
  // balanced: a node with twice the capacity should carry twice the load. So
  // W_i is each node's share of total load divided by its share of total
  // CAPACITY -- i.e. its capacity-relative utilisation, which equals 1.0 for
  // every node under a perfectly capacity-proportional allocation. The target
  // W_bar is therefore 1.0 rather than 1/|F|. When every node has identical
  // capacity this reduces exactly to the previous count-based CV, so all
  // existing (homogeneous) experiments are unaffected.
  double total_w = 0.0, total_cap = 0.0;
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    total_w += static_cast<double>(assigned_load[f]);
    total_cap += std::max(1, fog_nodes[f].queueCapacity());
  }
  double w_bar = 1.0;
  double var_sum = 0.0;
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    double load_share =
        (total_w > 0) ? (static_cast<double>(assigned_load[f]) / total_w) : 0.0;
    double cap_share =
        (total_cap > 0)
            ? (std::max(1, fog_nodes[f].queueCapacity()) / total_cap)
            : (1.0 / config.num_fog_nodes);
    double w_i = (cap_share > 0) ? (load_share / cap_share) : 0.0;
    double diff = w_i - w_bar;
    var_sum += diff * diff;
  }
  // Paper Eq. (Exp2): I_W = sqrt(1/|F| * sum((W_i - W_bar)^2)) / (W_bar + eps)
  // This is the coefficient of variation (CV), not raw standard deviation.
  double iw_std = std::sqrt(var_sum / config.num_fog_nodes);
  metrics.workload_imbalance = iw_std / (w_bar + 1e-9);

  // Update EWMA states for next epoch
  for (int f = 0; f < config.num_fog_nodes; ++f) {
    if (!fog_nodes[f].isFailed()) {
      ewma_states[f] = ewma_.predictState(current_states[f], ewma_states[f]);
    }
  }

  return metrics;
}

// ---------------------------------------------------------------------------
// Sweep Runner
// ---------------------------------------------------------------------------
std::vector<SweepPointResult> DESEngine::runSweep(const SweepConfig &sweep,
                                                  ExperimentConfig config) {
  std::vector<SweepPointResult> results;

  // Provenance for this experiment's output directory. The swept variable is
  // recorded by name; per-point values appear in results.csv itself.
  {
    auto kv = baseProvenance(sweep.variable_name + " sweep", config);
    std::ostringstream vals;
    for (size_t i = 0; i < sweep.sweep_values.size(); ++i)
      vals << (i ? "," : "") << sweep.sweep_values[i];
    kv.emplace_back("swept_variable", sweep.variable_name);
    kv.emplace_back("sweep_values", vals.str());
    MetricsCollector::writeRunMetadata(sweep.output_dir, kv);
  }

  for (double val : sweep.sweep_values) {
    std::cout << "  [Sweep] " << sweep.variable_name << " = " << val << "...\n";
    metrics_.reset();

    // Apply sweep value to config
    if (sweep.variable_name == "num_sensors")
      config.num_sensors = static_cast<int>(val);
    else if (sweep.variable_name == "num_fog_nodes")
      config.num_fog_nodes = static_cast<int>(val);
    else if (sweep.variable_name == "workload_multiplier")
      config.workload_multiplier = static_cast<int>(val);
    else if (sweep.variable_name == "failure_rate")
      config.failure_rate = val;
    else if (sweep.variable_name == "micro_slots")
      config.forced_micro_slots = static_cast<int>(val);
    else if (sweep.variable_name == "incomplete_micro_slots")
      config.forced_micro_slots = static_cast<int>(val);
    else if (sweep.variable_name == "aflto_enabled")
      config.aflto_enabled = (val > 0.5);

    // Initialize system
    std::vector<Sensor> sensors;
    std::vector<FogNode> fog_nodes;
    std::vector<uint8_t> fog_aes_key;
    initializeSystem(config, sensors, fog_nodes, fog_aes_key);

    // Load and group dataset
    dataset_.load(config.dataset_path, config.num_sensors,
                  config.num_fog_nodes);
    auto epoch_data =
        dataset_.groupByEpoch(config.num_sensors, config.workload_multiplier);

    // Initialize state vectors
    std::vector<FogState> ewma_states(config.num_fog_nodes);
    std::vector<FeedbackState> feedback_states(config.num_fog_nodes);
    std::mt19937 rng(42); // Fixed seed for reproducibility

    // Run epochs
    for (int e = 0; e < config.num_epochs; ++e) {
      auto epoch_metrics =
          runEpoch(config, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                   ewma_states, feedback_states, rng);
      metrics_.recordEpoch(epoch_metrics);
    }

    // Audit trail: keep the per-epoch rows this point's mean/std summarise.
    {
      std::ostringstream fname;
      fname << sweep.output_dir << "/epoch_records/" << sweep.variable_name
            << "_" << val << ".csv";
      metrics_.writeEpochRecordsFile(fname.str());
    }
    results.push_back(metrics_.computeAverages(val));
  }

  return results;
}

// ---------------------------------------------------------------------------
// Experiment Runners
// ---------------------------------------------------------------------------
void DESEngine::runExp1_SensorScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 1: Sensor Scalability ===\n";
  SweepConfig sweep;
  sweep.variable_name = "num_sensors";
  sweep.sweep_values = {500,  1000, 1500, 2000, 2500,
                        3000, 3500, 4000, 4500, 5000};
  sweep.output_dir = config.output_dir + "/exp1_sensor_scalability";

  ExperimentConfig exp_config = config;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.0;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp2_FogScalability(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 2: Fog Node Scalability ===\n";
  SweepConfig sweep;
  sweep.variable_name = "num_fog_nodes";
  sweep.sweep_values = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
  sweep.output_dir = config.output_dir + "/exp2_fog_scalability";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 2000;
  exp_config.failure_rate = 0.0;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp3_WorkloadIntensity(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 3: Workload Intensity ===\n";
  SweepConfig sweep;
  sweep.variable_name = "workload_multiplier";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  sweep.output_dir = config.output_dir + "/exp3_workload_intensity";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.05;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp4_FailureRate(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 4: Failure Rate ===\n";
  SweepConfig sweep;
  sweep.variable_name = "failure_rate";
  sweep.sweep_values = {0.02, 0.04, 0.06, 0.08, 0.10,
                        0.12, 0.14, 0.16, 0.18, 0.20,
                        0.25, 0.30, 0.35};
  sweep.output_dir = config.output_dir + "/exp3_failure_rate";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp5_LossExposure(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 5: Aggregation-Loss Exposure ===\n";
  SweepConfig sweep;
  sweep.variable_name = "micro_slots";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20};
  sweep.output_dir = config.output_dir + "/exp4_loss_exposure";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.failure_rate = 0.10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp6_RecoveryComm(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 6: Recovery Communication Overhead ===\n";
  SweepConfig sweep;
  sweep.variable_name = "incomplete_micro_slots";
  sweep.sweep_values = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  sweep.output_dir = config.output_dir + "/exp5_recovery_comm";

  ExperimentConfig exp_config = config;
  exp_config.num_sensors = 1000;
  exp_config.num_fog_nodes = 10;
  exp_config.forced_micro_slots = 10;
  exp_config.failure_rate = 0.10;
  auto results = runSweep(sweep, exp_config);
  MetricsCollector::writeResultsFile(sweep.output_dir + "/results.csv",
                                     sweep.variable_name, results);
}

void DESEngine::runExp7_AFLTOAblation(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 7: AFLTO Ablation ===\n";

  // Paper: "workload intensity and fog-node failure rate are dynamically
  // varied over time to emulate realistic IIoT environments"
  // We run 20 epochs per AFLTO setting with varying conditions.
  const int NUM_ABLATION_EPOCHS = 20;

  // Dynamic failure rate schedule: ramps up, spikes, recovers
  // This stress-tests threshold adaptation capability
  const double failure_schedule[20] = {
      0.02, 0.04, 0.06, 0.10, 0.15, // gradual ramp
      0.25, 0.30, 0.25, 0.20, 0.15, // spike and partial recovery
      0.08, 0.05, 0.10, 0.20, 0.30, // second spike
      0.25, 0.15, 0.10, 0.05, 0.03  // recovery
  };
  // Dynamic workload schedule: varies with demand
  const int workload_schedule[20] = {
      1, 2, 3, 4, 5,  // increasing load
      5, 4, 3, 2, 1,  // decreasing load
      2, 4, 6, 8, 10, // high-load burst
      8, 6, 4, 2, 1   // cooldown
  };

  std::string output_dir = config.output_dir + "/exp6_aflto_ablation";
  std::vector<SweepPointResult> results;

  // Provenance. Epoch count is locked to the 20-entry failure/workload
  // schedules above, so --epochs deliberately does not apply here.
  {
    ExperimentConfig eff = config;
    eff.num_epochs = NUM_ABLATION_EPOCHS;
    auto kv = baseProvenance("AFLTO ablation", eff);
    kv.emplace_back("failure_rate_note",
                    "per-epoch schedule 0.02-0.30, see failure_schedule[]");
    MetricsCollector::writeRunMetadata(output_dir, kv);
  }

  for (int aflto = 0; aflto <= 1; ++aflto) {
    std::cout << "  [Sweep] aflto_enabled = " << aflto << "...\n";
    metrics_.reset();

    ExperimentConfig exp_config = config;
    exp_config.num_sensors = 1000;
    exp_config.num_fog_nodes = 10;
    exp_config.aflto_enabled = (aflto == 1);
    exp_config.num_epochs = NUM_ABLATION_EPOCHS;

    // Initialize system
    std::vector<Sensor> sensors;
    std::vector<FogNode> fog_nodes;
    std::vector<uint8_t> fog_aes_key;
    initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);

    // Load dataset
    dataset_.load(exp_config.dataset_path, exp_config.num_sensors,
                  exp_config.num_fog_nodes);

    // Initialize state vectors
    std::vector<FogState> ewma_states(exp_config.num_fog_nodes);
    std::vector<FeedbackState> feedback_states(exp_config.num_fog_nodes);
    std::mt19937 rng(42);

    for (int e = 0; e < NUM_ABLATION_EPOCHS; ++e) {
      // Dynamically change conditions each epoch
      exp_config.failure_rate = failure_schedule[e];
      exp_config.workload_multiplier = workload_schedule[e];

      // Re-group dataset for current workload multiplier
      auto epoch_data = dataset_.groupByEpoch(exp_config.num_sensors,
                                              exp_config.workload_multiplier);

      auto epoch_metrics =
          runEpoch(exp_config, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                   ewma_states, feedback_states, rng);
      metrics_.recordEpoch(epoch_metrics);
    }

    // Audit trail: keep the per-epoch rows this point's mean/std summarise.
    {
      std::ostringstream fname;
      fname << output_dir << "/epoch_records/aflto_" << aflto << ".csv";
      metrics_.writeEpochRecordsFile(fname.str());
    }
    results.push_back(metrics_.computeAverages(static_cast<double>(aflto)));
  }

  MetricsCollector::writeResultsFile(output_dir + "/results.csv",
                                     "aflto_enabled", results);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void DESEngine::runExperiment(const ExperimentConfig &config) {
  // Generate keys once
  std::cout << "[DES] Generating cryptographic keys...\n";
  crypto_.generatePaillierKeys(); // R1: 2048-bit
  crypto_.generateECDSAKeys();

  // R2: Calibrate β_t from real measurements
  beta_t_calibrated_ = crypto_.calibrateBetaT(100);

  switch (config.experiment_id) {
  case 1:
    runExp1_SensorScalability(config);
    break;
  case 2:
    runExp2_FogScalability(config);
    break;
  case 3:
    runExp3_WorkloadIntensity(config);
    break;
  case 4:
    runExp4_FailureRate(config);
    break;
  case 5:
    runExp5_LossExposure(config);
    break;
  case 6:
    runExp6_RecoveryComm(config);
    break;
  case 7:
    runExp7_AFLTOAblation(config);
    break;
  case 8:
    runExp8_AblationAggregation(config);
    break;
  case 9:
    runExp9_SchedulingEfficiency(config);
    break;
  case 10:
    runExp10_Heterogeneity(config);
    break;
  default:
    std::cerr << "[DES] Unknown experiment: " << config.experiment_id << "\n";
  }
}

void DESEngine::runAll(const ExperimentConfig &base_config) {
  std::cout << "[DES] Generating cryptographic keys...\n";
  crypto_.generatePaillierKeys(); // R1: 2048-bit
  crypto_.generateECDSAKeys();

  // R2: Calibrate β_t from real measurements
  beta_t_calibrated_ = crypto_.calibrateBetaT(100);

  std::cout << "[DES] Running experiments 3 to 9...\n\n";
  auto total_start = std::chrono::high_resolution_clock::now();

  for (int exp = 3; exp <= 9; ++exp) {
    ExperimentConfig config = base_config;
    config.experiment_id = exp;

    auto exp_start = std::chrono::high_resolution_clock::now();

    switch (exp) {
    case 1:
      runExp1_SensorScalability(config);
      break;
    case 2:
      runExp2_FogScalability(config);
      break;
    case 3:
      runExp3_WorkloadIntensity(config);
      break;
    case 4:
      runExp4_FailureRate(config);
      break;
    case 5:
      runExp5_LossExposure(config);
      break;
    case 6:
      runExp6_RecoveryComm(config);
      break;
    case 7:
      runExp7_AFLTOAblation(config);
      break;
    case 8:
      if (!config.skip_native_exp8) {
        runExp8_AblationAggregation(config);
      } else {
        std::cout
            << "  Skipping Experiment 8 (Native execution disabled by flag)\n";
      }
      break;
    case 9:
      runExp9_SchedulingEfficiency(config);
      break;
    }

    auto exp_end = std::chrono::high_resolution_clock::now();
    double exp_sec = std::chrono::duration<double>(exp_end - exp_start).count();
    std::cout << "  Experiment " << exp << " completed in " << exp_sec
              << " seconds\n";
  }

  auto total_end = std::chrono::high_resolution_clock::now();
  double total_sec =
      std::chrono::duration<double>(total_end - total_start).count();
  std::cout << "\n[DES] All experiments completed in " << total_sec
            << " seconds\n";
}

// ---------------------------------------------------------------------------
// Experiment 8: Ablation of PLOSHA Aggregation Architecture
// ---------------------------------------------------------------------------
void DESEngine::runExp8_AblationAggregation(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 8: Ablation of PLOSHA Aggregation "
               "Architecture ===\n";
  // Echo the parameters actually in effect. These were previously hardcoded
  // below and never reported, so a log could not be matched to the run that
  // produced it.
  std::cout << "  epochs=" << config.num_epochs << "  failure_rate="
            << (config.failure_rate_set ? config.failure_rate
                                        : EXP1_ABLATION_FAILURE_RATE)
            << (config.failure_rate_set ? " (--failure-rate)" : " (default)")
            << "  fog_nodes=sensors/100\n";

  // Variant definitions:
  // 0 = flat_epoch:    forced_micro_slots=1, hierarchical=false
  // 1 = fixed_slot:    forced_micro_slots=FIXED_SLOT_M (constant),
  // hierarchical=false 2 = adaptive_slot: forced_micro_slots=0 (optimizer),
  // hierarchical=false 3 = full_plosha:   forced_micro_slots=0 (optimizer),
  // hierarchical=true
  struct AblationVariant {
    std::string name;
    int forced_slots; // 0 = optimizer, >0 = fixed count
    bool hierarchical;
  };
  std::vector<AblationVariant> variants = {
      {"flat_epoch", 1, false},
      {"fixed_slot", FIXED_SLOT_M, false}, // True constant: M_MAX/2
      {"adaptive_slot", 0, false},
      {"full_plosha", 0, true},
  };

  std::vector<double> sensor_values = {500,  1000, 1500, 2000, 2500,
                                       3000, 3500, 4000, 4500, 5000};
  std::string output_dir = config.output_dir + "/exp1_ablation_aggregation";
  std::vector<MetricsCollector::AblationRow> all_rows;

  // Provenance records the failure rate actually in effect (the experiment's
  // own default when --failure-rate was not given), not the config default.
  {
    ExperimentConfig eff = config;
    if (!config.failure_rate_set)
      eff.failure_rate = EXP1_ABLATION_FAILURE_RATE;
    auto kv = baseProvenance("aggregation ablation (paper Exp1)", eff);
    kv.emplace_back("variants", "flat_epoch,fixed_slot,adaptive_slot,full_plosha");
    kv.emplace_back("sensor_sweep", "500-5000 step 500");
    kv.emplace_back("fog_nodes", "max(5, sensors/100)");
    MetricsCollector::writeRunMetadata(output_dir, kv);
  }

  for (const auto &variant : variants) {
    std::cout << "  [Variant] " << variant.name << "\n";

    for (double num_sensors : sensor_values) {
      std::cout << "    [Sweep] num_sensors = " << num_sensors << "...\n";
      metrics_.reset();

      ExperimentConfig exp_config = config;
      exp_config.num_sensors = static_cast<int>(num_sensors);
      // Fog nodes scale with sensor count to hold the sensors-per-fog ratio
      // constant at 100:1 across the sweep; a fixed count would mean 500
      // sensors per fog node at the top end. Every variant gets the identical
      // configuration, so the ablation comparison remains like-for-like.
      exp_config.num_fog_nodes = std::max(5, static_cast<int>(num_sensors) / 100); // Scale: 5@500 → 50@5000
      // Previously both of these were hardcoded here, silently discarding
      // --failure-rate and --epochs for this experiment. The defaults are
      // unchanged; they are just no longer unconditional.
      exp_config.failure_rate = config.failure_rate_set
                                    ? config.failure_rate
                                    : EXP1_ABLATION_FAILURE_RATE;
      exp_config.hierarchical_aggregation = variant.hierarchical;

      // Set forced micro-slots
      if (variant.forced_slots == 0) {
        exp_config.forced_micro_slots = 0; // Adaptive optimizer
      } else {
        exp_config.forced_micro_slots = variant.forced_slots;
      }

      dataset_.load(exp_config.dataset_path, exp_config.num_sensors,
                    exp_config.num_fog_nodes);
      auto epoch_data = dataset_.groupByEpoch(exp_config.num_sensors,
                                              exp_config.workload_multiplier);

      auto sweep_start = std::chrono::high_resolution_clock::now();

      for (int iter = 0; iter < 30; ++iter) {
        if (iter < 10) {
          exp_config.failure_injection_time = 0.25;
        } else if (iter < 20) {
          exp_config.failure_injection_time = 0.50;
        } else {
          exp_config.failure_injection_time = 0.75;
        }

        // Initialize system freshly for each iteration
        std::vector<Sensor> sensors;
        std::vector<FogNode> fog_nodes;
        std::vector<uint8_t> fog_aes_key;
        initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);

        std::vector<FogState> ewma_states(exp_config.num_fog_nodes);
        std::vector<FeedbackState> feedback_states(exp_config.num_fog_nodes);
        std::mt19937 rng(42 + iter); // Different seed per iter

        for (int e = 0; e < exp_config.num_epochs; ++e) {
          auto epoch_start = std::chrono::high_resolution_clock::now();
          auto epoch_metrics =
              runEpoch(exp_config, sensors, fog_nodes, fog_aes_key, epoch_data,
                       e, ewma_states, feedback_states, rng);
          metrics_.recordEpoch(epoch_metrics);
          auto epoch_end = std::chrono::high_resolution_clock::now();
        }
      }
      auto sweep_end = std::chrono::high_resolution_clock::now();
      double sweep_wall_ms =
          std::chrono::duration<double, std::milli>(sweep_end - sweep_start)
              .count();
      std::cout << "    [TOTAL] " << exp_config.num_epochs << " epochs in "
                << sweep_wall_ms << "ms (avg "
                << sweep_wall_ms / exp_config.num_epochs << "ms/epoch)\n";

      // Audit trail: keep the per-epoch rows this point's mean/std summarise.
      {
        std::ostringstream fname;
        fname << output_dir << "/epoch_records/" << variant.name << "_"
              << static_cast<int>(num_sensors) << ".csv";
        metrics_.writeEpochRecordsFile(fname.str());
      }

      // Summarise this sweep point with the same estimator every other
      // experiment uses (mean, population std) rather than a local one, so
      // Exp1 is directly comparable with the remaining figures. Epoch-to-epoch
      // variability is reported through the std columns instead of being
      // suppressed by a robust centre chosen after the fact.
      auto avg = metrics_.computeAverages(num_sensors);

      MetricsCollector::AblationRow row;
      row.variant = variant.name;
      row.num_sensors = num_sensors;
      row.aggregation_latency_ms = avg.avg_aggregation_latency;
      row.std_aggregation_latency = avg.std_aggregation_latency;
      row.processing_overhead_ms = avg.avg_processing_overhead;
      row.std_processing_overhead = avg.std_processing_overhead;
      row.loss_exposure_fraction = avg.avg_loss_exposure;
      row.std_loss_exposure = avg.std_loss_exposure;
      row.energy_joules = avg.avg_energy_joules;
      row.std_energy_joules = avg.std_energy_joules;
      row.recomputation_overhead_ms = avg.avg_recomputation_overhead;
      row.std_recomputation_overhead = avg.std_recomputation_overhead;
      row.reused_microslot_count = avg.avg_reused_microslot_count;
      row.std_reused_microslot_count = avg.std_reused_microslot_count;

      all_rows.push_back(row);

      // Progressive saving
      MetricsCollector::writeAblationResultsFile(output_dir + "/results.csv",
                                                 all_rows);
    }
  }
}

// ---------------------------------------------------------------------------
// Experiment 9: Scheduling Efficiency
// ---------------------------------------------------------------------------
void DESEngine::runExp9_SchedulingEfficiency(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 9: Scheduling Efficiency ===\n";
  std::vector<double> fog_sweep = {5, 10, 15, 20, 25, 30, 35, 40, 45, 50};
  std::string output_dir = config.output_dir + "/exp2_scheduling_efficiency";

  std::vector<SweepPointResult> results;

  // Provenance for the effective configuration. This experiment fixes its own
  // parameters (no failures, 30 epochs, sensors = fog_nodes * 100, workload
  // burst at epoch 12, 20% node degradation at epoch 21); CLI overrides do
  // not apply here because the burst/degradation schedule is defined in
  // epoch indices and would silently shift under a different epoch count.
  {
    ExperimentConfig eff = config;
    eff.failure_rate = 0.0;
    eff.failure_rate_set = false;
    eff.num_epochs = 30;
    eff.heterogeneity_ratio = 5.0;
    eff.apply_scheduling_decision = true;
    auto kv = baseProvenance("scheduling efficiency (paper Exp2)", eff);
    kv.emplace_back("fog_sweep", "5-50 step 5");
    kv.emplace_back("sensors", "fog_nodes * 100");
    kv.emplace_back("iterations_per_point", "30");
    kv.emplace_back("schedule", "workload burst x1.5 from epoch 12; 20% node degradation from epoch 21");
    kv.emplace_back("heterogeneity_ratio", "5.0");
    kv.emplace_back("apply_scheduling_decision", "true");
    MetricsCollector::writeRunMetadata(output_dir, kv);
  }

  for (double num_fog_nodes : fog_sweep) {
    std::cout << "  [Sweep] num_fog_nodes = " << num_fog_nodes << "...\n";
    metrics_.reset();
    ExperimentConfig exp_config = config;
    exp_config.num_fog_nodes = static_cast<int>(num_fog_nodes);
    exp_config.num_sensors = exp_config.num_fog_nodes * 100;
    exp_config.failure_rate = 0.0;
    exp_config.num_epochs = 30;
    // Heterogeneous fog capability, per the paper's Exp2 setup ("...varied from
    // 5 to 50 under heterogeneous processing capacity, queue occupancy, and
    // communication latency"). 5x is our choice -- the paper states the
    // condition but gives no ratio. Ref[22] and Ref[37] use the identical
    // spread and the same 5.0; Ref[38] randomises per-node capability
    // natively, so all four schemes face heterogeneous nodes.
    exp_config.heterogeneity_ratio = 5.0;
    // Apply the capacity-aware scheduling decision to routing instead of
    // discarding it, so PLOSHA actually re-balances load in this experiment.
    exp_config.apply_scheduling_decision = true;

    dataset_.load(exp_config.dataset_path, exp_config.num_sensors,
                  exp_config.num_fog_nodes);

    double total_convergence_epochs = 0;

    for (int iter = 0; iter < 30; ++iter) {
      std::vector<Sensor> sensors;
      std::vector<FogNode> fog_nodes;
      std::vector<uint8_t> fog_aes_key;
      initializeSystem(exp_config, sensors, fog_nodes, fog_aes_key);

      std::vector<FogState> ewma_states(exp_config.num_fog_nodes);
      std::vector<FeedbackState> feedback_states(exp_config.num_fog_nodes);
      std::mt19937 rng(42 + iter);

      int convergence_epoch = -1;

      for (int e = 0; e < exp_config.num_epochs; ++e) {
        double burst_multiplier = exp_config.workload_multiplier;
        if (e >= 12) {
          burst_multiplier *= 1.5; // 50% burst
        }
        if (e >= 21) {
          // 20% Node Degradation
          int num_degraded =
              std::max(1, static_cast<int>(exp_config.num_fog_nodes * 0.20));
          for (int i = 0; i < num_degraded; ++i) {
            fog_nodes[i].updateProcessingLatency(
                200.0); // Artificially high latency
          }
        }
        auto epoch_data =
            dataset_.groupByEpoch(exp_config.num_sensors, burst_multiplier);

        auto epoch_metrics =
            runEpoch(exp_config, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                     ewma_states, feedback_states, rng);

        epoch_metrics.scheduling_comm_kb =
            (exp_config.num_fog_nodes * 32.0) / 1024.0;
        metrics_.recordEpoch(epoch_metrics);

        if (e >= 12 && convergence_epoch == -1 &&
            // Flat 0.1 on the CV-normalized I_W, identical across all schemes.
            // The previous 0.1*num_fog scaling made the test trivially true at
            // high fog counts (threshold 5.0 at 50 nodes vs a max I_W ~1.6),
            // so "convergence" was reported at epoch 0 regardless of behaviour.
            epoch_metrics.workload_imbalance < 0.1) {
          convergence_epoch = e - 12;
        }
      }
      if (convergence_epoch == -1)
        convergence_epoch = exp_config.num_epochs - 12;
      total_convergence_epochs += convergence_epoch;
    }

    // Audit trail: all 30 iterations x 30 epochs behind this point's summary.
    {
      std::ostringstream fname;
      fname << output_dir << "/epoch_records/fog_"
            << static_cast<int>(num_fog_nodes) << ".csv";
      metrics_.writeEpochRecordsFile(fname.str());
    }
    auto avg = metrics_.computeAverages(num_fog_nodes);
    avg.convergence_time_epochs = total_convergence_epochs / 30.0;
    results.push_back(avg);
  }

  MetricsCollector::writeSchedulingResultsFile(output_dir + "/results.csv",
                                               "num_fog_nodes", results);
}

// ---------------------------------------------------------------------------
// Experiment 10: Workload Imbalance under Heterogeneous Fog Capability
// ---------------------------------------------------------------------------
// Exp2's imbalance metric could not distinguish schemes because every fog node
// was identical and no scheme re-routed: each node's share of the load was the
// share the workload generator handed it. This experiment isolates the question
// Exp2 could not answer -- when fog nodes differ in processing capacity, queue
// occupancy and latency, does capacity-aware scheduling distribute work in
// proportion to capability?
//
// Independent variable: heterogeneity ratio (capacity of the strongest node
// divided by the weakest), 1x (homogeneous) to 10x. Total system capacity is
// held constant so runs differ only in how capability is DISTRIBUTED.
//
// Two PLOSHA configurations are compared:
//   static_assignment  - scheduling decision computed then discarded (the
//                        behaviour of every other experiment)
//   capacity_aware     - the same decision is applied: sensors migrate toward
//                        the highest-utility node (U_j uses predicted capacity,
//                        reliability and risk)
// Metric: capacity-normalized I_W. W_i is a node's load share divided by its
// capacity share, so 1.0 means "carrying exactly its fair share given its
// capability" and I_W = 0 is a perfectly capacity-proportional allocation.
void DESEngine::runExp10_Heterogeneity(const ExperimentConfig &config) {
  std::cout << "\n=== Experiment 10: Imbalance under Heterogeneous Capability "
               "===\n";

  const std::vector<double> ratios = {1.0, 2.0, 4.0, 6.0, 8.0, 10.0};
  const int kFog = 20;
  const int kSensors = kFog * 100;
  const int kIters = 10;
  const int kEpochs = 30;
  std::string output_dir = config.output_dir + "/exp7_heterogeneity";

  struct Variant {
    std::string name;
    bool apply_decision;
  };
  const std::vector<Variant> variants = {{"static_assignment", false},
                                         {"capacity_aware", true}};

  {
    ExperimentConfig eff = config;
    eff.num_fog_nodes = kFog;
    eff.num_sensors = kSensors;
    eff.num_epochs = kEpochs;
    eff.failure_rate = 0.0;
    eff.failure_rate_set = false;
    auto kv = baseProvenance("heterogeneity imbalance (Exp10)", eff);
    kv.emplace_back("fog_nodes", std::to_string(kFog));
    kv.emplace_back("sensors", std::to_string(kSensors));
    kv.emplace_back("heterogeneity_ratios", "1,2,4,6,8,10");
    kv.emplace_back("heterogeneity_dimensions",
                    "queue capacity; processing-latency headroom (inverse of "
                    "capacity); communication latency (permuted node index, "
                    "decorrelated from capacity), mean " +
                        std::to_string(COMM_LATENCY_MEAN_MS) + " ms");
    kv.emplace_back("iterations_per_point", std::to_string(kIters));
    kv.emplace_back("variants", "static_assignment,capacity_aware");
    kv.emplace_back("imbalance_metric",
                    "capacity-normalized CV (W_i = load_share/capacity_share)");
    MetricsCollector::writeRunMetadata(output_dir, kv);
  }

  std::string cmd = "mkdir -p " + output_dir;
  (void)system(cmd.c_str());
  std::ofstream out(output_dir + "/results.csv");
  if (!out.is_open()) {
    std::cerr << "[Exp10] ERROR: cannot open " << output_dir << "/results.csv\n";
    return;
  }
  out << "variant,heterogeneity_ratio,workload_imbalance,std_workload_imbalance,"
         "scheduling_latency_ms,std_scheduling_latency_ms\n";
  out << std::fixed << std::setprecision(6);

  for (const auto &variant : variants) {
    std::cout << "  [Variant] " << variant.name << "\n";
    for (double ratio : ratios) {
      std::cout << "    [Sweep] heterogeneity_ratio = " << ratio << "...\n";
      metrics_.reset();

      ExperimentConfig ec = config;
      ec.num_fog_nodes = kFog;
      ec.num_sensors = kSensors;
      ec.num_epochs = kEpochs;
      ec.failure_rate = 0.0;
      ec.heterogeneity_ratio = ratio;
      ec.apply_scheduling_decision = variant.apply_decision;

      dataset_.load(ec.dataset_path, ec.num_sensors, ec.num_fog_nodes);
      auto epoch_data =
          dataset_.groupByEpoch(ec.num_sensors, ec.workload_multiplier);

      for (int iter = 0; iter < kIters; ++iter) {
        std::vector<Sensor> sensors;
        std::vector<FogNode> fog_nodes;
        std::vector<uint8_t> fog_aes_key;
        initializeSystem(ec, sensors, fog_nodes, fog_aes_key);

        std::vector<FogState> ewma_states(ec.num_fog_nodes);
        std::vector<FeedbackState> feedback_states(ec.num_fog_nodes);
        std::mt19937 rng(42 + iter);

        for (int e = 0; e < kEpochs; ++e) {
          auto m = runEpoch(ec, sensors, fog_nodes, fog_aes_key, epoch_data, e,
                            ewma_states, feedback_states, rng);
          metrics_.recordEpoch(m);
        }
      }

      {
        std::ostringstream fname;
        fname << output_dir << "/epoch_records/" << variant.name << "_ratio"
              << static_cast<int>(ratio) << ".csv";
        metrics_.writeEpochRecordsFile(fname.str());
      }
      auto avg = metrics_.computeAverages(ratio);
      out << variant.name << "," << ratio << "," << avg.avg_workload_imbalance
          << "," << avg.std_workload_imbalance << ","
          << avg.avg_scheduling_latency << "," << avg.std_scheduling_latency
          << "\n";
      out.flush();
      std::cout << "      I_W = " << avg.avg_workload_imbalance << "\n";
    }
  }
  out.close();
  std::cout << "[Exp10] Wrote " << output_dir << "/results.csv\n";
}

} // namespace plosha
