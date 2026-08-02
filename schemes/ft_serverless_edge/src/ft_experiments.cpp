#include "ft_serverless_edge.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>

// ── Constructor ──────────────────────────────────────────────────────────
FTServerlessEdgeSim::FTServerlessEdgeSim(const SimConfig &cfg)
    : cfg_(cfg), rng_(cfg.seed) {
  loadDataset();
}

// ── Crypto Initialization (for fair wall-clock comparison) ───────────────
void FTServerlessEdgeSim::initCrypto() {
  if (crypto_initialized_)
    return;
  aes_key_.resize(32); // AES-256
  RAND_bytes(aes_key_.data(), 32);
  crypto_initialized_ = true;
}

int FTServerlessEdgeSim::aesEncryptInPlace(const uint8_t *plaintext,
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

// ── Helper: compute per-cloudlet load from request assignments ────────────
static void computeCloudletLoad(const std::vector<DSPRequest> &reqs,
                                int num_cloudlets, std::vector<int> &load_out) {
  load_out.assign(num_cloudlets, 0);
  for (const auto &req : reqs) {
    int cl = req.origin_fog;
    if (cl >= 0 && cl < num_cloudlets)
      load_out[cl] += static_cast<int>(req.data_rate * 1000);  // Use actual data rate as workload
  }
}

SimResult FTServerlessEdgeSim::run() {
  switch (cfg_.experiment) {
  case 1:
    return runExp1_SensorScalability();
  case 2:
    return runExp2_FogScalability();
  case 3:
    return runExp3_WorkloadIntensity();
  case 4:
    return runExp4_FailureRate();
  case 5:
    return runExp5_LossExposure();
  case 6:
    return runExp6_RecoveryComm();
  case 9:
    return runExp9_SchedulingEfficiency();
  default:
    return {cfg_.variable_value, 0, 0, 0};
  }
}

// ── Helper: run a batch of requests through the simulation ───────────────
static double runBatch(FTServerlessEdgeSim *sim, std::vector<DSPRequest> &reqs,
                       int num_cloudlets, double fail_override,
                       double *out_queue_util, double *out_recovery_freq,
                       double *out_completeness, double *out_availability,
                       double *out_recovery_latency, double *out_comm_kb) {
  double total_delay = 0.0;
  int total_count = 0;
  int failure_count = 0;
  int recovery_count = 0;
  double total_recovery_delay = 0.0;
  double total_comm_kb = 0.0;
  int completed = 0;

  int num_slots = std::max(1, (int)reqs.size() / 20);
  int per_slot = std::max(1, (int)reqs.size() / num_slots);

  for (int tau = 0; tau < num_slots; tau++) {
    int start = tau * per_slot;
    int end = std::min(start + per_slot, (int)reqs.size());

    for (int i = start; i < end; i++) {
      auto &req = reqs[i];
      // Place functions via Fwk
      sim->algorithmFwk(req);

      // Override failure if specified
      if (fail_override > 0) {
        req.had_failure = (sim->randDouble(0, 1) < fail_override);
      }

      // Simulate failover
      if (req.had_failure) {
        sim->simulateFailover(req);
        failure_count++;
      }

      // Compute delay
      double d = sim->computeTotalDelay(req);
      total_delay += d;
      total_count++;

      if (req.recovery_delay > 0) {
        recovery_count++;
        total_recovery_delay += req.recovery_delay;
      }

      // Communication overhead = state buffer transfers during recovery
      if (req.had_failure) {
        for (auto &fn : req.functions) {
          if (!fn.standby_cloudlets.empty()) {
            total_comm_kb += req.state_buffer_kb;
          }
        }
      }

      if (d < INF_DELAY)
        completed++;

      // Proactive adjustment for subsequent slots
      if (tau < num_slots - 1 && i + per_slot < (int)reqs.size()) {
        double next_rate =
            reqs[std::min(i + per_slot, (int)reqs.size() - 1)].data_rate;
        sim->algorithmAdj(req, tau, next_rate);
      }
    }
  }

  double avg_delay = (total_count > 0) ? total_delay / total_count : 0;
  if (out_queue_util)
    *out_queue_util =
        (total_count > 0) ? (double)failure_count / total_count : 0;
  if (out_recovery_freq)
    *out_recovery_freq = recovery_count;
  if (out_completeness)
    *out_completeness = (total_count > 0) ? (double)completed / total_count : 0;
  if (out_availability)
    *out_availability =
        (total_count > 0) ? 1.0 - (double)failure_count / total_count : 1.0;
  if (out_recovery_latency)
    *out_recovery_latency =
        (recovery_count > 0) ? total_recovery_delay / recovery_count : 0;
  if (out_comm_kb)
    *out_comm_kb = total_comm_kb;

  return avg_delay;
}

// ── Exp1: Sensor Scalability ─────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp1_SensorScalability() {
  int n_sensors = (int)cfg_.variable_value;
  int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  std::vector<DSPRequest> reqs;
  for (int i = 0; i < n_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    reqs.push_back(createRequest(i, i % 100, i % n_cl, row.bytes_transferred,
                                 row.packet_size, row.is_failure));
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);
  double avg = runBatch(this, reqs, n_cl, -1, nullptr, nullptr, nullptr,
                        nullptr, nullptr, nullptr);
  return {cfg_.variable_value, avg, 0, 0};
}

