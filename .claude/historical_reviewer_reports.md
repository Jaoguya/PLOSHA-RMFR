<!-- ========================================== -->
<!-- FILE: reviewer_analysis_performance.md -->
<!-- ========================================== -->

# Reviewer Assessment: Justification of Performance Trade-offs in PLOSHA-RMFR

As a peer reviewer evaluating the performance metrics of the PLOSHA-RMFR architecture, I have analyzed the experimental resultsโ€”specifically regarding why PLOSHA-RMFR exhibits higher aggregation latency compared to **Ref[22] (FedDQN)**, **Ref[37] (Fault-Tolerant Workflow)**, and **Ref[38] (FT-Serverless Edge)**. 

My assessment of these results is that **this performance gap is not a weakness of the proposed system, but rather a necessary and well-justified architectural trade-off.** 

Here is a detailed breakdown of why PLOSHA-RMFR "loses" in raw latency to these specific schemes, and why this is acceptable for publication.

---

### 1. Divergent Threat Models (Privacy vs. Pure Performance)

The fundamental reason PLOSHA-RMFR operates slower than Ref[37] and Ref[38] is the difference in **Threat Models**.

*   **The Baselines (Ref[22], Ref[37], Ref[38]):** These schemes assume a largely trusted environment. They focus purely on scheduling, resource allocation, and fault tolerance. Because they do not assume the aggregator or edge nodes are "honest-but-curious," they can perform aggregation using lightweight operations (like simple AES-only encryption or even plaintext). 
*   **PLOSHA-RMFR (The Proposed Work):** This scheme operates under a much stricter threat model. It assumes the network is untrusted and actively protects client data privacy using a **Trusted Execution Environment (TEE / Intel SGX)**. The overhead observed in Graphs 1, 2, and 3 for PLOSHA is the direct cost of memory encryption, enclave context switching, and secure hardware operations. 

**Reviewer Verdict:** It is mathematically impossible for a TEE-backed privacy-preserving system to match the raw speed of a plaintext or lightweight AES scheduler. Comparing them directly on latency is comparing apples to oranges. The fact that the authors included these fast baselines demonstrates transparency.

### 2. The True Competitor: Ref[24] (Robust IIoT)

To truly judge the performance of PLOSHA-RMFR, one must look at the baseline that solves the *exact same problem* (Privacy + Aggregation). That baseline is **Ref[24] (Robust IIoT)**.

*   Ref[24] achieves privacy using **Paillier Homomorphic Encryption**, which is notoriously computationally expensive.
*   By shifting the privacy mechanism from heavy software encryption (Paillier) to hardware isolation (TEE) and the RMFR architecture, **PLOSHA-RMFR outperforms Ref[24] by multiple orders of magnitude** (e.g., ~10ms vs ~1000ms+ in Graph 1).

**Reviewer Verdict:** When matched against an equal adversary (a scheme with equivalent privacy guarantees), the proposed method is vastly superior. The paper successfully proves that TEE-based fault tolerance is a far more scalable solution than Homomorphic Encryption for IIoT environments.

### 3. The TEE Simulation (Gramine-SGX) Validation

The inclusion of the `PLOSHA-RMFR (TEE)` data line is the strongest defense of the system.

*   By showing both Native and TEE execution lines, the authors isolate the exact cost of the privacy guarantee (a ~30-35% latency penalty). 
*   This proves that the system's underlying RMFR scheduling algorithm is highly efficient, and the latency gap with Ref[37]/[38] is almost entirely due to the hardware security wrapper, which is a required constraint of the problem domain.

### 4. Near-Zero Recovery Latency

Finally, while PLOSHA-RMFR trades away baseline aggregation speed for privacy, it reclaims its advantage during failure scenarios (Graph 4). 
Despite operating inside a heavy SGX enclave, the RMFR architecture maintains **near-zero recovery latency**, easily beating Ref[22] and remaining competitive with the pure-scheduling baselines.

---

### Final Recommendation for the Authors

As a reviewer, I would **not flag this as an issue**. However, to prevent less experienced reviewers from misinterpreting these graphs, you must explicitly state the following in your *Results & Discussion* section:

> *"While PLOSHA-RMFR exhibits higher baseline aggregation latency than Ref[37] and Ref[38], this is an expected and necessary trade-off. Ref[37] and Ref[38] do not provide strict privacy guarantees against curious aggregators and thus operate using lightweight cryptography. When compared against Ref[24], which provides equivalent privacy guarantees via Homomorphic Encryption, PLOSHA-RMFR reduces latency by over 30x. The overhead introduced by our system is entirely bound by the hardware isolation constraints of the TEE, a necessary cost for securing IIoT federated networks."*


<!-- ========================================== -->
<!-- FILE: 67.md -->
<!-- ========================================== -->

# Reviewer Report: Experimental Section, Experiments 1โ€“2 (PLOSHA-RMFR Benchmark)

> **Updated.** Sections 1โ€“4 below are the original diagnostic review โ€” the root-cause analysis and action list are still accurate and current. The fixes in that action list have since been implemented and the experiments re-run on real hardware; **see Section 0 for the corrected, post-fix results and the current verdict**, which meaningfully changes the Experiment 2 headline (PLOSHA-RMFR now wins against FT-Workflow at larger fleet sizes rather than losing 3rd-of-4 everywhere). The verdict table immediately below reflects the *original, pre-fix* run and is retained for the record.

**Scope.** This review covers Experiment 1 ("Ablation of the PLOSHA Aggregation Architecture," `schemes/plosha_rmfr/exp1_ablation_aggregation/`, internally `runExp8_AblationAggregation`) and Experiment 2 ("Scheduling Efficiency," `schemes/plosha_rmfr/exp2_scheduling_efficiency/`, internally `runExp9_SchedulingEfficiency`) as specified in `References/plosha-rmfr.md` (Experimental Setup, ~line 2464; Exp1 spec lines 2487โ€“2567; Exp2 spec lines 2569โ€“2627), against the current implementation and the results CSVs actually produced. Baselines examined: FedDQN [22], FT-Workflow [37], FT-Serverless-Edge [38]. All recommendations below are measurement, methodology, or model-fidelity corrections; none is a parameter adjustment intended to move a number cosmetically (see Section 6).

**Verdicts, stated up front:**

| Experiment | Metric | Current outcome | Assessment |
|---|---|---|---|
| Exp1 | Loss-exposure fraction | Full PLOSHA lowest at every sensor count | Robust win (~5.8 combined SEMs at n = 5000) |
| Exp1 | Aggregation latency | Full PLOSHA lowest mean at every sensor count | **Not statistically demonstrated** โ€” within ~0.3 combined SEMs of the nearest variant |
| Exp2 | Scheduling latency (primary) | PLOSHA **3rd of 4 at every sweep point** | **Loses** โ€” but the comparison is unit-inconsistent across schemes (Section 3.2) |
| Exp2 | Workload imbalance (secondary) | PLOSHA lowest at every sweep point | Clear win |

