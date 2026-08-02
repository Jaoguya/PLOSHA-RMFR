#include "fog_node.hpp"
#include <algorithm>

namespace ftworkflow {

FogNode::FogNode(int fog_id, int queue_capacity, double max_latency_ms,
                 double comm_latency_ms)
    : fog_id_(fog_id)
    , queue_capacity_(queue_capacity)
    , max_latency_ms_(max_latency_ms)
    , comm_latency_ms_(comm_latency_ms)
    , queue_mutex_(std::make_unique<std::mutex>()) {}

void FogNode::assignSensor(int sensor_id) {
    assigned_sensors_.push_back(sensor_id);
}

void FogNode::submitReading(const QueuedReading& reading) {
    std::lock_guard<std::mutex> lock(*queue_mutex_);
    reading_queue_.push(reading);
    total_queue_weight_ += reading.plaintext_value;
}

std::vector<QueuedReading> FogNode::drainQueue() {
    std::lock_guard<std::mutex> lock(*queue_mutex_);
    std::vector<QueuedReading> readings;
    readings.reserve(reading_queue_.size());
    while (!reading_queue_.empty()) {
        readings.push_back(std::move(reading_queue_.front()));
        reading_queue_.pop();
    }
    total_queue_weight_ = 0;
    return readings;
}

NodeState FogNode::getState() const {
    NodeState state;
    state.workload = (queue_capacity_ > 0)
        ? std::min(1.0, static_cast<double>(total_queue_weight_) / (queue_capacity_ * 35000.0))
        : 0.0;
    state.queue_load = state.workload;
    // Communication latency added to processing latency so L_i reflects total
    // response latency (identical treatment to PLOSHA's fog_node.cpp).
    state.latency = (max_latency_ms_ > 0)
        ? std::min(1.0, (processing_latency_ms_ + comm_latency_ms_) / max_latency_ms_)
        : 0.0;
    state.reliability = reliability_;
    state.perf_coeff = perf_coeff_;
    return state;
}

void FogNode::updateProcessingLatency(double latency_ms) {
    processing_latency_ms_ = latency_ms;
}

void FogNode::updateReliability(double new_rel) {
    reliability_ = std::max(0.0, std::min(1.0, new_rel));
}

void FogNode::updatePerfCoefficient(double coeff) {
    perf_coeff_ = std::max(PERF_COEFF_MIN, std::min(PERF_COEFF_MAX, coeff));
}

void FogNode::saveCheckpoint(const Checkpoint& ckpt) {
    checkpoint_ = ckpt;
}

int FogNode::currentQueueSize() const {
    std::lock_guard<std::mutex> lock(*queue_mutex_);
    return static_cast<int>(reading_queue_.size());
}

} // namespace ftworkflow