// ── Exp2: Fog Scalability ────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp2_FogScalability() {
  int n_cl = (int)cfg_.variable_value;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  std::vector<DSPRequest> reqs;
  for (int i = 0; i < n_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    reqs.push_back(createRequest(i, i % 100, i % n_cl, row.bytes_transferred,
                                 row.packet_size, row.is_failure));
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);
  double avg = runBatch(this, reqs, n_cl, -1, nullptr, nullptr, nullptr,
                        nullptr, nullptr, nullptr);
                        
  // Calculate capacity-normalized Workload Imbalance (I_W)
  double total_w = 0.0;
  double total_cap = 0.0;
  for (int i = 0; i < n_cl; i++) {
    total_w += cloudlet_load_[i];
    total_cap += cloudlets_[i].memory_capacity_mb;
  }
  double w_bar = 1.0;
  double var_sum = 0.0;
  for (int i = 0; i < n_cl; i++) {
    double load_share = (total_w > 0) ? (cloudlet_load_[i] / total_w) : 0.0;
    double cap_share = (total_cap > 0) ? (cloudlets_[i].memory_capacity_mb / total_cap) : (1.0 / n_cl);
    double w_i = (cap_share > 0) ? (load_share / cap_share) : 0.0;
    double diff = w_i - w_bar;
    var_sum += diff * diff;
  }
  double iw = std::sqrt(var_sum / n_cl) / (w_bar + 1e-9);

  return {cfg_.variable_value, avg, iw, 0};
}

// ── Exp3: Workload Intensity ─────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp3_WorkloadIntensity() {
  double rate_mult = cfg_.variable_value; // reporting rate multiplier
  int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  // Scale the number of requests by the workload multiplier.
  // Higher workload intensity = more sensor reports per aggregation epoch.
  int effective_sensors = static_cast<int>(n_sensors * rate_mult);

  std::vector<DSPRequest> reqs;
  for (int i = 0; i < effective_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    // Also scale data rate with workload to model heavier per-request
    // processing
    double scaled_rate = row.bytes_transferred * (rate_mult / 1.0);
    reqs.push_back(createRequest(i, i % 100, i % n_cl, scaled_rate,
                                 row.packet_size, row.is_failure));
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);
  double queue_util = 0, recovery_freq = 0;
  double avg = runBatch(this, reqs, n_cl, -1, &queue_util, &recovery_freq,
                        nullptr, nullptr, nullptr, nullptr);
  return {cfg_.variable_value, avg, queue_util, recovery_freq};
}

// ── Exp4: Failure Rate ───────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp4_FailureRate() {
  double fail_rate = cfg_.variable_value; // Already a fraction (0.02-0.35)
  int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  // Override function failure probabilities
  std::vector<DSPRequest> reqs;
  for (int i = 0; i < n_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    auto req = createRequest(i, i % 100, i % n_cl, row.bytes_transferred,
                             row.packet_size, false);
    for (auto &fn : req.functions)
      fn.failure_prob = fail_rate;
    reqs.push_back(std::move(req));
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);
  double completeness = 0, availability = 0, rec_lat = 0;
  runBatch(this, reqs, n_cl, fail_rate, nullptr, nullptr, &completeness,
           &availability, &rec_lat, nullptr);
  return {cfg_.variable_value, rec_lat, completeness, availability};
}

