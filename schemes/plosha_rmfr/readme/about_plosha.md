# How to Run PLOSHA-RMFR Experiments

This document explains how to compile and run the PLOSHA-RMFR Discrete-Event Simulation (DES) benchmark. 

All commands must be executed from within the `src/` directory.

## 1. Bare-Metal Execution (Fastest for debugging)
Use this to quickly test the code logic without the Intel SGX/Gramine LibOS overhead.

```bash
cd /run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src
make clean && make

# Run a single experiment (e.g., Exp 1) with 1 epoch for quick testing
./plosha_rmfr --experiment 1 --sensors 1000 --epochs 1 \
  --dataset ../../../dataset/plosha_dataset.csv --output ../

# Run ALL 7 experiments with the full 10 epochs required by the paper
./plosha_rmfr --experiment all --epochs 10 \
  --dataset ../../../dataset/plosha_dataset.csv --output ../
```

## 2. Gramine TEE Execution (For Paper Results)
Use this to run the simulation inside the **Intel SGX Enclave** using Gramine. This is the execution mode that validates the TEE claims in the paper. The Paillier pre-computation pool ensures that the enclave does not freeze during random number generation.

```bash
cd /run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr/src
make clean && make

# 1. Build the Gramine Manifest right here in the src/ directory
# This generates the correct security hash for the new binary and maps the folders
gramine-manifest --no-check \
  -Dlog_level=error \
  -Ddataset_dir="/run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR(cpp)/dataset" \
  -Doutput_dir="/run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR(cpp)/schemes/plosha_rmfr" \
  plosha_rmfr.manifest.template plosha_rmfr.manifest

# 2. Run the enclave (Single experiment, quick test)
gramine-direct ./plosha_rmfr --experiment 1 --sensors 1000 --epochs 1 \
  --dataset /dataset/plosha_dataset.csv --output /output

# 3. Run the enclave (ALL 7 experiments, 10 epochs for final paper)
gramine-direct ./plosha_rmfr --experiment all --epochs 10 \
  --dataset /dataset/plosha_dataset.csv --output /output
```

## Where do the results go?
The simulation automatically routes the output to the correct experiment folder (e.g., `schemes/plosha_rmfr/exp1_sensor_scalability/results.csv`) provided the `--output` argument points to the root of the `plosha_rmfr` directory (which is mapped to `/output` inside Gramine).
