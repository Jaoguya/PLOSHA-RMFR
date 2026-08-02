#include "ft_serverless_edge.hpp"
#include <iostream>
#include <cstring>
#include <cstdio>

int main(int argc, char* argv[]) {
    SimConfig cfg;
    cfg.dataset_path = "../../../dataset/plosha_dataset.csv";
    cfg.experiment = 1;
    cfg.variable_value = 1000;
    cfg.num_cloudlets = 10;
    cfg.num_sensors = 1000;
    cfg.failure_rate = 0.0;
    cfg.num_micro_slots = 1;
    cfg.reporting_rate_mult = 1.0;
    cfg.seed = 12345;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--experiment") && i+1 < argc) cfg.experiment = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--variable") && i+1 < argc) cfg.variable_value = atof(argv[++i]);
        else if (!strcmp(argv[i], "--cloudlets") && i+1 < argc) cfg.num_cloudlets = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sensors") && i+1 < argc) cfg.num_sensors = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i+1 < argc) cfg.seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dataset") && i+1 < argc) cfg.dataset_path = argv[++i];
        else if (!strcmp(argv[i], "--heterogeneity") && i+1 < argc) cfg.heterogeneity_ratio = atof(argv[++i]);
    }

    FTServerlessEdgeSim sim(cfg);
    SimResult res = sim.run();
    // Output CSV line (no header)
    printf("%g,%.6f", res.variable_value, res.primary_metric);
    if (res.secondary_1 != 0 || res.secondary_2 != 0)
        printf(",%.6f,%.6f", res.secondary_1, res.secondary_2);
    else
        printf(",,");
    printf("\n");
    return 0;
}
