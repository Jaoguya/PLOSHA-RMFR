#ifndef PLOSHA_METRICS_HPP
#define PLOSHA_METRICS_HPP

#include <string>
#include <vector>
#include <fstream>
#include <utility>

namespace plosha {

// One row of metrics collected per epoch
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
    double state_refresh_ms = 0.0;
    double workload_imbalance = 0.0;
    double processing_overhead_ms = 0.0;
    double energy_joules = 0.0;
    double scheduling_comm_kb = 0.0;
    double recomputation_overhead_ms = 0.0;
    double reused_microslot_count = 0.0;
};

// Averaged metrics written as one CSV row per sweep point
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
    double std_scheduling_latency = 0.0;
    double avg_state_refresh_ms = 0.0;
    double std_state_refresh_ms = 0.0;
    double avg_workload_imbalance = 0.0;
    double std_workload_imbalance = 0.0;
    double avg_processing_overhead = 0.0;
    double std_processing_overhead = 0.0;
    double avg_energy_joules = 0.0;
    double std_energy_joules = 0.0;
    double avg_scheduling_comm_kb = 0.0;
    double convergence_time_epochs = 0.0;
    // Exp1 (ablation) headline metrics. Kept here rather than computed at the
    // call site so that every experiment derives its mean and dispersion from
    // the same implementation.
    double std_aggregation_latency = 0.0;
    double std_loss_exposure = 0.0;
    double avg_recomputation_overhead = 0.0;
    double std_recomputation_overhead = 0.0;
    double avg_reused_microslot_count = 0.0;
    double std_reused_microslot_count = 0.0;
};

class MetricsCollector {
public:
    // Record one epoch's metrics
    void recordEpoch(const EpochMetrics& m);

    // Compute averages across all recorded epochs
    SweepPointResult computeAverages(double variable_value) const;

    // Reset for next sweep point
    void reset();

    // Write results.csv header
    static void writeCSVHeader(std::ofstream& file, const std::string& variable_name);

    // Write one row to results.csv
    static void writeCSVRow(std::ofstream& file, const SweepPointResult& result);

    // Convenience: write full results file for an experiment
    static void writeResultsFile(const std::string& filepath,
                                  const std::string& variable_name,
                                  const std::vector<SweepPointResult>& results);

    // Get raw records for variance computation
    const std::vector<EpochMetrics>& getRecords() const { return epoch_records_; }

    // Audit trail: dump every recorded epoch as one CSV row, so the mean/std
    // reported in results.csv can be checked against the underlying
    // distribution. Called per sweep point, before reset() destroys the data.
    void writeEpochRecordsFile(const std::string& filepath) const;

    // Provenance: write a run_metadata.txt of key:value lines next to a
    // results.csv, recording what produced it (commit, host, native vs SGX,
    // effective parameters). Entries are ordered as given.
    static void writeRunMetadata(const std::string& dir,
                                 const std::vector<std::pair<std::string, std::string>>& entries);

    // Write ablation results (variant column format)
    struct AblationRow {
        std::string variant;
        double num_sensors;
        double aggregation_latency_ms;
        double std_aggregation_latency;
        double processing_overhead_ms;
        double std_processing_overhead;
        double loss_exposure_fraction;
        double std_loss_exposure;
        double energy_joules;
        double std_energy_joules;
        double recomputation_overhead_ms;
        double std_recomputation_overhead;
        double reused_microslot_count;
        double std_reused_microslot_count;
    };
    static void writeAblationResultsFile(const std::string& filepath,
                                          const std::vector<AblationRow>& rows);

    // Write scheduling results (2-metric format)
    static void writeSchedulingResultsFile(const std::string& filepath,
                                            const std::string& variable_name,
                                            const std::vector<SweepPointResult>& results);

private:
    std::vector<EpochMetrics> epoch_records_;
};

} // namespace plosha

#endif // PLOSHA_METRICS_HPP
