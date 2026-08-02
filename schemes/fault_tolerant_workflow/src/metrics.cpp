#include "metrics.hpp"
#include <numeric>
#include <iomanip>
#include <iostream>

namespace ftworkflow {

void MetricsCollector::recordEpoch(const EpochMetrics& m) {
    epoch_records_.push_back(m);
}

SweepPointResult MetricsCollector::computeAverages(double variable_value) const {
    SweepPointResult result;
    result.variable_value = variable_value;

    if (epoch_records_.empty()) return result;

    double n = static_cast<double>(epoch_records_.size());
    for (const auto& m : epoch_records_) {
        result.avg_aggregation_latency += m.aggregation_latency_ms;
        result.avg_recovery_latency += m.recovery_latency_ms;
        result.avg_aggregation_completeness += m.aggregation_completeness;
        result.avg_loss_exposure += m.loss_exposure_fraction;
        result.avg_communication_overhead += m.communication_overhead_KB;
        result.avg_system_availability += m.system_availability;
        result.avg_queue_utilization += m.queue_utilization;
        result.avg_recovery_frequency += m.recovery_frequency;
        result.avg_scheduling_latency += m.scheduling_latency_ms;
        result.avg_workload_imbalance += m.workload_imbalance;
        result.avg_scheduling_comm_kb += m.scheduling_comm_kb;
    }
    result.avg_aggregation_latency /= n;
    result.avg_recovery_latency /= n;
    result.avg_aggregation_completeness /= n;
    result.avg_loss_exposure /= n;
    result.avg_communication_overhead /= n;
    result.avg_system_availability /= n;
    result.avg_queue_utilization /= n;
    result.avg_recovery_frequency /= n;
    result.avg_scheduling_latency /= n;
    result.avg_workload_imbalance /= n;
    result.avg_scheduling_comm_kb /= n;
    return result;
}

void MetricsCollector::reset() {
    epoch_records_.clear();
}

void MetricsCollector::writeCSVHeader(std::ofstream& file, const std::string& variable_name) {
    file << variable_name
         << ",aggregation_latency_ms"
         << ",recovery_latency_ms"
         << ",aggregation_completeness"
         << ",loss_exposure_fraction"
         << ",communication_overhead_KB"
         << ",system_availability"
         << ",queue_utilization"
         << ",recovery_frequency\n";
}

void MetricsCollector::writeCSVRow(std::ofstream& file, const SweepPointResult& r) {
    file << std::fixed << std::setprecision(6)
         << r.variable_value
         << "," << r.avg_aggregation_latency
         << "," << r.avg_recovery_latency
         << "," << r.avg_aggregation_completeness
         << "," << r.avg_loss_exposure
         << "," << r.avg_communication_overhead
         << "," << r.avg_system_availability
         << "," << r.avg_queue_utilization
         << "," << r.avg_recovery_frequency
         << "\n";
}

void MetricsCollector::writeResultsFile(const std::string& filepath,
                                         const std::string& variable_name,
                                         const std::vector<SweepPointResult>& results) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[FTWorkflow-Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    writeCSVHeader(file, variable_name);
    for (const auto& r : results) writeCSVRow(file, r);
    file.close();
    std::cout << "[FTWorkflow-Metrics] Wrote " << results.size() << " rows to " << filepath << "\n";
}

} // namespace ftworkflow
