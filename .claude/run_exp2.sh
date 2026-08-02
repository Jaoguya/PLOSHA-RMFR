#!/bin/bash
# Experiment 2 -- scheduling efficiency. Native run, no SGX required.
# Per README Experiment Definitions #2, this is a FOUR-scheme comparison:
# PLOSHA-RMFR + FedDQN (Ref[22]) + FT-Workflow (Ref[37]) + FT-Serverless
# (Ref[38]). The PLOSHA stage runs through run_exp_common.sh (preflight,
# provenance, epoch records, verified output); the three baselines run as
# extra_stages() below so everything lands in one log.
#
# NOTE: the binary's flag is --experiment 9, not 2. The numbering is offset;
# runExp9_SchedulingEfficiency sweeps fog nodes {5..50} and writes to
# exp2_scheduling_efficiency/.
#
#   ./run_exp2.sh                 # full 4-scheme run
#   EPOCHS=5 ./run_exp2.sh        # EPOCHS is accepted but Exp9 keeps its
#                                 # fixed 30-epoch schedule by design (burst
#                                 # at epoch 12, degradation at epoch 21)
#   REBUILD=1 ./run_exp2.sh       # force make clean first (PLOSHA stage)
#   DEBUG=1 ./run_exp2.sh         # trace every command

EXP_FLAG=9
EXP_NAME="Experiment 2 (Scheduling Efficiency)"
OUT_SUBDIR="exp2_scheduling_efficiency"

# Baseline schemes (README: Exp2 compares 4 schemes). Invocations are kept
# identical to the original run_exp2.sh; only path handling changed (absolute
# paths from ROOT_DIR so the script works from any cwd).
extra_stages() {
  echo ""
  echo "-- baseline: Fault-Tolerant Workflow (Ref[37]) --"
  cd "$ROOT_DIR/schemes/fault_tolerant_workflow/src"
  make clean && make
  mkdir -p ../exp2_scheduling_efficiency
  ./ftworkflow --experiment 9 --dataset "$DATASET_PATH"

  echo ""
  echo "-- baseline: FedDQN (Ref[22]) --"
  cd "$ROOT_DIR/schemes/fed_dqn/src"
  make clean && make
  mkdir -p ../exp2_scheduling_efficiency
  ./exp9_scheduling_efficiency

  echo ""
  echo "-- baseline: FT-Serverless Edge (Ref[38]) --"
  cd "$ROOT_DIR/schemes/ft_serverless_edge/src"
  make clean && make
  local BASE_SEED=12345
  mkdir -p ../exp2_scheduling_efficiency
  echo "num_fog_nodes,scheduling_latency_ms,workload_imbalance" > ../exp2_scheduling_efficiency/results.csv
  local v line sched_lat imbalance sensors
  for v in 5 10 15 20 25 30 35 40 45 50; do
    # FAIRNESS: match the other three schemes, which all use num_sensors =
    # fog_nodes * 100 in Exp9 (PLOSHA des_engine.cpp, FedDQN exp9_main.cpp,
    # FT-Workflow ft_engine.cpp). Previously this was hardcoded --sensors 12600,
    # 2.5-25x the other schemes' workload and not scaling with fog count. Now
    # cloudlets == fog nodes == v, and sensors == v*100 to keep 100 sensors per
    # cloudlet, identical to the others. (Also cuts runtime ~4.5x.)
    sensors=$(( v * 100 ))
    line=$(./ft_serverless_sim --experiment 9 --variable "$v" --cloudlets "$v" --sensors "$sensors" --seed "$BASE_SEED" --dataset "$DATASET_PATH")
    sched_lat=$(echo "$line" | awk -F',' '{print $2}')
    imbalance=$(echo "$line" | awk -F',' '{print $3}')
    printf "%s,%.6f,%.6f\n" "$v" "${sched_lat:-0}" "${imbalance:-0}" >> ../exp2_scheduling_efficiency/results.csv
  done

  echo ""
  echo "-- baseline verify --"
  local d rows csv
  for d in fault_tolerant_workflow fed_dqn ft_serverless_edge; do
    csv="$ROOT_DIR/schemes/$d/exp2_scheduling_efficiency/results.csv"
    [ -f "$csv" ] || fail "no results.csv produced for baseline $d"
    rows=$(($(wc -l < "$csv") - 1))
    [ "$rows" -ge 1 ] || fail "baseline $d results.csv has no data rows"
    echo "   $d: $rows data rows"
  done
}

source "$(dirname "${BASH_SOURCE[0]}")/run_exp_common.sh"
run_experiment
