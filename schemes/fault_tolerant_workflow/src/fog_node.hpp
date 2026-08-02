#ifndef FTWORKFLOW_FOG_NODE_HPP
#define FTWORKFLOW_FOG_NODE_HPP

#include "crypto_wrapper.hpp"
#include "config.hpp"
#include <vector>
#include <queue>
#include <mutex>
#include <memory>

namespace ftworkflow {

// State vector for a fog/VM node
struct NodeState {
    double workload    = 0.0;  // Normalized task load [0,1]
    double queue_load  = 0.0;  // Queue occupancy [0,1]
    double latency     = 0.0;  // Normalized processing latency [0,1]
    double reliability = 1.0;  // Node reliability [0,1]
    double perf_coeff  = 1.0;  // Performance coefficient p_j(t) from Ref[37]
};

// Encrypted reading queued for processing
struct QueuedReading {
    int sensor_id;
    AESCiphertext aes_ct;
    uint32_t plaintext_value;
};

// Checkpoint state stored per-node (Ref[37] §III-B)
struct Checkpoint {
    PaillierCiphertext partial_aggregate;
    int readings_processed = 0;
    size_t storage_bytes    = 0;
    bool valid              = false;
};

class FogNode {
public:
    // comm_latency_ms: per-node network latency, added to processing latency
    // when forming L_i. Defaults to 0 so non-heterogeneous experiments are
    // bit-identical. Mirrors PLOSHA's FogNode exactly.
    FogNode(int fog_id, int queue_capacity, double max_latency_ms = 100.0,
            double comm_latency_ms = 0.0);

    void assignSensor(int sensor_id);
    void submitReading(const QueuedReading& reading);
    std::vector<QueuedReading> drainQueue();
    NodeState getState() const;

    void updateProcessingLatency(double latency_ms);
    void updateReliability(double new_rel);
    void updatePerfCoefficient(double coeff);

    void setFailed(bool failed) { is_failed_ = failed; }
    bool isFailed() const { return is_failed_; }

    // Checkpoint management
    void saveCheckpoint(const Checkpoint& ckpt);
    const Checkpoint& lastCheckpoint() const { return checkpoint_; }
    void clearCheckpoint() { checkpoint_ = Checkpoint{}; }

    int fogId() const { return fog_id_; }
    const std::vector<int>& assignedSensors() const { return assigned_sensors_; }
    int queueCapacity() const { return queue_capacity_; }
    int currentQueueSize() const;
    double perfCoefficient() const { return perf_coeff_; }

private:
    int fog_id_;
    int queue_capacity_;
    double max_latency_ms_;
    double comm_latency_ms_ = 0.0;  // Per-node network latency (heterogeneity)
    int total_queue_weight_ = 0;
    std::vector<int> assigned_sensors_;
    mutable std::unique_ptr<std::mutex> queue_mutex_;
    std::queue<QueuedReading> reading_queue_;
    Checkpoint checkpoint_;

    double processing_latency_ms_ = 0.0;
    double reliability_ = 1.0;
    double perf_coeff_  = 1.0;
    bool is_failed_     = false;
};

} // namespace ftworkflow

#endif // FTWORKFLOW_FOG_NODE_HPP
