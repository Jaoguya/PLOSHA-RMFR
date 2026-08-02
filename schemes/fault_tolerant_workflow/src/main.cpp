#include "ft_engine.hpp"
#include "config.hpp"
#include <iostream>
#include <string>

using namespace ftworkflow;

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --experiment <1-6|9|all> [options]\n"
              << "\nOptions:\n"
              << "  --experiment <N|all>   Experiment number (1-6, 9) or 'all'\n"
              << "  --sensors <N>          Number of sensors (default: 1000)\n"
              << "  --fog-nodes <N>        Number of fog nodes (default: 10)\n"
              << "  --epochs <N>           Number of epochs (default: 10)\n"
              << "  --failure-rate <F>     Failure rate 0.0-1.0 (default: 0.05)\n"
              << "  --dataset <PATH>       Path to plosha_dataset.csv\n"
              << "  --output <DIR>         Output directory (default: ..)\n"
              << "  --help                 Show this help\n";
}

int main(int argc, char* argv[]) {
    ExperimentConfig config;
    config.output_dir = "..";
    bool run_all = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--experiment" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "all") {
                run_all = true;
            } else {
                config.experiment_id = std::stoi(val);
            }
        } else if (arg == "--sensors" && i + 1 < argc) {
            config.num_sensors = std::stoi(argv[++i]);
        } else if (arg == "--fog-nodes" && i + 1 < argc) {
            config.num_fog_nodes = std::stoi(argv[++i]);
        } else if (arg == "--epochs" && i + 1 < argc) {
            config.num_epochs = std::stoi(argv[++i]);
        } else if (arg == "--failure-rate" && i + 1 < argc) {
            config.failure_rate = std::stod(argv[++i]);
        } else if (arg == "--dataset" && i + 1 < argc) {
            config.dataset_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_dir = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (!run_all && config.experiment_id == 0) {
        std::cerr << "Error: --experiment is required\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "========================================\n"
              << "  Fault-Tolerant Workflow (Ref[37]) DES\n"
              << "========================================\n"
              << "  Sensors:       " << config.num_sensors << "\n"
              << "  Fog Nodes:     " << config.num_fog_nodes << "\n"
              << "  Epochs:        " << config.num_epochs << "\n"
              << "  Failure Rate:  " << config.failure_rate << "\n"
              << "  Dataset:       " << config.dataset_path << "\n"
              << "  Output Dir:    " << config.output_dir << "\n"
              << "========================================\n\n";

    FTEngine engine;
    if (run_all) {
        engine.runAll(config);
    } else {
        engine.runExperiment(config);
    }

    return 0;
}
