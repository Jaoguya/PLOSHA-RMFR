#!/bin/bash
set -e

echo "=========================================="
echo "    PLOSHA-RMFR Benchmark Runner"
echo "=========================================="

ROOT_DIR=$(pwd)
DATASET_PATH="$ROOT_DIR/dataset/plosha_dataset.csv"

# ---------------------------------------------------------
# 1a. PLOSHA-RMFR (Native — for fair comparison with baselines)
# ---------------------------------------------------------
echo "[1/5] Running PLOSHA-RMFR (Ours) — Native mode..."
cd "$ROOT_DIR/schemes/plosha_rmfr/src"
make clean && make
if [ ! -d "$ROOT_DIR/schemes/plosha_rmfr" ]; then
  mkdir -p "$ROOT_DIR/schemes/plosha_rmfr"
fi
./plosha_rmfr --experiment all --epochs 30 --dataset "$DATASET_PATH" --output "$ROOT_DIR/schemes/plosha_rmfr"
echo "✔ PLOSHA-RMFR (Native) completed."
echo ""

# ---------------------------------------------------------
# 1b. PLOSHA-RMFR (Intel SGX — for SGX overhead comparison)
# ---------------------------------------------------------
echo "[1b/5] Running PLOSHA-RMFR (Ours) in Intel SGX..."
# Copy native results to backup before SGX overwrites them
for d in exp1_ablation_aggregation exp2_scheduling_efficiency exp3_failure_rate exp4_loss_exposure exp5_recovery_comm exp6_aflto_ablation; do
  if [ -d "$ROOT_DIR/schemes/plosha_rmfr/$d" ]; then
    mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/${d}_native"
    cp -r "$ROOT_DIR/schemes/plosha_rmfr/$d/"* "$ROOT_DIR/schemes/plosha_rmfr/${d}_native/" 2>/dev/null || true
  fi
done

gramine-manifest --no-check -Dlog_level=error \
  -Ddataset_dir="$ROOT_DIR/dataset" \
  -Doutput_dir="$ROOT_DIR/schemes/plosha_rmfr" \
  plosha_rmfr.manifest.template plosha_rmfr.manifest

# Generate enclave signing key if it does not exist globally
if [ ! -f "$HOME/.config/gramine/enclave-key.pem" ]; then
  echo "  -> Generating SGX enclave private key..."
  gramine-sgx-gen-private-key
fi

# echo "  -> Signing SGX enclave..."
# gramine-sgx-sign --manifest plosha_rmfr.manifest --output plosha_rmfr.manifest.sgx
# 
# echo "  -> Executing via Gramine-SGX (all experiments)..."
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp1_ablation_aggregation"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp2_scheduling_efficiency"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp3_failure_rate"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp3_workload_intensity"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp4_loss_exposure"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp5_recovery_comm"
# mkdir -p "$ROOT_DIR/schemes/plosha_rmfr/exp6_aflto_ablation"
# gramine-sgx plosha_rmfr --experiment all --skip-native-exp8 --epochs 30 --dataset /dataset/plosha_dataset.csv --output /output
# echo "✔ PLOSHA-RMFR (SGX) completed."
# echo ""

# ---------------------------------------------------------
# 2. Fault-Tolerant Workflow (Ref[37])
# ---------------------------------------------------------
echo "[2/5] Running Fault-Tolerant Workflow (Ref[37])..."
cd "$ROOT_DIR/schemes/fault_tolerant_workflow/src"
make clean && make
echo "  -> Executing all experiments..."
./ftworkflow --experiment all --dataset "$DATASET_PATH"
echo "✔ FT-Workflow completed."
echo ""

# ---------------------------------------------------------
# 3. Robust IIoT (Ref[24])
# ---------------------------------------------------------
echo "[3/5] Running Robust IIoT (Ref[24])..."
cd "$ROOT_DIR/schemes/robust_iiot/src"
make clean && make
# echo "  -> Running Exp 1 (Sensor Scalability)..."
# mkdir -p ../exp1_sensor_scalability
# ./exp1_sensor_scalability
# echo "  -> Running Exp 3 (Workload Intensity)..."
# mkdir -p ../exp3_workload_intensity
# ./exp3_workload_intensity
echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
./exp5_loss_exposure
echo "✔ Robust IIoT completed."
echo ""

# ---------------------------------------------------------
# 4. FedDQN (Ref[22])
# ---------------------------------------------------------
echo "[4/5] Running FedDQN (Ref[22])..."
cd "$ROOT_DIR/schemes/fed_dqn/src"
make clean && make
# echo "  -> Running Exp 1 (Sensor Scalability)..."
# mkdir -p ../exp1_sensor_scalability
# ./exp1_sensor_scalability
# echo "  -> Running Exp 2 (Fog Scalability)..."
# mkdir -p ../exp2_fog_scalability
# ./exp2_fog_scalability
# echo "  -> Running Exp 3 (Workload Intensity)..."
# mkdir -p ../exp3_workload_intensity
# ./exp3_workload_intensity
echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
./exp4_failure_rate
echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
./exp9_scheduling_efficiency
echo "✔ FedDQN completed."
echo ""

