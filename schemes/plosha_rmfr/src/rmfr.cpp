#include "rmfr.hpp"
#include "plosha.hpp"
#include "config.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace plosha {

// Phase IV Step 1: Recovery Urgency (paper Eq. 26)
// RU_i(t) = ρ₁·Risk + ρ₂·(1 − V) + ρ₃·(1 − Rel)
double RMFREngine::computeRecoveryUrgency(double risk, double completeness_V,
                                          double reliability) {
  return RHO_1 * risk + RHO_2 * (1.0 - completeness_V) +
         RHO_3 * (1.0 - reliability);
}

// Phase IV Step 2: Determine recovery mode (paper Eq. 28)
// Escalation: Normal → Delegation → MicroRecovery → Failover
RecoveryMode RMFREngine::determineRecoveryMode(bool completeness_flag,
                                               double recovery_urgency,
                                               double reliability, double tau_1,
                                               double tau_2, double tau_3,
                                               double tau_f) {
  // Failover: highest precedence safety check (paper Eq. 28 case 4)
  // RU >= τ₃ or Rel <= τ_f triggers immediate failover
  if (recovery_urgency >= tau_3 || reliability <= tau_f) {
    return RecoveryMode::Failover;
  }

  // Complete aggregation branch (Φ_i(t) = 0, completeness_flag = true)
  if (completeness_flag) {
    if (recovery_urgency < tau_1) {
      return RecoveryMode::Normal;
    }
    // τ₁ ≤ RU < τ₃: Delegation (covers paper's τ₁..τ₂ range
    // and the gap τ₂..τ₃ not explicitly defined in Eq. 28)
    return RecoveryMode::Delegation;
  }

  // Incomplete aggregation branch (Φ_i(t) = 1, completeness_flag = false)
  // RU < τ₃: MicroRecovery (paper Eq. 28 case 3)
  return RecoveryMode::MicroRecovery;
}

// Phase IV Step 3: Select best candidate (paper Eq. 30)
// U_j = α_c·Cap_j + α_r·Rel_j + α_k·(1 − Risk_j)
int RMFREngine::selectRecoveryCandidate(
    const std::vector<FogNode> &fog_nodes,
    const std::vector<PredictionVector> &predictions, int failed_fog_id) {
  int best_id = -1;
  double best_utility = -1.0;

  for (size_t j = 0; j < fog_nodes.size(); ++j) {
    if (static_cast<int>(j) == failed_fog_id)
      continue;
    if (fog_nodes[j].isFailed())
      continue;

    double utility = ALPHA_C * predictions[j].capacity +
                     ALPHA_R * fog_nodes[j].reliability() +
                     ALPHA_K * (1.0 - predictions[j].risk);

    if (utility > best_utility) {
      best_utility = utility;
      best_id = static_cast<int>(j);
    }
  }
  return best_id;
}

