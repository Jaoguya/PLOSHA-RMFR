#include "metrics.hpp"
#include <numeric>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <cstdlib>

namespace plosha {

void MetricsCollector::recordEpoch(const EpochMetrics& m) {
    epoch_records_.push_back(m);
}

namespace {
// Same directory-creation idiom the existing writers use (POSIX mkdir -p).
void ensureParentDir(const std::string& filepath) {
    auto slash = filepath.rfind('/');
    if (slash != std::string::npos) {
        std::string cmd = "mkdir -p " + filepath.substr(0, slash);
        (void)system(cmd.c_str());
    }
}
} // namespace

void MetricsCollector::writeEpochRecordsFile(const std::string& filepath) const {
    ensureParentDir(filepath);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    file << "epoch,aggregation_latency_ms,recovery_latency_ms,"
            "aggregation_completeness,loss_exposure_fraction,"
            "communication_overhead_KB,system_availability,queue_utilization,"
            "recovery_frequency,scheduling_latency_ms,state_refresh_ms,"
            "workload_imbalance,processing_overhead_ms,energy_joules,"
            "scheduling_comm_kb,recomputation_overhead_ms,reused_microslot_count\n";
    file << std::fixed << std::setprecision(6);
    for (size_t i = 0; i < epoch_records_.size(); ++i) {
        const auto& m = epoch_records_[i];
        file << i
             << "," << m.aggregation_latency_ms
             << "," << m.recovery_latency_ms
             << "," << m.aggregation_completeness
             << "," << m.loss_exposure_fraction
             << "," << m.communication_overhead_KB
             << "," << m.system_availability
             << "," << m.queue_utilization
             << "," << m.recovery_frequency
             << "," << m.scheduling_latency_ms
             << "," << m.state_refresh_ms
             << "," << m.workload_imbalance
             << "," << m.processing_overhead_ms
             << "," << m.energy_joules
             << "," << m.scheduling_comm_kb
             << "," << m.recomputation_overhead_ms
             << "," << m.reused_microslot_count
             << "\n";
    }
}

void MetricsCollector::writeRunMetadata(
    const std::string& dir,
    const std::vector<std::pair<std::string, std::string>>& entries) {
    std::string filepath = dir + "/run_metadata.txt";
    ensureParentDir(filepath);
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    for (const auto& e : entries) {
        file << e.first << ": " << e.second << "\n";
    }
    std::cout << "[Metrics] Wrote provenance to " << filepath << "\n";
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
        result.avg_state_refresh_ms += m.state_refresh_ms;
        result.avg_workload_imbalance += m.workload_imbalance;
        result.avg_processing_overhead += m.processing_overhead_ms;
        result.avg_energy_joules += m.energy_joules;
        result.avg_scheduling_comm_kb += m.scheduling_comm_kb;
        result.avg_recomputation_overhead += m.recomputation_overhead_ms;
        result.avg_reused_microslot_count += m.reused_microslot_count;
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
    result.avg_state_refresh_ms /= n;
    result.avg_workload_imbalance /= n;
    result.avg_processing_overhead /= n;
    result.avg_energy_joules /= n;
    result.avg_scheduling_comm_kb /= n;
    result.avg_recomputation_overhead /= n;
    result.avg_reused_microslot_count /= n;

    // R7 FIX: dispersion (population std) for the three Exp2 headline metrics,
    // so scheduling-efficiency comparisons can be reported with error bars
    // instead of bare means. Two-pass (mean already known above).
    // Extended to the Exp1 (ablation) headline metrics as well: every reported
    // figure now derives its centre and dispersion from this one estimator, so
    // no experiment summarises its epochs differently from the others.
    if (n > 1.0) {
        double var_sched = 0.0, var_refresh = 0.0, var_imbalance = 0.0;
        double var_lat = 0.0, var_overhead = 0.0, var_loss = 0.0,
               var_energy = 0.0, var_recomp = 0.0, var_reused = 0.0;
        for (const auto& m : epoch_records_) {
            double d_sched = m.scheduling_latency_ms - result.avg_scheduling_latency;
            double d_refresh = m.state_refresh_ms - result.avg_state_refresh_ms;
            double d_imb = m.workload_imbalance - result.avg_workload_imbalance;
            var_sched += d_sched * d_sched;
            var_refresh += d_refresh * d_refresh;
            var_imbalance += d_imb * d_imb;

            double d_lat = m.aggregation_latency_ms - result.avg_aggregation_latency;
            double d_over = m.processing_overhead_ms - result.avg_processing_overhead;
            double d_loss = m.loss_exposure_fraction - result.avg_loss_exposure;
            double d_energy = m.energy_joules - result.avg_energy_joules;
            double d_recomp = m.recomputation_overhead_ms - result.avg_recomputation_overhead;
            double d_reused = m.reused_microslot_count - result.avg_reused_microslot_count;
            var_lat += d_lat * d_lat;
            var_overhead += d_over * d_over;
            var_loss += d_loss * d_loss;
            var_energy += d_energy * d_energy;
            var_recomp += d_recomp * d_recomp;
            var_reused += d_reused * d_reused;
        }
        result.std_scheduling_latency = std::sqrt(var_sched / n);
        result.std_state_refresh_ms = std::sqrt(var_refresh / n);
        result.std_workload_imbalance = std::sqrt(var_imbalance / n);
        result.std_aggregation_latency = std::sqrt(var_lat / n);
        result.std_processing_overhead = std::sqrt(var_overhead / n);
        result.std_loss_exposure = std::sqrt(var_loss / n);
        result.std_energy_joules = std::sqrt(var_energy / n);
        result.std_recomputation_overhead = std::sqrt(var_recomp / n);
        result.std_reused_microslot_count = std::sqrt(var_reused / n);
    }
    return result;
}

void MetricsCollector::reset() {
    epoch_records_.clear();
}

void MetricsCollector::writeCSVHeader(std::ofstream& file,
                                       const std::string& variable_name) {
    file << variable_name
         << ",aggregation_latency_ms"
         << ",recovery_latency_ms"
         << ",aggregation_completeness"
         << ",loss_exposure_fraction"
         << ",communication_overhead_KB"
         << ",system_availability"
         << ",queue_utilization"
         << ",recovery_frequency"
         << ",energy_joules"
         << ",scheduling_comm_kb"
         << ",convergence_time_epochs"
         << "\n";
}

void MetricsCollector::writeCSVRow(std::ofstream& file,
                                    const SweepPointResult& r) {
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
         << "," << r.avg_energy_joules
         << "," << r.avg_scheduling_comm_kb
         << "," << r.convergence_time_epochs
         << "\n";
}

void MetricsCollector::writeResultsFile(const std::string& filepath,
                                         const std::string& variable_name,
                                         const std::vector<SweepPointResult>& results) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    writeCSVHeader(file, variable_name);
    for (const auto& r : results) {
        writeCSVRow(file, r);
    }
    file.close();
    std::cout << "[Metrics] Wrote " << results.size() << " rows to " << filepath << "\n";
}

