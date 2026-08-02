#include "fog_node.hpp"
#include <algorithm>
#include <cstddef>

namespace plosha {

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

void FogNode::removeSensor(int sensor_id) {
    auto it = std::find(assigned_sensors_.begin(), assigned_sensors_.end(), sensor_id);
    if (it != assigned_sensors_.end()) {
        assigned_sensors_.erase(it);
    }
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

FogState FogNode::getState() const {
    FogState state;

    int current_weight = 0;
    std::size_t current_backlog = 0;
    {
        std::lock_guard<std::mutex> lock(*queue_mutex_);
        current_weight = total_queue_weight_;
        current_backlog = reading_queue_.size();
    }

    // Max capacity approx: 65535 (max quantized val) * queue_capacity_
    double max_weight_capacity = 65535.0 * queue_capacity_;

    // R5 FIX: W_i = queue_weight / max_weight_capacity, normalized to [0,1]
    state.workload = (max_weight_capacity > 0)
        ? std::min(1.0, static_cast<double>(current_weight) / max_weight_capacity)
        : 0.0;

    // R10 FIX: Q_i previously copied W_i verbatim (state.queue_load =
    // state.workload), collapsing two of the three dimensions of the
    // capacity model (paper Eq. 8: Cap_i uses omega_w*W_i + omega_q*Q_i +
    // omega_l*L_i as independent terms) into one. Q_i is now derived from
    // actual queue occupancy (item count vs. queue_capacity_) rather than
    // the value-weighted workload signal, so the two can diverge -- e.g. a
    // backlog of many low-value readings (high Q_i, low W_i) or a few
    // heavy readings (low Q_i, high W_i), and under the burst multiplier in
    // Exp2 where arrival rate outpaces drain rate independently of reading
    // size.
    state.queue_load = (queue_capacity_ > 0)
        ? std::min(1.0, static_cast<double>(current_backlog) /
                            static_cast<double>(queue_capacity_))
        : 0.0;

    // R6 FIX: L_i = processing_latency / max_expected_latency, normalized to [0,1]
    // Communication latency is added to processing latency so L_i reflects the
    // node's TOTAL response latency. comm_latency_ms_ is 0 unless the
    // experiment models heterogeneity, so this is a no-op elsewhere.
    state.latency = (max_latency_ms_ > 0)
        ? std::min(1.0, (processing_latency_ms_ + comm_latency_ms_) / max_latency_ms_)
        : 0.0;

    state.reliability = reliability_;
    return state;
}

void FogNode::updateProcessingLatency(double latency_ms) {
    processing_latency_ms_ = latency_ms;
}

void FogNode::updateReliability(double new_rel) {
    reliability_ = std::max(0.0, std::min(1.0, new_rel));
}

void FogNode::setMicroSlotAggregates(std::vector<PaillierCiphertext>&& aggs) {
    micro_aggregates_ = std::move(aggs);
}

int FogNode::currentQueueSize() const {
    std::lock_guard<std::mutex> lock(*queue_mutex_);
    return static_cast<int>(reading_queue_.size());
}

} // namespace plosha