// Phase IV Steps 4-6: Execute recovery
RecoveryResult RMFREngine::executeRecovery(
    CryptoWrapper &crypto, std::vector<FogNode> &fog_nodes,
    const std::vector<PredictionVector> &predictions, int failed_fog_id,
    const std::vector<uint8_t> &aes_key, double tau_1, double tau_2,
    double tau_3, double tau_f, double completeness_V, bool completeness_flag,
    double risk, double reliability,
    const std::vector<int> &incomplete_slot_indices,
    const std::vector<MicroSlot> &slots) {
  auto start = std::chrono::high_resolution_clock::now();
  RecoveryResult result;

  double RU = computeRecoveryUrgency(risk, completeness_V, reliability);
  result.mode = determineRecoveryMode(completeness_flag, RU, reliability, tau_1,
                                      tau_2, tau_3, tau_f);

  if (result.mode == RecoveryMode::Normal) {
    auto end = std::chrono::high_resolution_clock::now();
    result.recovery_latency_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    return result;
  }

  // Find best candidate
  int target_id =
      selectRecoveryCandidate(fog_nodes, predictions, failed_fog_id);
  if (target_id < 0) {
    result.success = false;
    auto end = std::chrono::high_resolution_clock::now();
    result.recovery_latency_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    return result;
  }
  result.recovery_target_id = target_id;

  auto &failed_node = fog_nodes[failed_fog_id];
  auto &target_node = fog_nodes[target_id];

  switch (result.mode) {
  case RecoveryMode::Delegation: {
    // Step 4: Construct DSP_i(t) and transfer state
    DelegationPackage dsp;
    dsp.source_fog_id = failed_fog_id;
    dsp.target_fog_id = target_id;
    dsp.state_snapshot = failed_node.getState();
    dsp.sensor_list = failed_node.assignedSensors();
    // Payload: state (4 doubles) + sensor list (4 bytes each)
    dsp.payload_bytes = sizeof(FogState) + dsp.sensor_list.size() * sizeof(int);
    
    // Actually transfer ~20% of sensors from overloaded to target
    size_t transfer_count = std::max((size_t)1, dsp.sensor_list.size() / 5);
    for (size_t i = 0; i < transfer_count; ++i) {
        int sid = dsp.sensor_list[i];
        target_node.assignSensor(sid);
        failed_node.removeSensor(sid);
    }

    result.communication_bytes = dsp.payload_bytes;
    result.success = true;
    break;
  }
  case RecoveryMode::MicroRecovery: {
    // Step 5: Re-aggregate only incomplete micro-slots
    // Perform the same TEE-transform + Paillier aggregation as normal
    // aggregation for each incomplete slot's readings. The cost scales
    // with the actual number of readings in those slots.
    result.incomplete_slots = incomplete_slot_indices;
    size_t re_agg_bytes = 0;

    for (int slot_idx : incomplete_slot_indices) {
      if (slot_idx >= 0 && slot_idx < static_cast<int>(slots.size()) &&
          !slots[slot_idx].readings.empty()) {
        // Real re-aggregation: TEE-transform each reading and aggregate
        std::vector<PaillierCiphertext> paillier_cts;
        paillier_cts.reserve(slots[slot_idx].readings.size());
        for (const auto &reading : slots[slot_idx].readings) {
          PaillierCiphertext pct =
              crypto.teeTransform(aes_key, reading.aes_ct);
          paillier_cts.push_back(std::move(pct));
        }
        PaillierCiphertext slot_agg = crypto.aggregateMultiple(paillier_cts);
        re_agg_bytes += slot_agg.byteSize();
      } else {
        // Empty or out-of-range slot: minimal cost
        PaillierCiphertext zero_ct = crypto.paillierEncrypt(0);
        re_agg_bytes += zero_ct.byteSize();
      }
    }

    result.communication_bytes = re_agg_bytes;
    result.success = true;
    break;
  }
  case RecoveryMode::Failover: {
    // Step 6: Construct FSM_i(t), reassign sensors
    FailoverPackage fsm;
    fsm.source_fog_id = failed_fog_id;
    fsm.target_fog_id = target_id;
    fsm.state_snapshot = failed_node.getState();
    fsm.reassigned_sensors = failed_node.assignedSensors();

    // Transfer micro-slot aggregates (real Paillier ciphertexts)
    const auto &micro_aggs = failed_node.microSlotAggregates();
    fsm.partial_aggregates.reserve(micro_aggs.size());
    for (const auto &agg : micro_aggs) {
      fsm.partial_aggregates.push_back(agg);
    }

    // Payload: state + sensors + Paillier ciphertexts
    fsm.payload_bytes =
        sizeof(FogState) + fsm.reassigned_sensors.size() * sizeof(int);
    for (const auto &pa : fsm.partial_aggregates) {
      fsm.payload_bytes += pa.byteSize();
    }

    result.communication_bytes = fsm.payload_bytes;

    // Actually reassign sensors to target node
    for (int sid : fsm.reassigned_sensors) {
      target_node.assignSensor(sid);
    }

    result.success = true;
    break;
  }
  default:
    break;
  }

  auto end = std::chrono::high_resolution_clock::now();
  result.recovery_latency_ms =
      std::chrono::duration<double, std::milli>(end - start).count();
  return result;
}

// Phase IV Step 7: Update reliability (paper Eq. 38)
// Rel_i(t+1) = min(1, β_r·Rel + (1 − β_r)·[λ_s·Succ + λ_v·V])
double RMFREngine::updateReliability(double current_rel, bool success,
                                     double completeness_V) {
  double succ_val = success ? 1.0 : 0.0;
  double new_rel =
      BETA_REL * current_rel +
      (1.0 - BETA_REL) * (LAMBDA_S * succ_val + LAMBDA_V * completeness_V);
  return std::min(1.0, std::max(0.0, new_rel));
}

} // namespace plosha