# ---------------------------------------------------------
# 5. FT-Serverless Edge (Ref[38]) — 10 seeds per point
# ---------------------------------------------------------
echo "[5/5] Running FT-Serverless Edge (Ref[38])..."
cd "$ROOT_DIR/schemes/ft_serverless_edge/src"
make clean && make

NUM_SEEDS=10
BASE_SEED=12345

# Helper: run an experiment point NUM_SEEDS times and output the averaged CSV line
run_avg() {
  local exp=$1 var=$2; shift 2
  local sum1=0 sum2=0 sum3=0
  for s in $(seq $BASE_SEED $((BASE_SEED + NUM_SEEDS - 1))); do
    local line
    line=$(./ft_serverless_sim --experiment $exp --variable $var --seed $s "$@" --dataset "$DATASET_PATH")
    local m1 m2 m3
    m1=$(echo "$line" | awk -F',' '{print $2}')
    m2=$(echo "$line" | awk -F',' '{print $3}')
    m3=$(echo "$line" | awk -F',' '{print $4}')
    sum1=$(echo "$sum1 + ${m1:-0}" | bc -l)
    sum2=$(echo "$sum2 + ${m2:-0}" | bc -l)
    sum3=$(echo "$sum3 + ${m3:-0}" | bc -l)
  done
  printf "%s,%.6f,%.6f,%.6f\n" "$var" \
    "$(echo "$sum1 / $NUM_SEEDS" | bc -l)" \
    "$(echo "$sum2 / $NUM_SEEDS" | bc -l)" \
    "$(echo "$sum3 / $NUM_SEEDS" | bc -l)"
}

# echo "  -> Running Exp 1 (Sensor Scalability)..."
# mkdir -p ../exp1_sensor_scalability
# echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp1_sensor_scalability/results.csv
# for v in 500 1000 1500 2000 2500 3000 3500 4000 4500 5000; do
#   run_avg 1 $v --sensors $v >> ../exp1_sensor_scalability/results.csv
# done

# echo "  -> Running Exp 2 (Fog Scalability)..."
# mkdir -p ../exp2_fog_scalability
# echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp2_fog_scalability/results.csv
# for v in 5 10 15 20 25 30 35 40 45 50; do
#   run_avg 2 $v --cloudlets $v >> ../exp2_fog_scalability/results.csv
# done

# echo "  -> Running Exp 3 (Workload Intensity)..."
# mkdir -p ../exp3_workload_intensity
# echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp3_workload_intensity/results.csv
# for v in 1 2 3 4 5 6 7 8 9 10; do
#   run_avg 3 $v >> ../exp3_workload_intensity/results.csv
# done

echo "  -> Running Exp 3 (Failure Rate)..."
mkdir -p ../exp3_failure_rate
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp3_failure_rate/results.csv
for v in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.25 0.30 0.35; do
  run_avg 4 $v >> ../exp3_failure_rate/results.csv
done

echo "  -> Running Exp 4 (Loss Exposure)..."
mkdir -p ../exp4_loss_exposure
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp4_loss_exposure/results.csv
for v in 1 2 3 4 5 6 7 8 9 10 12 14 16 18 20; do
  run_avg 5 $v >> ../exp4_loss_exposure/results.csv
done

echo "  -> Running Exp 5 (Recovery Communication)..."
mkdir -p ../exp5_recovery_comm
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > ../exp5_recovery_comm/results.csv
for v in 1 2 3 4 5 6 7 8 9 10; do
  run_avg 6 $v >> ../exp5_recovery_comm/results.csv
done

echo "  -> Running Exp 2 (Scheduling Efficiency)..."
mkdir -p ../exp2_scheduling_efficiency
echo "num_fog_nodes,scheduling_latency_ms,workload_imbalance" > ../exp2_scheduling_efficiency/results.csv
for v in 5 10 15 20 25 30 35 40 45 50; do
  line=$(./ft_serverless_sim --experiment 9 --variable $v --cloudlets $v --sensors 12600 --seed $BASE_SEED --dataset "$DATASET_PATH")
  # primary_metric = scheduling_latency_ms, secondary_1 = workload_imbalance
  sched_lat=$(echo "$line" | awk -F',' '{print $2}')
  imbalance=$(echo "$line" | awk -F',' '{print $3}')
  printf "%s,%.6f,%.6f\n" "$v" "${sched_lat:-0}" "${imbalance:-0}" >> ../exp2_scheduling_efficiency/results.csv
done
echo "✔ FT-Serverless Edge completed."
echo ""

# ---------------------------------------------------------
# 6. Generate Plots
# ---------------------------------------------------------
echo "[6/6] Generating Comparison Plots..."
cd "$ROOT_DIR/plots"
python3 generate_plots.py
echo "✔ Plots generated in plots/output/"
echo ""
echo "=========================================="
echo "    Benchmark run fully complete!"
echo "=========================================="
