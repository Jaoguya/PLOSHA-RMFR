# Reviewer Report — PLOSHA-RMFR Experimental Implementation

**Scope**: Experiment design, simulation methodology, and code correctness of the `plosha_rmfr/` scheme.  
**Reference context**: Ref[37] (Ren & Yao, IEEE TSC 2026) — *A Hybrid Fault-Tolerant Workflow Scheduling* — serves as a baseline comparator in Experiments 1–6.

---

## 1. Overall Assessment

The PLOSHA-RMFR implementation is a **well-structured discrete-event simulation** that faithfully translates the five-phase pipeline (Init → EWMA → PLOSHA → RMFR → AFLTO) into working C++ code with real cryptographic operations (Paillier via GMP, AES-256-GCM & ECDSA via OpenSSL). The experiment harness sweeps independent variables correctly and outputs standardized CSVs.

**Verdict**: Structurally sound. Several issues below range from **minor clarifications** to **major methodological concerns** that should be addressed before publication.

---

## 2. Strengths

| Area | Observation |
|------|-------------|
| **Real crypto** | Paillier 2048-bit, AES-256-GCM, ECDSA P-256 are executed end-to-end — no stubs. |
| **β_t calibration** | [crypto_wrapper.cpp:261–302](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/crypto_wrapper.cpp#L261-L302) measures actual per-slot overhead instead of using the paper constant. |
| **Blinding-factor pool** | Pre-computed pool of 10k blinding factors avoids Gramine `getrandom()` entropy starvation — pragmatic engineering. |
| **Reproducibility** | Fixed seed `rng(42)` in [des_engine.cpp:259](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L259) ensures deterministic failure injection. |
| **Modular design** | Clean separation: `ewma_predictor`, `plosha`, `rmfr`, `aflto`, `metrics` — each module maps 1:1 to a paper phase. |

---

## 3. Major Concerns

### 3.1 β_t Calibration Uses Only 5 Trials

> [!CAUTION]
> [crypto_wrapper.cpp:263](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/crypto_wrapper.cpp#L263): `num_trials = 5;` — The function signature accepts 100 and the caller passes 100, but the body immediately overrides it to 5.

This is likely a leftover from debugging RDRAND issues. **5 trials is far too few** for a stable micro-benchmark of Paillier + AES — variance will dominate. A reviewer would flag this as insufficient warm-up. **Fix**: Remove the override or bump to ≥100 with initial warm-up discards.

### 3.2 Epoch Count Is Low (10 Epochs Everywhere)

> [!WARNING]
> [config.hpp:93,96](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/config.hpp#L93-L96): `DEFAULT_NUM_EPOCHS = 10`, `ABLATION_EPOCHS = 10`.

10 epochs is thin for statistical significance, especially for:
- **Exp 4 (failure rate)**: Stochastic failure injection needs ≥30 epochs for stable means.
- **Exp 7 (ablation)**: Only 2 sweep points × 10 epochs — too few to claim AFLTO impact.

Recommend: ≥30 epochs for stochastic experiments, with reported confidence intervals (mean ± std or 95% CI).

### 3.3 Metrics CSV Lacks Confidence Intervals

[metrics.cpp](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/metrics.cpp) only outputs **mean** values. No standard deviation, min/max, or percentiles are recorded. A journal reviewer would require error bars on all graphs. The `MetricsCollector::computeAverages()` must also compute stddev.

### 3.4 MicroRecovery Uses Dummy Zero Encryptions

> [!IMPORTANT]
> [rmfr.cpp:129–133](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/rmfr.cpp#L129-L133): Re-aggregation encrypts zeros instead of actual missed sensor values.

For **latency measurement** this is acceptable (Paillier encrypt cost is data-independent). For **communication overhead** it is correct (ciphertext size is fixed). But the comment "simulates re-aggregation" should be explicit that the values are placeholders — a reviewer may misread this as fabricated results.

### 3.5 Ref[37] Baseline Is Completely Unimplemented

`fault_tolerant_workflow/src/` contains only `.gitkeep`. This scheme participates in **6 of 7 experiments**. Without it, no comparative claims can be made for Experiments 1–6. The same applies to `robust_iiot/`, `fed_dqn/`, and `ft_serverless_edge/` — all are empty.

---

## 4. Minor Concerns

### 4.1 Queue Utilization Measurement Timing

[des_engine.cpp:124](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L124): Queue utilization is measured **after** readings are submitted but **before** `drainQueue()`. The comment says "before draining" which is correct intent, but `getState()` is called on L101 (before drain), while queue_load is accumulated on L124 (after the `getState` call). This is consistent, but should be documented clearly.

### 4.2 Workload / Queue Load Are Identical

[fog_node.cpp:43](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/fog_node.cpp#L43): `state.queue_load = state.workload;` — These two metrics are defined identically. The paper (Eq. 8) treats W_i and Q_i as distinct dimensions. If the sensor reporting rate is the workload and the queue occupancy is the queue load, they should diverge under high workload_multiplier. Currently, Exp 3 (workload intensity) will show artificially correlated capacity degradation.

### 4.3 Failure Injection Resets Every Epoch

[des_engine.cpp:84](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L84): All fog nodes are reset to non-failed at the start of each epoch, then re-randomized. This means failures are **transient** (1-epoch duration). Ref[37]'s model (and most fault-tolerance literature) distinguishes between transient and persistent failures. The paper should state this assumption explicitly.

### 4.4 AFLTO Threshold Ordering May Collapse

[aflto.cpp:60–62](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/aflto.cpp#L60-L62): τ_v, τ_r, τ_f use `delta * 0.5` but share the same direction. Under sustained high error, all thresholds drift toward 0 or 1 simultaneously. The ordering enforcement (L66-67) with `MIN_GAP = 0.05` is correct, but the projection on L69-71 can undo the ordering if τ₃ exceeds 1.0. Consider clamping *after* ordering.

### 4.5 Loss Exposure Averaging

[des_engine.cpp:196–205](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L196-L205): Loss exposure is accumulated per-fog and then divided by `active_fogs`. This gives the **mean per-fog loss exposure**, not the **system-level** loss exposure. The paper should clarify which definition is used.

---

## 5. Alignment with Ref[37] Comparison

Ref[37] (Ren & Yao) proposes a **hybrid checkpointing + replication** strategy for workflow scheduling on fluctuating cloud resources. To fairly compare against PLOSHA-RMFR:

| Dimension | PLOSHA-RMFR (Current) | Ref[37] Requirements |
|-----------|----------------------|---------------------|
| Failure model | Transient, per-epoch random | Must match: VM performance fluctuation + sudden failure |
| Recovery mechanism | RMFR (delegation/micro-recovery/failover) | Checkpointing + task replication |
| Workload model | Sensor readings, round-robin | DAG-structured workflows |
| Cost metric | Communication KB | Checkpoint storage + replication cost |

The `fault_tolerant_workflow/` implementation **must faithfully model** Ref[37]'s checkpointing/replication approach, not merely wrap PLOSHA logic with different parameters. This is the most critical gap for the comparative study.

---

## 6. Summary of Required Actions

| Priority | Item | File(s) |
|----------|------|---------|
| 🔴 Critical | Remove β_t trial override (5→100+) | [crypto_wrapper.cpp:263](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/crypto_wrapper.cpp#L263) |
| 🔴 Critical | Increase epoch count for stochastic experiments | [config.hpp:93-96](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/config.hpp#L93-L96) |
| 🔴 Critical | Add stddev/CI to metrics output | [metrics.cpp](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/metrics.cpp), [metrics.hpp](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/metrics.hpp) |
| 🔴 Critical | Implement all 4 baseline schemes | `fault_tolerant_workflow/`, `robust_iiot/`, `fed_dqn/`, `ft_serverless_edge/` |
| 🟡 Major | Differentiate W_i from Q_i | [fog_node.cpp:43](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/fog_node.cpp#L43) |
| 🟡 Major | Document transient-failure assumption | [des_engine.cpp:84](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L84) |
| 🟢 Minor | Fix AFLTO threshold projection order | [aflto.cpp:66-71](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/aflto.cpp#L66-L71) |
| 🟢 Minor | Clarify loss-exposure averaging semantics | [des_engine.cpp:196](file:///run/media/peppo/DATA1/PLOSHA/PLOSHA-RMFR%28cpp%29/schemes/plosha_rmfr/src/des_engine.cpp#L196) |
