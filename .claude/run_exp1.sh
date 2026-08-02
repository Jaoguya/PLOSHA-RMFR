#!/bin/bash
# Experiment 1 -- aggregation ablation. Native run, no SGX required.
# PLOSHA-only internal ablation (see README Experiment Definitions #1).
#
# NOTE: the binary's flag is --experiment 8, not 1. The numbering is offset;
# runExp8_AblationAggregation writes to exp1_ablation_aggregation/.
#
#   ./run_exp1.sh                      # 30 epochs, incremental build
#   EPOCHS=2 ./run_exp1.sh             # short debug run
#   FAILURE_RATE=0.10 ./run_exp1.sh    # sensitivity analysis
#   REBUILD=1 ./run_exp1.sh            # force make clean first
#   DEBUG=1 ./run_exp1.sh              # trace every command
#
# Without FAILURE_RATE the experiment uses EXP1_ABLATION_FAILURE_RATE (0.50,
# see config.hpp). That is outside the 0.02-0.35 range swept by the
# failure-rate experiment and must be stated wherever these results appear.

EXP_FLAG=8
EXP_NAME="Experiment 1 (Aggregation Ablation)"
OUT_SUBDIR="exp1_ablation_aggregation"

source "$(dirname "${BASH_SOURCE[0]}")/run_exp_common.sh"
run_experiment
