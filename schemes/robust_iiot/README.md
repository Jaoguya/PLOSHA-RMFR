# Robust IIoT (PPDA) — Ref[24] Baseline Scheme

**Paper**: Shang et al., "A Robust Privacy-Preserving Data Aggregation Scheme for Edge-Supported IIoT," IEEE Transactions on Industrial Informatics, Vol. 20, No. 3, March 2024.

## Overview

This folder implements a **discrete-event simulation** of the PPDA (Privacy-Preserving Data Aggregation) scheme described in Ref[24]. The simulation faithfully reproduces the full cryptographic pipeline using **real Paillier encryption** and **real Modified ECDSA signatures** — no operations are mocked or fabricated.

## Architecture

```
Sensors (Ss)  →  Edge Servers (ESs)  →  Cloud Center (CC)
   │                    │                       │
   ├─ Paillier Encrypt  ├─ Batch Verify ECDSA   ├─ Batch Verify ECDSA
   ├─ ECDSA Sign        ├─ Homomorphic Aggregate ├─ Paillier Decrypt
   └─ Send to ES        ├─ Add Laplace Noise     └─ Output aggregated sum
                        ├─ ECDSA Sign
                        └─ Send to CC
```

## What Is Real

| Component | Implementation | Library |
|-----------|---------------|---------|
| Paillier Encryption | `g^m · r^N mod N²` with real 1024-bit primes | OpenSSL BIGNUM |
| Paillier Decryption | `L(C^λ mod N²) · μ mod N` | OpenSSL BIGNUM |
| Homomorphic Aggregation | `C1 · C2 mod N²` (modular multiplication) | OpenSSL BIGNUM |
| ECDSA Signing | Modified ECDSA on P-256 curve | OpenSSL EC |
| Batch Verification | `(Σ e_i·s_i⁻¹)G + Σ r_i·s_i⁻¹·Q_i = Σ P_i` | OpenSSL EC |
| Differential Privacy | Laplace noise `Lap(Δf/ε)` encrypted and aggregated | C++ `<random>` |
| Registration | Mutual authentication via real ECDSA signature exchange | OpenSSL EC |
| Timing | Wall-clock `std::chrono::high_resolution_clock` | C++ STL |

## Source Files

```
src/
├── robust_iiot_sim.hpp    # DES engine header (entity structs, simulation class)
├── robust_iiot_sim.cpp    # Full PPDA pipeline implementation
├── exp1_main.cpp          # Experiment 1: Sensor Scalability (500–5000)
├── exp3_main.cpp          # Experiment 3: Workload Intensity
├── exp5_main.cpp          # Experiment 5: Loss Exposure (1–20 micro-slots)
└── Makefile               # Build targets for each experiment
```

## Experiments

| # | Experiment | Independent Variable | Output |
|---|-----------|---------------------|--------|
| 1 | Sensor Scalability | Number of sensors (500–5000) | `exp1_sensor_scalability/results.csv` |
| 3 | Workload Intensity | Readings per sensor per round | `exp3_workload_intensity/results.csv` |
| 5 | Loss Exposure | Number of micro-slots (1–20) | `exp4_loss_exposure/results.csv` |

## Build & Run

**Prerequisites**: Ubuntu 22.04, g++ with C++17, OpenSSL development libraries.

```bash
# Install dependencies (if needed)
sudo apt install libssl-dev

# Build all experiments
cd src/
make all

# Run individual experiments
./exp1_sensor_scalability
./exp3_workload_intensity
./exp4_loss_exposure
```

## Output Format

All `results.csv` files follow the project standard:
```csv
variable_value,primary_metric,secondary_metric_1,secondary_metric_2
500,12.3400,,
1000,24.5600,,
```

## Configuration

Default parameters (matching the paper):
- **Paillier key size**: 1024-bit primes (2048-bit N)
- **ECDSA curve**: P-256 (prime256v1)
- **Differential privacy ε**: 1.0
- **Edge servers**: 5 (round-robin sensor assignment)
- **Sensor data**: Temperature field from `plosha_dataset.csv`, quantized to integer

## Dependencies

- Shared crypto library: `src/crypto/paillier.{hpp,cpp}` and `src/crypto/modified_ecdsa.{hpp,cpp}`
- Dataset: `dataset/plosha_dataset.csv`
