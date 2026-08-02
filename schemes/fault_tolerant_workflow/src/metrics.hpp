#ifndef FTWORKFLOW_METRICS_HPP
#define FTWORKFLOW_METRICS_HPP

#include <string>
#include <vector>
#include <fstream>

namespace ftworkflow {

struct EpochMetrics {
    double aggregation_latency_ms = 0.0;
    double recovery_latency_ms = 0.0;
    double aggregation_completeness = 0.0;
    double loss_exposure_fraction = 0.0;
    double communication_overhead_KB = 0.0;
    double system_availability = 0.0;
    double queue_utilization = 0.0;
    int recovery_frequency = 0;
    double scheduling_latency_ms = 0.0;
    double workload_imbalance = 0.0;
    double scheduling_comm_kb = 0.0;
};

struct SweepPointResult {
    double variable_value = 0.0;
    double avg_aggregation_latency = 0.0;
    double avg_recovery_latency = 0.0;
    double avg_aggregation_completeness = 0.0;
    double avg_loss_exposure = 0.0;
    double avg_communication_overhead = 0.0;
    double avg_system_availability = 0.0;
    double avg_queue_utilization = 0.0;
    double avg_recovery_frequency = 0.0;
    double avg_scheduling_latency = 0.0;
    double avg_workload_imbalance = 0.0;
    double avg_scheduling_comm_kb = 0.0;
    double convergence_time_epochs = 0.0;
};

class MetricsCollector {
public:
    void recordEpoch(const EpochMetrics& m);
    SweepPointResult computeAverages(double variable_value) const;
    void reset();

    static void writeCSVHeader(std::ofstream& file, const std::string& variable_name);
    static void writeCSVRow(std::ofstream& file, const SweepPointResult& result);
    static void writeResultsFile(const std::string& filepath,
                                  const std::string& variable_name,
                                  const std::vector<SweepPointResult>& results);

private:
    std::vector<EpochMetrics> epoch_records_;
};

} // namespace ftworkflow

#endif // FTWORKFLOW_METRICS_HPP