// ── Exp5: Loss Exposure ──────────────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp5_LossExposure() {
  int micro_slots = (int)cfg_.variable_value;
  int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  std::vector<DSPRequest> reqs;
  for (int i = 0; i < n_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    reqs.push_back(createRequest(i, i % 100, i % n_cl, row.bytes_transferred,
                                 row.packet_size, row.is_failure));
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);

  // Place all requests
  for (auto &req : reqs)
    algorithmFwk(req);

  // Loss exposure: fraction of DAG execution unprotected between adjustments
  // With K micro-slots, adjustment happens every 1/K of the execution window
  // Loss exposure = average unprotected fraction across requests
  double total_loss = 0.0;
  for (auto &req : reqs) {
    int nf = (int)req.functions.size();
    int unprotected = 0;
    for (auto &fn : req.functions) {
      // A function is "exposed" if it has no standby OR standby is stale
      // With more micro-slots, standbys are refreshed more frequently
      double protection_prob = 1.0 - std::pow(0.5, micro_slots);
      if (fn.standby_cloudlets.empty() || randDouble(0, 1) > protection_prob)
        unprotected++;
    }
    total_loss += (double)unprotected / nf;
  }
  double avg_loss = total_loss / reqs.size();
  return {cfg_.variable_value, avg_loss, 0, 0};
}

// ── Exp6: Recovery Communication ─────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp6_RecoveryComm() {
  int incomplete_slots = (int)cfg_.variable_value;
  int n_cl = cfg_.num_cloudlets > 0 ? cfg_.num_cloudlets : 10;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 1000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  std::vector<DSPRequest> reqs;
  for (int i = 0; i < n_sensors; i++) {
    auto &row = dataset_[i % dataset_.size()];
    reqs.push_back(createRequest(i, i % 100, i % n_cl, row.bytes_transferred,
                                 row.packet_size, true)); // force failure
  }
  computeCloudletLoad(reqs, n_cl, cloudlet_load_);

  // Place all requests
  for (auto &req : reqs)
    algorithmFwk(req);

  // Communication overhead = state buffer transfers during recovery
  // More incomplete slots → more state that needs to be re-transferred
  double total_comm_kb = 0.0;
  for (auto &req : reqs) {
    for (auto &fn : req.functions) {
      if (fn.standby_cloudlets.empty())
        continue;
      // Each incomplete slot requires transferring accumulated state
      double state_per_slot = req.state_buffer_kb;
      // Total = incomplete_slots * state_buffer * num_affected_functions
      total_comm_kb += incomplete_slots * state_per_slot;
    }
  }
  // Average per request
  double avg_comm_kb = total_comm_kb / reqs.size();
  return {cfg_.variable_value, avg_comm_kb, 0, 0};
}

