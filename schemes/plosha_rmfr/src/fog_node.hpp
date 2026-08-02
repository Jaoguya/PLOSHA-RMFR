#ifndef PLOSHA_FOG_NODE_HPP
#define PLOSHA_FOG_NODE_HPP

#include "crypto_wrapper.hpp"
#include "config.hpp"
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace plosha {

// State vector for a fog node: State_i(t) = [W_i, Q_i, L_i, Rel_i]
struct FogState {
    double workload   = 0.0;  // W_i ∈ [0,1]
    double queue_load = 0.0;  // Q_i ∈ [0,1]
    double latency    = 0.0;  // L_i ∈ [0,1]
    double reliability = 1.0; // Rel_i ∈ [0,1]
};

// An encrypted reading queued for processing
struct QueuedReading {
    int sensor_id;
    AESCiphertext aes_ct;
    uint32_t plaintext_value;  // Kept for verification (not used in aggregation)
};

class FogNode {
public:
    // comm_latency_ms: per-node communication (network) latency. Defaults to 0
    // so every experiment that does not model heterogeneity is bit-identical to
    // before. It is added to processing latency when forming L_i, so the
    // existing scheduling utility U_j consumes it without any change to the
    // ALPHA_C/ALPHA_R/ALPHA_K weights.
    FogNode(int fog_id, int queue_capacity, double max_latency_ms = 100.0,
            double comm_latency_ms = 0.0);

    // Assign a sensor to this fog node
    void assignSensor(int sensor_id);
    void removeSensor(int sensor_id);

    // Submit an encrypted reading to the queue (thread-safe)
    void submitReading(const QueuedReading& reading);

    // Drain all queued readings for epoch processing
    std::vector<QueuedReading> drainQueue();

    // Get current state vector (R5: W_i normalized, R6: L_i normalized)
    FogState getState() const;

    // Update state after processing
    void updateProcessingLatency(double latency_ms);
    void updateReliability(double new_rel);

    // Failure management
    void setFailed(bool failed) { is_failed_ = failed; }
    bool isFailed() const { return is_failed_; }

    // Micro-slot aggregation state
    void setMicroSlotAggregates(std::vector<PaillierCiphertext>&& aggs);
    const std::vector<PaillierCiphertext>& microSlotAggregates() const { return micro_aggregates_; }
    void clearMicroSlots() { micro_aggregates_.clear(); }

    // Accessors
    int fogId() const { return fog_id_; }
    const std::vector<int>& assignedSensors() const { return assigned_sensors_; }
    int queueCapacity() const { return queue_capacity_; }
    int currentQueueSize() const;
    double reliability() const { return reliability_; }

private:
    int fog_id_;
    int queue_capacity_;
    double max_latency_ms_;  // For L_i normalization (R6)
    double comm_latency_ms_ = 0.0;  // Per-node network latency (heterogeneity)

    std::vector<int> assigned_sensors_;
    std::vector<PaillierCiphertext> micro_aggregates_;

    // Thread-safe queue (R: Q2 — real pthreads)
    // Using unique_ptr<mutex> to make FogNode movable for std::vector
    mutable std::unique_ptr<std::mutex> queue_mutex_;
    std::queue<QueuedReading> reading_queue_;

    // State
    double processing_latency_ms_ = 0.0;
    double reliability_ = 1.0;
    int total_queue_weight_ = 0;
    bool is_failed_ = false;
};

} // namespace plosha

#endif // PLOSHA_FOG_NODE_HPP