void MetricsCollector::writeAblationResultsFile(const std::string& filepath,
                                                 const std::vector<AblationRow>& rows) {
    // Ensure directory exists
    auto slash = filepath.rfind('/');
    if (slash != std::string::npos) {
        std::string dir = filepath.substr(0, slash);
        // Create directory (POSIX)
        std::string cmd = "mkdir -p " + dir;
        (void)system(cmd.c_str());
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    // R13 FIX: recomputation_overhead_ms and reused_microslot_count are the
    // two metrics the paper's Experiment 1 text promises ("recomputation
    // overhead, and the number of reused completed micro-slot aggregates")
    // but the collector previously never emitted. Both are read directly
    // from the existing MicroRecovery/D_i^miss-D_i^valid bookkeeping in
    // runEpoch, not newly fabricated.
    file << "variant,num_sensors,aggregation_latency_ms,std_aggregation_latency,processing_overhead_ms,std_processing_overhead,loss_exposure_fraction,std_loss_exposure,energy_joules,std_energy_joules,recomputation_overhead_ms,std_recomputation_overhead,reused_microslot_count,std_reused_microslot_count\n";
    for (const auto& r : rows) {
        file << r.variant
             << "," << std::fixed << std::setprecision(6) << r.num_sensors
             << "," << r.aggregation_latency_ms
             << "," << r.std_aggregation_latency
             << "," << r.processing_overhead_ms
             << "," << r.std_processing_overhead
             << "," << r.loss_exposure_fraction
             << "," << r.std_loss_exposure
             << "," << r.energy_joules
             << "," << r.std_energy_joules
             << "," << r.recomputation_overhead_ms
             << "," << r.std_recomputation_overhead
             << "," << r.reused_microslot_count
             << "," << r.std_reused_microslot_count
             << "\n";
    }
    file.close();
    std::cout << "[Metrics] Wrote " << rows.size() << " ablation rows to " << filepath << "\n";
}

void MetricsCollector::writeSchedulingResultsFile(const std::string& filepath,
                                                   const std::string& variable_name,
                                                   const std::vector<SweepPointResult>& results) {
    auto slash = filepath.rfind('/');
    if (slash != std::string::npos) {
        std::string dir = filepath.substr(0, slash);
        std::string cmd = "mkdir -p " + dir;
        (void)system(cmd.c_str());
    }

    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[Metrics] ERROR: Cannot open " << filepath << "\n";
        return;
    }
    file << variable_name
         << ",scheduling_latency_ms,std_scheduling_latency_ms"
         << ",state_refresh_ms,std_state_refresh_ms"
         << ",workload_imbalance,std_workload_imbalance"
         << ",scheduling_comm_kb,convergence_time_epochs\n";
    for (const auto& r : results) {
        file << std::fixed << std::setprecision(6)
             << r.variable_value
             << "," << r.avg_scheduling_latency
             << "," << r.std_scheduling_latency
             << "," << r.avg_state_refresh_ms
             << "," << r.std_state_refresh_ms
             << "," << r.avg_workload_imbalance
             << "," << r.std_workload_imbalance
             << "," << r.avg_scheduling_comm_kb
             << "," << r.convergence_time_epochs
             << "\n";
    }
    file.close();
    std::cout << "[Metrics] Wrote " << results.size() << " scheduling rows to " << filepath << "\n";
}

} // namespace plosha