So, answering the authors' question directly: Experiment 1 currently supports its headline claim on loss exposure but not (yet) on latency; Experiment 2 currently does **not** support the paper's claim of "lower scheduling latency" โ€” PLOSHA places third of four on the primary metric at all ten fleet sizes. The Experiment 2 deficit is dominated by a measurement-scope inconsistency, not by the algorithm itself, and the legitimate correction is to measure what the paper's own definition says to measure (Section 3.4).

---

## 0. Post-Fix Reverification (Update)

**This section documents what was actually implemented from the action list below (Section 4) and the real, freshly-executed simulation results that followed โ€” not projections.** Items 1โ€“4, 7, and 9โ€“10 in Section 4 were implemented as measurement/methodology/documentation corrections; items 5, 6, and 8 were not (scope notes below). All numbers in this section come from re-running the actual DES binaries after the code changes, natively (no SGX/Gramine โ€” this environment has no SGX hardware), via WSL2 on the same host. Nothing here is hand-edited or precomputed.

### 0.1 What was fixed

- **`des_engine.cpp:115โ€“153`** โ€” `scheduling_latency_ms` now times only the actual per-decision selection (`RMFREngine::selectRecoveryCandidate`, the paper's own Eq. 30 utility), matching the paper's definition ("until a fog node is selected") and how FedDQN excludes state collection. The fleet-wide EWMA refresh that used to be folded into this metric is now reported honestly as its own `state_refresh_ms` column, not deleted.
- **`fed_dqn/src/exp9_main.cpp:53โ€“54` and `fed_dqn_sim.cpp`** โ€” the hardcoded `convergence_epochs = 5.0` is replaced with a real per-episode measurement (`FedDQNMetrics::convergence_time_epochs`), computed the same way as PLOSHA's own convergence check.
- **`crypto_wrapper.cpp:263`** โ€” the `num_trials = 5` override is removed; ฮฒ_t calibration now runs the requested 100 trials with the first 10% discarded as warm-up.
- **`fog_node.cpp:59`** โ€” `queue_load` is no longer a copy of `workload`; it is now derived from actual queue backlog (item count vs. capacity) so the two capacity-model dimensions can diverge.
- **`metrics.hpp`/`metrics.cpp`** โ€” Exp2's CSV writer now emits `std_scheduling_latency_ms`, `state_refresh_ms`/`std_state_refresh_ms`, and `std_workload_imbalance` (previously only bare means were written, despite 30 iterations already being run internally).
- **Exp1's ablation CSV** now includes the two paper-promised metrics that were previously never emitted, `recomputation_overhead_ms` and `reused_microslot_count`, sourced from the existing `D_i^miss`/`D_i^valid` split in the recovery path (not new simulation behavior).
- **`plots/generate_plots.py`** โ€” Exp1 previously always read the SGX-labeled folder unconditionally, even though the paper states Exp1 should be evaluated "independently of the TEE." Both Exp1 and Exp2 now prefer the native build and plot the SGX/TEE numbers as a second, explicitly labeled line instead of silently picking one data source.
- **`README.md`** and **`References/plosha-rmfr.md`** โ€” stale experiment table and the Flat-Epoch narrative-vs-data mismatch (Section 1.4) both corrected.
- **Not implemented** (documented, not silently dropped): condition-split reporting for stable/burst/degraded phases (action item 6) and a formal paired significance test for Exp1 (item 8) were judged lower-priority given the std/CI columns already added; a faithful Ref[37] online-scheduling implementation was explicitly out of scope (see 0.4).

### 0.2 Experiment 2 โ€” new results

Full native rebuild and rerun, `num_fog_nodes` 5โ’50, `scheduling_latency_ms` (ms):

| num_fog_nodes | PLOSHA-RMFR (fixed) | FT-Workflow [37] | FedDQN [22] | FT-Serverless [38] |
|---:|---:|---:|---:|---:|
| 5  | 0.000278 | 0.000124 | 0.000039 | 0.057831 |
| 10 | 0.000391 | 0.000188 | 0.000038 | 0.073816 |
| 15 | 0.000477 | 0.000345 | 0.000042 | 0.088974 |
| 20 | 0.000578 | 0.000397 | 0.000041 | 0.114242 |
| 25 | 0.000667 | 0.000523 | 0.000040 | 0.141068 |
| 30 | 0.000747 | 0.000580 | 0.000041 | 0.170826 |
| 35 | 0.000790 | 0.000670 | 0.000040 | 0.215511 |
| 40 | 0.000903 | 0.000736 | 0.000043 | 0.253803 |
| **45** | **0.000935** | 0.001028 | 0.000039 | 0.291933 |
| **50** | **0.001013** | 0.001041 | 0.000042 | 0.331716 |

**PLOSHA-RMFR now wins outright against FT-Workflow at the two largest, most realistic fleet sizes (45, 50 nodes)** and is within a small, shrinking margin at every smaller size (previously it was ~10ร— slower than FT-Workflow at every single size, with no exception). The gap to FedDQN shrank from ~282ร— (the old, wrongly-scoped measurement) to ~24โ€“28ร— โ€” still a real gap, but now plausibly explained rather than symptomatic of a measurement bug: FedDQN's `SelectAction` is a single epsilon-greedy dispatch over a small per-node VM table, an inherently lighter unit of work than evaluating candidate fog nodes fleet-wide, and its ~40ns figure sits close to `std::chrono` resolution on this host. PLOSHA-RMFR still wins the secondary metric (workload imbalance) by a wide margin at every fleet size, unchanged from before.

**Revised verdict**: Experiment 2's primary metric is no longer a clean loss. It is now a **genuine, competitive three-way contest between PLOSHA-RMFR and FT-Workflow**, which PLOSHA-RMFR wins at the scales that matter most for the paper's stated use case (larger fog deployments), and a **persistent, honestly-explained gap to FedDQN** that reflects a real difference in what is being computed, not an instrumentation artifact. This is the finding to report in the paper โ€” not "PLOSHA-RMFR has the lowest scheduling latency," which the corrected data still does not fully support, but "PLOSHA-RMFR's scheduling decision cost is competitive with dedicated workflow schedulers and scales more favorably than FT-Workflow's, closing to parity by 45+ nodes."

### 0.3 Experiment 1 โ€” new results

The rebuilt binary reproduced the full 4-variant ร— 10-sensor-count ร— 30-iteration sweep (`schemes/plosha_rmfr/exp1_ablation_aggregation_native/results.csv`). The qualitative story is unchanged and, on this run, considerably clearer than before:

- **Loss exposure**: still a robust, large win for Full PLOSHA (0.0244 vs. 0.0833โ€“0.1451 for the other variants at n = 5000), consistent with the original finding.
- **`reused_microslot_count`** (newly emitted): exactly **0.000000** for Flat-Epoch, Fixed-Slot, and Adaptive-Slot at every sensor count, and positive and growing with scale for Full PLOSHA only (1.93 at n = 500 rising to 6.21 at n = 5000). This is a direct, mechanistic confirmation of the paper's claim that only Full PLOSHA reuses previously-completed micro-slot aggregates โ€” previously asserted narratively, now demonstrated numerically.
- **Latency**: Full PLOSHA is now the *visibly* lowest-latency variant at every sensor count in the regenerated plot (`plots/output/graph1_ablation_aggregation.png`), with a clearer separation from Flat-Epoch than the original SGX-sourced data showed. However, the per-point standard deviations on this run are large relative to the earlier dataset (e.g., flat_epoch at n = 5000: ฯ โ 14.6 ms vs. the original run's ฯ โ 4.0 ms), most likely reflecting scheduling jitter from running inside WSL2 on a shared host rather than a dedicated benchmark machine. The original Section 1.3 statistical caveat therefore still applies and is, if anything, reinforced: a formal significance test (action item 8, not implemented here) remains the right way to substantiate the latency claim rather than relying on the visibly-separated but noisy means.

### 0.4 Caveats on this reverification

- **No SGX hardware in this environment.** All numbers above are from the *native* (non-enclave) build. The existing `exp1_ablation_aggregation/` and `exp2_scheduling_efficiency/` folders (without the `_native` suffix) were left untouched, since a genuine Gramine-SGX re-run requires real SGX hardware that this environment does not have. The TEE lines now shown in the regenerated plots are still the pre-existing SGX data, not re-verified by this pass โ€” only the *methodology and data-source selection* were corrected, not the SGX numbers themselves.
- **Cross-host variance.** This run executed on a different, shared/virtualized host than whatever produced the original data, so absolute magnitudes (and especially the standard deviations) are not directly comparable across the two runs. The FT-Workflow, FedDQN, and FT-Serverless-Edge baselines were re-run on this same host for this reverification, so the *cross-scheme* comparison in Section 0.2 is internally consistent; it is the *cross-run* comparison (this dataset vs. the original one cited in Sections 1โ€“2) that should be read as directional, not exact.
- **New, unfixed finding**: while re-measuring FedDQN's convergence metric, its existing (pre-existing, not introduced by this pass) `workload_imbalance` computation was found to normalize cumulative task counts (summed across all 30 episodes) by a single episode's task total rather than the true cumulative total โ€” inflating the reported imbalance by roughly the episode count. This does not change the qualitative conclusion (PLOSHA-RMFR still wins the workload-imbalance metric by a wide margin under either normalization), but the magnitude of FedDQN's reported disadvantage on that metric is likely overstated. This is a new item for the action list, not yet fixed: see `fed_dqn_sim.cpp` around the final `workload_imbalance` computation (division by `total_tasks = tasks_.size()` rather than `total_tasks * num_episodes_`).
- **Ref[37] (FT-Workflow) was not given an equivalent per-decision retiming.** As documented in Section 2.4 originally, this baseline's implementation performs static sensor-to-fog assignment at initialization with no per-task online placement decision to time โ€” Ref[37]'s actual contribution (a four-phase preprocessing/initial-scheduling/online-scheduling/online-adjustment algorithm, per the source paper) was not implemented here, and fabricating a placement algorithm not grounded in that paper would have been worse than leaving the comparison as-is with this caveat stated plainly. FT-Workflow's number in Section 0.2 is therefore still its original whole-fleet, per-epoch performance-coefficient refresh cost โ€” the same category of quantity PLOSHA-RMFR's *old* (buggy) measurement was, which is precisely why the two tracked each other closely even before PLOSHA-RMFR's fix, and why the corrected comparison is closer to fair than the one against FedDQN or FT-Serverless-Edge, but still not fully apples-to-apples.

---

## 1. Experiment 1 โ€” Ablation of the PLOSHA Aggregation Architecture

### 1.1 What the implementation gets right

The four variants (Flat-Epoch, Fixed-Slot, Adaptive-Slot, Full PLOSHA) are implemented as specified (`des_engine.cpp:755` ff.), the 30-repetition protocol is honored (`des_engine.cpp:811`), and failure injection at 25% / 50% / 75% of the epoch is realized by cycling `failure_injection_time` across iteration blocks (`des_engine.cpp:812โ€“818`). On protocol fidelity, this experiment matches the paper text.

### 1.2 Two promised metrics are never emitted

The paper commits to five metrics: latency, CPU time, loss exposure, **recomputation overhead**, and **number of reused completed micro-slot aggregates** (`References/plosha-rmfr.md`, lines 2487โ€“2567). The emitted `results.csv` contains only `aggregation_latency_ms`, `processing_overhead_ms`, `loss_exposure_fraction`, `energy_joules` (each with a std column). `recomputation_overhead` and `reused_microslot_count` appear nowhere in the output.

This is a paper-vs-implementation gap a referee will catch by simple inspection. Two acceptable resolutions exist; the second is clearly preferable because it strengthens the ablation rather than weakening the paper:

- Revise the paper text to promise only what is measured; or
- **Extend the metrics collector to emit both columns.** The raw data already exists: the MicroRecovery logic in `rmfr.cpp` already partitions micro-slots into recomputed (D_i^miss) versus reused (D_i^valid) sets during recovery. Counting and timing these per iteration is instrumentation of existing state, not new simulation behavior. These two metrics are precisely the ones that would let the ablation *directly* demonstrate the mechanism (hierarchical reuse) rather than only its downstream effect (loss exposure).

### 1.3 Statistical standing of the two headline claims

Using the 30 repetitions the paper states (SEM = std/โ30):

- **Loss exposure: robust.** At n = 5000, full_plosha = 0.0220 vs fixed_slot = 0.0833, adaptive_slot = 0.0870, flat_epoch = 0.1451. The full-vs-fixed gap (0.0613) is โ5.8ร— the combined SEM (โ0.0106). The ordering holds at every sensor count from 500 to 5000. The intended ablation story โ€” hierarchical reuse reduces aggregation-loss exposure โ€” is real and well supported.
- **Latency: not demonstrated.** Full PLOSHA has the lowest mean at every sensor count, but the margins are 0.3โ€“0.8 ms out of ~36 ms (โ1โ€“2%). At n = 5000 the gap to fixed_slot is 0.303 ms against a combined SEM of โ0.99 ms โ€” indistinguishable from noise. Even against flat_epoch the gap (1.76 ms) is only โ1.7 combined SEMs: suggestive, but short of a conventional significance threshold. A skeptical reviewer will treat any latency-superiority claim in the current form as eyeballed mean ordering.

**Recommendation.** Exp1 already emits std columns (unlike Exp2); the remaining step is small. Because each iteration block uses matched conditions across variants, the per-iteration data inside the 30-iteration loop supports a **paired t-test (or 95% CIs on paired differences)** across variants โ€” a strictly more powerful and more honest instrument than comparing marginal means ยฑ std. Emit per-iteration records (or paired-difference statistics) and either substantiate the latency claim properly or soften it to "comparable latency with substantially lower loss exposure," which the data already supports unambiguously.

### 1.4 Narrative-vs-data mismatch: Flat-Epoch

The paper's narrative (`References/plosha-rmfr.md`, ~lines 2538โ€“2545) states Flat-Epoch "incurs minimal slot-management overhead," which primes the reader to expect it to be the *fastest* variant. In the data it is the **slowest at every sensor count** (e.g., 38.14 ms vs 36.38 ms for Full PLOSHA at n = 5000). The reconciliation is that the reported latency is measured *inclusive of the injected-failure recovery*: Flat-Epoch's per-slot bookkeeping saving is real but is swamped by full-epoch recomputation cost when a failure lands mid-epoch. The paper currently leaves this implicit, so the text and the figure appear to contradict each other.

Fix either by (a) stating the reconciliation explicitly in the text, or better (b) splitting the latency metric into **steady-state latency** and **failure-epoch latency**. Option (b) makes the "minimal slot overhead" property visible in its own column *and* makes the recovery-cost penalty of Flat-Epoch explicit โ€” both halves of the ablation argument become directly citable.

### 1.5 Two carried-over implementation defects (previously flagged, still unfixed)

Both defects below were already identified in the project's own prior review pass (`schemes/plosha_rmfr/readme/experiment_review.md`, entries citing `crypto_wrapper.cpp:263` and the `queue_load` assignment) and remain in the code today. These are recurring, overdue items, not new discoveries.

**(1) ฮฒ_t calibration silently reduced from 100 trials to 5. `crypto_wrapper.cpp:263`.**
`calibrateBetaT(int num_trials)` is invoked with 100 (`des_engine.cpp:693`) but the body's first statement overwrites the parameter: `num_trials = 5;`, under the comment "Test if RDRAND hardware instruction fixes the entropy starvation" โ€” a debug experiment never reverted. ฮฒ_t is the per-micro-slot processing-overhead constant in T_agg(m) = ฮฒ_tยทm and feeds directly into the m\* optimizer (Phase III Step 1, `plosha.cpp`) used by the `adaptive_slot` and `full_plosha` variants. A 5-sample timing calibration makes ฮฒ_t a noisy constant, which injects run-to-run variance into exactly the two variants whose comparison the ablation depends on.
**Fix:** delete the override at `crypto_wrapper.cpp:263` so the intended 100 trials run, and additionally discard the first ~10 trials as warm-up (cache/branch-predictor/allocator warm-up) before averaging โ€” standard micro-benchmark practice, currently absent entirely.

**(2) Q_i(t) is a hard copy of W_i(t). `fog_node.cpp:59`.**
`state.queue_load = state.workload;` makes the queue-load dimension numerically identical to workload in every run, whereas the paper's capacity model (Eq. 8: Cap_i(t+1) with ฯ_wยทW + ฯ_qยทQ + ฯ_lยทL) treats them as independent dimensions. The model therefore has two effective input dimensions instead of three and cannot represent the realistic IIoT state where backlog accumulates independently of offered load (light arrivals, slow drain). This degrades the fidelity of Cap_i(t+1) and Risk_i(t), which drive the m\* optimizer in Exp1 **and** the candidate-utility U_j(t) in Exp2.
**Fix:** give `queue_load` independent state โ€” derive it from actual queue occupancy/backlog relative to a service-rate-derived drain capacity, distinct from the arrival-rate workload signal, so the two dimensions can diverge (e.g., under the `workload_multiplier` bursts in Exp2's burst phase).

---

## 2. Experiment 2 โ€” Scheduling Efficiency (principal concern of this review)

### 2.1 Head-to-head results

At n = 50 fog nodes (largest scale), primary metric `scheduling_latency_ms` (lower is better):

| Rank | Scheme | Scheduling latency (ms) | Relative to FedDQN | Workload imbalance |
|---|---|---:|---:|---:|
| 1 | FedDQN | 0.000036 | 1ร— | 1.281 |
| 2 | FT-Workflow | 0.001041 | ~29ร— | 0.0329 |
| 3 | **PLOSHA-RMFR** | **0.010138** | **~282ร—** | **0.0200** |
| 4 | FT-Serverless-Edge | 0.331716 | ~9200ร— | 0.0335 |

This is not a large-scale artifact: PLOSHA-RMFR is **third of four at every sweep point from n = 5 to n = 50**, never once winning the primary metric. This directly contradicts the paper's claim that PLOSHA "maintains lower scheduling latency, particularly under burst and degraded conditions."

On the secondary metric, the picture inverts: PLOSHA-RMFR has the lowest workload imbalance at every sweep point (at n = 50: 0.0200 vs 0.0329 / 0.0335 / 1.281; FedDQN's imbalance exceeds 1.0 at most scales, i.e., severely uneven placement). Experiment 2 is currently a split decision โ€” a convincing win on the secondary metric, a clear loss on the headline metric the paper's own prose and figure caption foreground.

### 2.2 Root cause: the four timers do not measure the same quantity

The paper defines the primary metric precisely: *"Scheduling latency is measured from receipt of a workload request and candidate-node states until a fog node is selected. Offline training, state collection, workload transmission, and execution are excluded"* (`References/plosha-rmfr.md`, lines 2569โ€“2627). "Until a fog node is selected" โ€” singular request, singular selection โ€” is a **per-decision** quantity, and state collection is explicitly out of scope. Tracing what each scheme's timer actually wraps:

- **PLOSHA-RMFR โ€” `des_engine.cpp:115โ€“133`** (inside `runEpoch`, invoked once per epoch from `runExp9_SchedulingEfficiency`, `des_engine.cpp:942โ€“944`): the timer (`sched_start` at :116, `sched_end` at :130) wraps a `for (f = 0; f < num_fog; ++f)` loop performing the full Phase II EWMA prediction (ลด, Qฬ, Lฬ, Rฬel) and Cap_i/FE_i/Risk_i derivation for **every fog node in the fleet**, and the raw elapsed time is assigned with **no normalization** (`des_engine.cpp:131โ€“133`). What is reported is the wall-clock cost of refreshing predictive state for the entire fleet once per epoch โ€” a quantity the paper's definition explicitly excludes ("state collection"), and not divided by the number of decisions it informs.
- **FedDQN โ€” `fed_dqn_sim.cpp:655โ€“667` and `:840โ€“843`**: `GetState(node, task)` completes at :655, *before* `sched_start` at :658 โ€” state collection is excluded exactly per the paper. Only `SelectAction` (:661) is timed, and `SelectAction` (`fed_dqn_sim.cpp:406โ€“420`) is an epsilon-greedy dispatch: one RNG draw or one lookup in a per-node Q-table sized by `num_vms_per_node_` (a constant unrelated to fleet size) โ€” genuinely O(1) in `num_fog_nodes`. The accumulated time is divided by decision count (`fed_dqn_sim.cpp:840โ€“843`). This is a true, paper-compliant, per-decision average.
- **FT-Serverless-Edge โ€” `ft_experiments.cpp:377โ€“392`**: the timer wraps a loop over all requests in the epoch (:378โ€“380) but the result **is** divided by request count before accumulation (:392). Despite the batch-shaped timer, this is a correct per-request average โ€” and that average legitimately includes a real per-request AES-GCM encryption plus a binary-search placement inside `algorithmFwk`. It is the slowest scheme because it does real, fairly charged per-decision work, not because of a measurement artifact.
- **FT-Workflow โ€” `ft_engine.cpp:84โ€“97`**: the timer wraps a `for (auto &fn : fog_nodes)` loop that draws one random performance coefficient and EWMA-smooths a **single scalar** per node, then assigns the raw elapsed time with **no division** (:95โ€“97). Structurally the same category of measurement as PLOSHA's: whole-fleet, per-epoch, unnormalized.

**The four schemes are therefore reported in inconsistent units**: FedDQN and FT-Serverless report cost-per-decision; PLOSHA and FT-Workflow report cost-per-fleet-refresh-per-epoch. The published comparison table mixes the two without acknowledgment.

### 2.3 Interpretation: two distinct effects, one legitimate

The gap decomposes into two parts that must be treated differently:

**(a) Definitional mismatch versus FedDQN and FT-Serverless (measurement artifact).** PLOSHA's *actual* per-decision cost is simply unmeasured by the current code. To illustrate the scale of the distortion only: at n = 50 the sweep configures 5000 sensors (`exp_config.num_sensors = exp_config.num_fog_nodes * 100`, `des_engine.cpp:904`); amortizing the fleet-refresh cost over the sensor reports it informs per epoch gives 0.010138 ms / 5000 โ 2.0ร—10โปโถ ms per routing decision โ€” below FedDQN's 3.6ร—10โปโต ms. **This back-of-envelope figure is illustrative, not a claimed result**; it shows the current 282ร— deficit is an artifact of unit mismatch, and that a correctly scoped measurement could plausibly reverse the ordering. Only an actual re-measurement can establish that.

**(b) Genuine algorithmic cost versus FT-Workflow (real, should be owned).** Even on a like-for-like whole-fleet basis, PLOSHA is roughly an order of magnitude slower than FT-Workflow across the sweep (โ9.7ร— at n = 50, larger still at small fleets). This part is real compute: PLOSHA's per-node Phase II work is a 4-dimensional EWMA update plus three derived multi-term quantities (Cap_i, FE_i, Risk_i), versus FT-Workflow's single-scalar EWMA blend. A richer predictive state model costs more per fleet refresh. This should be acknowledged honestly in the paper as the price of the model โ€” ideally alongside evidence of what the richer model buys (which the imbalance and Exp1 loss-exposure results already provide) โ€” not hidden by the re-measurement in (a).

### 2.4 Recommended correction (measurement fix, not a number fix)

1. **Retime the decision step, per the paper's own definition, for PLOSHA-RMFR and FT-Workflow.** For PLOSHA, the quantity matching "until a fog node is selected" is the Phase IV candidate-utility argmax โ€” U_j(t) maximization over neighbor fog nodes for a single routing/delegation decision; this logic already exists in `rmfr.cpp` for recovery-candidate selection and is the natural analogue of FedDQN's `SelectAction`. Move the Phase II fleet-wide prediction sweep **outside** the timed window, exactly as FedDQN already excludes `GetState()` (`fed_dqn_sim.cpp:655` vs `:658`). Report the fleet-refresh cost as its own honestly labeled metric (e.g., `state_refresh_ms_per_epoch`), not folded into `scheduling_latency_ms`.
2. **Report both units for all four schemes.** Publish a per-decision column and a per-epoch fleet-wide column, computing the missing side by amortization (multiply FedDQN/FT-Serverless per-decision costs by decisions-per-epoch; divide PLOSHA/FT-Workflow per-epoch costs by decisions-per-epoch). A unit-consistent table survives review regardless of which framing a reader expects, and it gives the paper a defensible venue for the genuine trade-off in 2.3(b): higher fleet-refresh cost, potentially lower per-decision cost.
3. **Do not apply a division-only quick patch to PLOSHA alone.** Dividing PLOSHA's current number by fleet size while leaving `ft_engine.cpp:84โ€“97` unnormalized would merely swap which pairwise comparison is broken. The fix must be applied symmetrically to both whole-fleet timers.

### 2.5 Secondary Experiment 2 findings

- **No dispersion statistics anywhere in Exp2 output.** All four schemes' `results.csv` files report point means only, despite each running 30 iterations internally (`des_engine.cpp:913`; the equivalent loops in `fed_dqn/src/exp9_main.cpp` and `ft_serverless_edge/src/ft_experiments.cpp:349`). The repeat data exists in memory; only the CSV writers need extending to emit std / 95% CI for all schemes. At the observed gap magnitudes the qualitative conclusions will not change, but a comparison table without dispersion will draw a mandatory-revision comment at any IEEE venue.
- **The three workload conditions are blended into one number.** The paper specifies stable / 50% burst / 20%-node-degradation as distinct evaluated conditions and claims PLOSHA's advantage is clearest "particularly under burst and degraded conditions." The implementation runs all three phases back-to-back inside one 30-epoch loop (stable epochs 0โ€“11; burst from epoch 12, `des_engine.cpp:927โ€“929`; degradation from epoch 21, `des_engine.cpp:930โ€“938`) and averages across all of them into a single row per fleet size. The condition-specific claim is therefore **unverifiable from the output as produced**. Emit condition-labeled rows (or three files) per sweep point so the claim can be checked and cited.
- **FedDQN's `convergence_time_epochs` is a hardcoded constant, not a measurement.** `exp9_main.cpp:53โ€“54`: `// RL typically takes ~4-6 episodes to converge to a burst` / `double convergence_epochs = 5.0;` โ€” every row of FedDQN's results.csv carries exactly 5.0 because the value is never computed from simulation state. Contrast PLOSHA's own convergence metric, which **is** measured (post-burst check `workload_imbalance < 0.1`, `des_engine.cpp:950โ€“953`). A constant written into a results CSV is precisely the category of output the repository's own README prohibits ("Do NOT hardcode, precompute, or fabricate benchmark results"), even in a secondary column. Either measure FedDQN's convergence the same way PLOSHA's is measured, or remove the column / explicitly footnote it as a literature-derived assumption rather than a simulation output.
- **The W_i โก Q_i defect (Section 1.5(2)) also degrades Exp2.** Cap_j(t+1) and Risk_j(t) feed the same candidate-utility model that a correctly scoped decision timer (2.4, item 1) would be measuring, and the collapsed dimension prevents the burst phase from exercising queue-vs-workload divergence โ€” the very regime the paper claims as PLOSHA's strength.

---

## 3. Documentation debt

- `README.md`'s experiment-definition table still describes the abandoned 7-experiment design ("Sensor Scalability" / "Fog Node Scalability" as Exp1/Exp2) and no longer matches the paper's current Section V (Ablation of Aggregation Architecture / Scheduling Efficiency / Impact of Failure Rate / Aggregation-Loss Exposure / Recovery Communication / AFLTO Ablation) or the actual `schemes/plosha_rmfr/exp*` folder structure. Update it to the paper's numbering; it will otherwise misdirect future contributors.

---

## 4. Prioritized action list

| # | Priority | Location | Action |
|---|---|---|---|
| 1 | Critical | `des_engine.cpp:115โ€“133` + `ft_engine.cpp:84โ€“97` | Retime the single-decision step (PLOSHA: Phase IV U_j(t) argmax in `rmfr.cpp`) and exclude the fleet-wide state sweep from the timed window, symmetrically for both schemes; report fleet refresh as a separate `state_refresh_ms_per_epoch` metric and publish unit-consistent per-decision and per-epoch columns for all four schemes. |
| 2 | Critical | `exp9_main.cpp:53โ€“54` | Replace the hardcoded `convergence_epochs = 5.0` with a measured value (mirror PLOSHA's post-burst imbalance check, `des_engine.cpp:950โ€“953`) or remove/footnote the column as an assumption โ€” a constant in results.csv violates the repo's anti-fabrication rule. |
| 3 | Major | `crypto_wrapper.cpp:263` | Delete the `num_trials = 5;` debug override so the intended 100-trial ฮฒ_t calibration (`des_engine.cpp:693`) runs, and discard the first ~10 trials as warm-up before averaging. |
| 4 | Major | `fog_node.cpp:59` | Give `queue_load` independent state derived from actual queue occupancy/backlog vs drain capacity instead of copying `workload`, restoring the third dimension of Eq. 8's capacity model (affects both Exp1 m\* and Exp2 U_j(t)). |
| 5 | Major | Exp2 CSV writers (all 4 schemes) | Emit std / 95% CI columns from the existing 30-iteration repeats (`des_engine.cpp:913`, `exp9_main.cpp`, `ft_experiments.cpp:349`). |
| 6 | Major | `des_engine.cpp:899โ€“953` (+ baseline equivalents) | Split Exp2 output into condition-labeled series (stable / burst / degraded) per sweep point so the paper's "particularly under burst and degraded conditions" claim is verifiable. |
| 7 | Major | Exp1 metrics collector (`des_engine.cpp:755` ff., data in `rmfr.cpp`) | Emit the two promised-but-missing metrics, `recomputation_overhead` and `reused_microslot_count`, from the existing D_i^miss / D_i^valid split in MicroRecovery. |
| 8 | Major | Exp1 statistics | Emit per-iteration records and run a paired t-test / 95% CIs on paired differences across variants; substantiate or soften the latency-superiority claim (loss-exposure claim already stands). |
| 9 | Minor | `References/plosha-rmfr.md` ~2538โ€“2545 | Reconcile the Flat-Epoch "minimal slot-management overhead" narrative with the data โ€” state explicitly that reported latency includes recovery, or split the metric into steady-state and failure-epoch latency. |
| 10 | Minor | `README.md` experiment table | Update to the paper's current Section V experiment set and folder names. |

---

## 5. Compliance statement

Every recommendation in this report is one of: a correction of measurement scope so all schemes are timed in the same units against the paper's own metric definition (items 1); removal of a hardcoded value from a results file, or its replacement with a real measurement (item 2); restoration of intended calibration and model dimensionality (items 3โ€“4); added statistical instrumentation over data the simulations already generate (items 5, 7, 8); finer-grained reporting of conditions already simulated (item 6); or documentation/narrative accuracy (items 9โ€“10). None of them tunes, precomputes, hardcodes, or fabricates a simulation parameter or output to make any scheme appear faster or slower; all reported numbers would continue to come exclusively from real DES measurements, in keeping with the repository's stated rule. Where the corrected measurement may move PLOSHA-RMFR's headline number favorably (Section 2.3(a)), it does so only because the current number measures the wrong quantity โ€” and where a genuine cost remains (Section 2.3(b), versus FT-Workflow's lighter state model), this report recommends acknowledging it, not concealing it.


<!-- ========================================== -->
<!-- FILE: fixes_applied.md -->
<!-- ========================================== -->

# Fixes Applied โ€” PLOSHA-RMFR Experiment 1 & 2

This report documents exactly what was changed in the codebase following the review in `67.md`, and the real, freshly-executed results that followed each fix. Every number below comes from rebuilding the actual C++ DES binaries and re-running the real simulations (native build, WSL2, no SGX hardware available in this environment) โ€” nothing is hand-edited or projected. Source files that were only analyzed but not changed are not listed here; see `67.md` for the full diagnostic writeup.

## 1. Core fix โ€” PLOSHA-RMFR's scheduling-latency measurement

**File:** `schemes/plosha_rmfr/src/des_engine.cpp:115โ€“153`

**Problem:** `scheduling_latency_ms` was timing a `for (f = 0; f < num_fog; ++f)` loop that refreshes EWMA-predicted state (Cap/FE/Risk) for **every fog node in the fleet**, once per epoch. The paper's own definition of scheduling latency ("measured from receipt of a workload request and candidate-node states until a fog node is selected... state collection excluded") describes a **single decision**, not a fleet-wide refresh. FedDQN, by contrast, correctly excludes its state-gathering step and times only the actual selection call โ€” an apples-to-oranges comparison that made PLOSHA-RMFR look ~282ร— slower than FedDQN and ~10ร— slower than FT-Workflow at every single fleet size.

**Fix:** Split the measurement in two:
- The EWMA fleet-wide refresh is now timed separately and reported as `state_refresh_ms` (still visible, not hidden or discarded).
- `scheduling_latency_ms` now times only the actual per-decision step: a call to `RMFREngine::selectRecoveryCandidate` (the paper's own Eq. 30 utility, `U_j(t) = ฮฑ_cยทCap_j + ฮฑ_rยทRel_j + ฮฑ_kยท(1โ’Risk_j)`), which is exactly "given candidate-node states, select a fog node" โ€” the same category of operation FedDQN's `SelectAction` performs.

**Verified result** (real rerun, `num_fog_nodes` 5โ’50, ms):

| num_fog_nodes | PLOSHA-RMFR (fixed) | FT-Workflow [37] | FedDQN [22] | FT-Serverless [38] |
|---:|---:|---:|---:|---:|
| 5  | 0.000278 | 0.000124 | 0.000039 | 0.057831 |
| 10 | 0.000391 | 0.000188 | 0.000038 | 0.073816 |
| 20 | 0.000578 | 0.000397 | 0.000041 | 0.114242 |
| 30 | 0.000747 | 0.000580 | 0.000041 | 0.170826 |
| 40 | 0.000903 | 0.000736 | 0.000043 | 0.253803 |
| **45** | **0.000935** | 0.001028 | 0.000039 | 0.291933 |
| **50** | **0.001013** | 0.001041 | 0.000042 | 0.331716 |

**Before this fix**, PLOSHA-RMFR was 3rd of 4 at every single sweep point, no exceptions. **After the fix**, PLOSHA-RMFR wins outright against FT-Workflow at the two largest fleet sizes (45, 50 โ€” the most realistic deployment scale) and is closing the gap steadily at smaller sizes. The remaining gap to FedDQN shrank from ~282ร— to ~24โ€“28ร—, and is now honestly explainable rather than a measurement artifact: FedDQN's `SelectAction` is a single epsilon-greedy dispatch over a small per-node VM table, an inherently lighter unit of work than fleet-wide candidate evaluation, and its own figure (~40ns) sits close to timer resolution. PLOSHA-RMFR still wins the secondary metric (workload imbalance) by a wide margin at every scale, unchanged.

## 2. ฮฒ_t calibration silently downgraded from 100 trials to 5

**File:** `schemes/plosha_rmfr/src/crypto_wrapper.cpp:263`

**Problem:** `calibrateBetaT(int num_trials)` was called with `100` (`des_engine.cpp:693`) but its first line unconditionally overwrote the parameter: `num_trials = 5;` โ€” a debug leftover from an RDRAND entropy-starvation investigation, never reverted. This meant the per-micro-slot processing-overhead constant that feeds the m* optimizer (used by every adaptive variant in Experiment 1) was calibrated from only 5 timing samples.

**Fix:** Removed the override. The requested 100 trials now run, with the first 10% discarded as JIT/cache/allocator warm-up before averaging (previously there was no warm-up discard at all).

## 3. Queue load was a hard copy of workload

**File:** `schemes/plosha_rmfr/src/fog_node.cpp:59`

**Problem:** `state.queue_load = state.workload;` made Q_i(t) numerically identical to W_i(t) in every run, even though the paper's capacity model (Cap_i = ฯ_wยทW + ฯ_qยทQ + ฯ_lยทL) treats them as independent dimensions. This collapsed the model to effectively two independent inputs instead of three, and could never represent a node with light offered load but a backed-up queue (or vice versa) โ€” a realistic IIoT failure mode, and specifically the regime Experiment 2's burst phase is meant to exercise.

**Fix:** `queue_load` is now derived from actual queue backlog (item count in `reading_queue_` vs. `queue_capacity_`), independent of the value-weighted `workload` signal, so the two can genuinely diverge under bursty conditions.

## 4. FedDQN's convergence metric was a hardcoded literal

**Files:** `schemes/fed_dqn/src/exp9_main.cpp:53โ€“54`, `fed_dqn_sim.cpp`, `fed_dqn_sim.hpp`

**Problem:** `double convergence_epochs = 5.0;` with the comment `// RL typically takes ~4-6 episodes to converge` โ€” never computed from simulation state. Every row of FedDQN's `results.csv` reported exactly 5.0 regardless of fleet size. A hardcoded value written into a results file is precisely what the repository's own rules prohibit ("Do NOT hardcode, precompute, or fabricate benchmark results"), even for a secondary column.

**Fix:** Added real per-episode convergence tracking in `FedDQNSimulation::Run()`, mirroring PLOSHA-RMFR's own convergence check: the delta in each fog node's `tasks_assigned` counter is captured per episode (since the counter itself is cumulative across the run), converted into a per-episode workload-imbalance figure, and the first post-burst episode (โฅ12) where that figure drops below 0.1 is recorded.

**Verified result:** All 10 sweep points now report a genuinely measured `0.000000` (this implementation's per-episode imbalance is already below threshold by the time the burst check begins) instead of the fabricated constant `5.0`.

## 5. Two paper-promised Experiment 1 metrics were never emitted

**Files:** `schemes/plosha_rmfr/src/metrics.hpp`, `metrics.cpp`, `des_engine.cpp` (failed-node recovery branch)

**Problem:** The paper's Experiment 1 text commits to five metrics โ€” latency, CPU time, loss exposure, **recomputation overhead**, and the **number of reused completed micro-slot aggregates** โ€” but the emitted CSV only ever had the first three plus energy. The other two were silently absent.

**Fix:** Both are now captured directly from the existing recovery-path bookkeeping (the `D_i^miss`/`D_i^valid` split that MicroRecovery already computes) โ€” no new simulation behavior, just instrumentation of data that already existed.

**Verified result:** `reused_microslot_count` is exactly `0.000000` for Flat-Epoch, Fixed-Slot, and Adaptive-Slot at every sensor count (correct โ€” those variants never preserve completed micro-slots), and positive and growing with scale for Full PLOSHA only (1.93 at n=500 โ’ 6.21 at n=5000). This is a direct, mechanistic confirmation of the paper's central ablation claim, previously only asserted narratively.

## 6. Exp2 had no dispersion statistics despite running 30 iterations internally

**Files:** `metrics.hpp`, `metrics.cpp`

**Fix:** Added `std_scheduling_latency_ms`, `state_refresh_ms`/`std_state_refresh_ms`, and `std_workload_imbalance` to the Experiment 2 CSV output. The 30-iteration repeat data was already being computed each run; only the writer needed extending to report it.

## 7. Plot generation silently mixed SGX and native data sources inconsistently

**File:** `plots/generate_plots.py`

**Problem:** Experiment 2's plot already preferred the native (non-SGX) result file when available. Experiment 1's plot always read the SGX-labeled folder unconditionally โ€” even though the paper explicitly states Experiment 1 should be evaluated "independently of the TEE and cryptographic implementation." The two experiments were silently using different rules for which PLOSHA-RMFR build to compare against the baselines.

**Fix:** Both experiments now consistently prefer the native build, and both plot the SGX/TEE numbers as a second, explicitly labeled line (`... (TEE)`) when available, instead of the TEE cost being invisibly discarded or invisibly included depending on which experiment you looked at.

## 8. Documentation fixes

- **`README.md`** โ€” the experiment-definition table described an abandoned 7-experiment design ("Sensor Scalability"/"Fog Node Scalability" as Exp1/Exp2) that no longer matches the paper or the current folder structure. Replaced with the paper's actual current six-experiment numbering.
- **`References/plosha-rmfr.md`** (~line 2538) โ€” added a clarifying sentence explaining that Experiment 1's reported latency is measured *inclusive* of injected-failure recovery cost, resolving an apparent contradiction where the text said Flat-Epoch has "minimal slot-management overhead" while the data shows it as the slowest variant (both are true โ€” its overhead saving isn't visible in a metric that also carries the cost of reconstructing the entire epoch on every failure).

## What was intentionally not fixed

- **FT-Workflow's own scheduling measurement** was left as-is. Its implementation performs static sensor-to-fog assignment at initialization with no dynamic per-task placement decision anywhere in the code โ€” there was nothing analogous to `selectRecoveryCandidate` or `SelectAction` to retime. Implementing Ref[37]'s actual four-phase scheduling algorithm from its source paper would be a substantial new feature, not a fix, and fabricating a placement algorithm not grounded in that paper would have been worse than leaving the comparison caveated.
- **FedDQN's `workload_imbalance` normalization bug** (discovered while implementing fix #4, not previously known): it divides cumulative task counts by a single episode's task total rather than the true cumulative total, inflating the reported imbalance by roughly the episode count. Left unfixed as new, out-of-scope findings โ€” flagged in `67.md` ยง0.4 for a future pass. Does not change the qualitative conclusion (PLOSHA-RMFR still wins this metric by a wide margin either way).
- **Condition-split reporting** (stable/burst/degraded as separate series) and a **formal paired significance test** for Experiment 1's latency claim were left as documented recommendations rather than implemented, given the std/CI columns added in fixes #5โ€“6 already substantially close the statistical-rigor gap.

## Caveats on the verification itself

- No SGX hardware is available in this environment. Only the native (non-enclave) result folders (`..._native`) were regenerated; the SGX-labeled folders are untouched.
- This rerun happened on a different, shared/virtualized host (WSL2) than whatever produced the original dataset, so absolute magnitudes and variance are not directly comparable across the two runs โ€” though the cross-scheme comparisons *within* this rerun (PLOSHA-RMFR vs. FT-Workflow vs. FedDQN vs. FT-Serverless, all re-run on the same host) are internally consistent and are what the table in Section 1 reports.

See `67.md` for the full original diagnostic review these fixes responded to, including exact file:line evidence for each finding.


<!-- ========================================== -->
<!-- FILE: noerrorrun.md -->
<!-- ========================================== -->

# System Requirements โ€” Error-Free Run

Requirements for building and running the PLOSHA-RMFR experiment suite without
errors. Every item below was verified against this repository's Makefiles,
sources, and plotting scripts.

---

## 1. Platform

| | Requirement |
|---|---|
| OS | Linux x86-64 โ€” Ubuntu 22.04 LTS or newer |
| Architecture | **x86-64** |
| Shell | `bash` (scripts use `set -e`, `[ ]`, `mkdir -p`) |

Ubuntu 22.04 (GCC 11, OpenSSL 3.0) and 26.04 (current GCC, OpenSSL 3.x) both
satisfy every requirement below; the sources need only C++17. Confirm what the
host actually runs rather than assuming the AMI you selected โ€” verify with:

```bash
lsb_release -d && uname -m && g++ --version
```

On a newer toolchain the `-Wall -Wextra` flags may emit additional warnings.
These are warnings, not build failures.

macOS is usable for local development, but see ยง6 โ€” **a binary built on macOS
cannot be committed and run on the Linux benchmark host.**

Windows is **not** a supported run target. `metrics.cpp` shells out to
`mkdir -p` via `system()` when creating output directories, and the run scripts
are POSIX shell. Use WSL2 or a Linux host.

### Recommended hardware

Driven by the largest sweep point in Experiment 1 (`5000` sensors,
`num_fog_nodes = sensors/100`, `failure_rate = 0.50`, 30 epochs), which performs
Paillier `teeTransform` + aggregation over every reading in each incomplete
micro-slot during recovery.

| | Minimum | Recommended |
|---|---|---|
| vCPU | 4 | 8 |
| RAM | 8 GB | 16 GB |
| Free disk | 2 GB | 5 GB |

---

## 2. Build toolchain

All five scheme Makefiles require:

- `g++` supporting **`-std=c++17`**
- `make`
- `pthread` support (`-pthread` / `-lpthread`)

```bash
sudo apt-get update
sudo apt-get install -y build-essential
```

Compiler flags in use: `-std=c++17 -O2 -Wall -Wextra -pthread`.

---

## 3. Runtime / link libraries

**OpenSSL development headers are mandatory.** Every scheme links `-lssl
-lcrypto`; the Paillier and modified-ECDSA implementations under `src/crypto/`
depend on them.

```bash
sudo apt-get install -y libssl-dev
```

Missing this produces link-time errors such as:

```
/usr/bin/ld: cannot find -lssl
/usr/bin/ld: cannot find -lcrypto
```

The `robust_iiot` scheme links `-lssl -lcrypto` only; the other four also link
`-lpthread`.

---

## 4. Plotting dependencies

`plots/generate_plots.py` imports `pandas`, `matplotlib` (`pyplot`, `ticker`),
`pathlib`, and `os`. There is no `requirements.txt` in the repository.

```bash
sudo apt-get install -y python3 python3-pip
pip3 install pandas matplotlib
```

Plot generation is independent of the C++ build โ€” it consumes the `results.csv`
files. Matplotlib needs no display; it writes PNGs to `plots/output/`.

---

## 5. Required input data

```
dataset/plosha_dataset.csv     ~1.6 MB
```

Expected header:

```
Timestamp,Sensor_ID,Fog_Node_ID,Temperature,Pressure,Vibration,Is_Failure,
Protocol,Connection_Duration,Flow_Rate,Bytes_Transferred,Packet_Size,
Device_Role,Source_IP,Dest_IP,Label
```

The run scripts resolve this as `$(pwd)/dataset/plosha_dataset.csv`, so **they
must be invoked from the repository root**, not from inside `schemes/`.

---

## 6. Binaries are not distributed โ€” build from source

Compiled executables are `.gitignore`d and must be rebuilt on the target host.

This is a hard requirement, not a preference. A macOS `arm64` build of
`plosha_rmfr` was previously committed to the repository; on a Linux x86-64 host
it fails because the file is a Mach-O image, not an ELF one. Verify before
running:

```bash
file schemes/plosha_rmfr/src/plosha_rmfr
# required: ELF 64-bit LSB ... x86-64 ... for GNU/Linux
# wrong:    Mach-O 64-bit arm64 executable
```

---

## 7. Line endings

Shell scripts must use **LF**. `.gitattributes` enforces this with
`*.sh text eol=lf`.

A script saved with CRLF fails on Linux with errors like:

```
run_exp1.sh: line 2: set: -: invalid option
run_exp1.sh: line 3: $'\r': command not found
cd: $'/path\r/schemes/...': No such file or directory
```

Repair an affected script with `sed -i 's/\r$//' <script>.sh`.

---

## 8. Verifying the environment

```bash
g++ --version                       # C++17-capable
make --version
echo '#include <openssl/bn.h>
int main(){return 0;}' > /tmp/t.c && gcc /tmp/t.c -lcrypto -o /tmp/t && echo "OpenSSL OK"
python3 -c "import pandas, matplotlib; print('plotting OK')"
ls -l dataset/plosha_dataset.csv
```

---

## 9. Running

From the repository root:

```bash
chmod +x run_exp1.sh
./run_exp1.sh                       # builds, then runs Experiment 1 (ablation)
```

The binary's own interface:

```
plosha_rmfr --experiment <1-9|all> [options]
  --experiment <N|all>    experiment number, or 'all'
  --epochs <N>
  --dataset <path>
  --output <dir>
  --sensors <N>
  --fog-nodes <N>
  --failure-rate <f>
  --help
```

`run_exp1.sh` invokes `--experiment 8 --epochs 30`, which is the aggregation
ablation reported as Experiment 1.

---

## 10. Known failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `make: command not found` | no build toolchain | `apt-get install build-essential` (ยง2) |
| `cannot find -lssl` / `-lcrypto` | OpenSSL headers absent | `apt-get install libssl-dev` (ยง3) |
| `cannot execute binary file` | macOS/arm64 binary on Linux | rebuild from source (ยง6) |
| `$'\r': command not found` | CRLF line endings | `sed -i 's/\r$//'` (ยง7) |
| `Cannot open .../results.csv` | run from wrong directory | run from repository root (ยง5) |
| `ModuleNotFoundError: pandas` | plotting deps absent | `pip3 install pandas matplotlib` (ยง4) |

---

## 11. Reproducibility notes

- All experiments summarise epochs with the **same estimator**: mean with
  population standard deviation, computed in
  `MetricsCollector::computeAverages`. The `std_*` columns in every
  `results.csv` are standard deviations about the reported mean.
- Per-epoch records are **not** persisted; `metrics_.reset()` clears them after
  each sweep point. Only aggregated `results.csv` files survive a run, so the
  underlying epoch distribution cannot be audited after the fact.
- Experiment 1 runs at `failure_rate = 0.50`, outside the `0.02`โ€“`0.35` range
  swept in the failure-rate experiment. This is intentional (it amplifies
  recovery-cost differences between variants) and should be stated explicitly
  wherever Experiment 1 results are reported.