// ── Exp9: Scheduling Efficiency ──────────────────────────────────────────
SimResult FTServerlessEdgeSim::runExp9_SchedulingEfficiency() {
  int n_cl = (int)cfg_.variable_value;
  int n_sensors = cfg_.num_sensors > 0 ? cfg_.num_sensors : 2000;
  rng_.seed(cfg_.seed);
  buildSECNetwork(n_cl);
  initCrypto();

  double overall_sched_ms = 0.0;
  double overall_imbalance = 0.0;
  int num_epochs = 30;

  for (int iter = 0; iter < 30; ++iter) {
      rng_.seed(cfg_.seed + iter);
      buildSECNetwork(n_cl);
      
      double total_sched_ms_accum = 0.0;
      double total_imbalance_accum = 0.0;
      
      for (int e = 0; e < num_epochs; ++e) {
          double multiplier = (e >= 12) ? 1.5 : 1.0;
          int current_sensors = n_sensors * multiplier;
          std::vector<DSPRequest> reqs;
          for (int i = 0; i < current_sensors; i++) {
            auto &row = dataset_[i % dataset_.size()];
            // FAIRNESS (workload imbalance): apply the SAME 25%-to-node-0
            // hotspot formula PLOSHA/FedDQN/FT-Workflow use. NOTE createRequest
            // ignores its fog_id argument and derives origin_fog internally from
            // (sensor_id % 4), whose "1 + sensor_id % (n_cl-1)" split leaves some
            // nodes empty (e.g. node 1 at n_cl=5) and does NOT match the other
            // schemes' distribution -- so we OVERRIDE origin_fog here, Exp9 only,
            // leaving createRequest untouched for the other experiments.
            int origin = (i < static_cast<int>(current_sensors * 0.25))
                             ? 0
                             : 1 + (i % std::max(1, n_cl - 1));
            DSPRequest req = createRequest(i, i % 100, origin, row.bytes_transferred,
                                           row.packet_size, row.is_failure);
            req.origin_fog = origin;
            reqs.push_back(std::move(req));
          }
          
          std::fill(cloudlet_load_.begin(), cloudlet_load_.end(), 0);
          computeCloudletLoad(reqs, n_cl, cloudlet_load_);

          // Node degradation
          if (e >= 21) {
              int num_degraded = std::max(1, static_cast<int>(n_cl * 0.20));
              for (int c = 0; c < num_degraded; ++c) {
                  cloudlets_[c].memory_capacity_mb /= 2.0; // Artificial degradation
              }
          }

          auto sched_start = std::chrono::high_resolution_clock::now();
          for (auto &req : reqs) {
            algorithmFwk(req);
          }
          auto sched_end = std::chrono::high_resolution_clock::now();
          
          // Restore capacity
          if (e >= 21) {
              int num_degraded = std::max(1, static_cast<int>(n_cl * 0.20));
              for (int c = 0; c < num_degraded; ++c) {
                  cloudlets_[c].memory_capacity_mb *= 2.0;
              }
          }

          double sched_ms = std::chrono::duration<double, std::milli>(sched_end - sched_start).count();
          total_sched_ms_accum += (sched_ms / std::max(1, (int)reqs.size()));

          // Workload imbalance over POST-PLACEMENT load: count the functions
          // algorithmFwk actually placed on each cloudlet (fn.active_cloudlet),
          // not the requests' origin. Measuring origins would report the input
          // traffic distribution, which is identical for every scheme by
          // construction and therefore blind to the scheduler -- exactly the
          // quantity the paper's I_W claim ("load-sharing ... lower imbalance")
          // requires to be measured AFTER placement. This runs after the
          // algorithmFwk loop above, so active_cloudlet is populated.
          std::vector<int> origin_count(n_cl, 0);
          for (const auto &req : reqs)
            for (const auto &fn : req.functions)
              if (fn.active_cloudlet >= 0 && fn.active_cloudlet < n_cl)
                origin_count[fn.active_cloudlet]++;
          // Capacity-normalized Workload Imbalance
          double total_w = 0.0;
          double total_cap = 0.0;
          for (int c = 0; c < n_cl; c++) {
            total_w += origin_count[c];
            total_cap += cloudlets_[c].memory_capacity_mb;
          }
          double w_bar = 1.0;
          double var_sum = 0.0;
          for (int c = 0; c < n_cl; c++) {
            double load_share = (total_w > 0) ? (static_cast<double>(origin_count[c]) / total_w) : 0.0;
            double cap_share = (total_cap > 0) ? (cloudlets_[c].memory_capacity_mb / total_cap) : (1.0 / n_cl);
            double w_i = (cap_share > 0) ? (load_share / cap_share) : 0.0;
            double diff = w_i - w_bar;
            var_sum += diff * diff;
          }
          total_imbalance_accum += std::sqrt(var_sum / n_cl) / (w_bar + 1e-9);
      }
      overall_sched_ms += total_sched_ms_accum / num_epochs;
      overall_imbalance += total_imbalance_accum / num_epochs;
  }

  return {cfg_.variable_value, overall_sched_ms / 30.0, overall_imbalance / 30.0, 0};
}
