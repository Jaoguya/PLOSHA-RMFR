# Overleaf Version Comparison

`References/plosha-rmfr.md` (**OLD**) vs `References/GuyPLOSHA` (**NEW**)

> **Status note (2026-07-28, after the KuyKeaw→main merge).** Sections 3.1–3.5
> below were written when the code used `heterogeneity_ratio = 4.0` and Exp2
> reported scheduling latency as its primary metric. Both have since changed:
>
> - **Ratio is now 5.0** in PLOSHA, FT-Workflow and FedDQN, so §3 item 1's
>   "code is 4.0" mismatch is **resolved** — `newest.md` and the code agree.
> - **Exp2's primary metric is now workload imbalance**, not scheduling
>   latency (scheduling *efficiency* = how well work is distributed, not how
>   fast a decision is made). `apply_scheduling_decision = true` was added so
>   PLOSHA actually re-routes and the metric can discriminate.
> - Consequently §3.4's stale-numbers table is still valid as a warning but its
>   specific figures are doubly outdated: Exp2 is being re-run at ratio 5 with
>   imbalance as the reported quantity.
>
> The §3.1 title/body inconsistency and §3.5 delegation clause are **still
> open** in the paper. §4 (Experiment 7 absent from both versions) also stands.

| | OLD | NEW |
|---|---|---|
| File | `plosha-rmfr.md` | `GuyPLOSHA` |
| Size | 113,642 bytes | 115,371 bytes |
| Format | Markdown (pandoc-converted) | Raw LaTeX (IEEEtran) |
| Modified | 2026-07-21 | 2026-07-28 |

**Structure is identical in both**: same sections (Introduction → Related Work →
Proposed Scheme with Phases I–V → Security Analysis → Performance Evaluation →
Conclusion) and the same six experiments. No experiment was added or removed.

> Note: no code has been changed based on this comparison. This document is for
> decision-making only.

---

## 1. Summary of what changed

| Area | Change | Effect on our implementation |
|---|---|---|
| Exp2 title | now "Scheduling Efficiency **and Workload Imbalance**" | ⚠️ title/body mismatch (see §3.1) |
| Exp2 imbalance metric | **removed entirely** ($I_W$ equation deleted) | ✅ matches our decision to drop panel (b) |
| Exp2 figure | now `exp2_scheduling_efficiency_line.png` | ✅ matches our line-graph file |
| Exp2 workload conditions | **removed** (burst / degradation / stable no longer described) | ❌ code still implements them (§3.3) |
| Exp2 FedDQN behaviour | now explicitly "all schemes **except FedDQN**" | ✅ matches our real-DQN result |
| Exp2 heterogeneity | "each node **initialized under** heterogeneous … profiles" | ✅ matches our new implementation |
| Exp1 failure rate | **newly stated as 10 %** | ❌ code uses 50 % (§3.2) |
| Exp1 fog scaling | newly stated "100 sensors per fog node" | ✅ matches code |
| Exp2 numbers | quantitative results added | ❌ stale, from pre-heterogeneity run (§3.4) |
| Phase IV delegation | **unchanged** | ⚠️ still conflicts with Exp7 (§3.5) |
| Exp3, Exp4, Exp5, Exp6 | essentially unchanged | — |

---

## 2. Resolved by the NEW version

These were real contradictions against the OLD paper and are now **fixed in the
paper text**:

1. **Imbalance claim dropped.** OLD asserted PLOSHA's "predictive load-sharing …
   resulting in lower workload imbalance" and defined $I_W$. NEW deletes both the
   equation and the claim. This matches the measured reality: in Exp2 no scheme
   re-routes work, so $I_W$ only restates the offered input distribution.
2. **FedDQN's flat latency is now explained, not contradicted.** OLD said latency
   "increases … for all schemes". NEW says "all schemes **except FedDQN**, which
   maintains a near-constant latency … due to its pre-trained DQN policy producing
   fixed-cost inference regardless of the scheduling space." Consistent with the
   real `DQNNetwork` implementation.
3. **PLOSHA is no longer claimed to be lower than every baseline on everything.**
   NEW confines the claim to scheduling latency, which the data supports.
4. **Figure switched to the line form** we generated (`..._line.png`).
5. **Heterogeneity wording made concrete** — "each node initialized under
   heterogeneous processing capacity, queue occupancy, and communication latency
   profiles" — which is exactly the model now implemented in both PLOSHA and
   FT-Workflow.

---

## 3. Still contradicting (needs a decision)

### 3.1 Exp2 title vs. its own body
- **Title:** "Experiment 2: Scheduling Efficiency **and Workload Imbalance**"
- **Body:** reports scheduling latency only; the $I_W$ equation is gone
  (`grep I_W` → OLD = 1 occurrence, NEW = 0); the only surviving mention of
  "workload imbalance" in the whole NEW paper **is the title itself**.
- **Figure caption:** "Scheduling latency under increasing numbers of
  heterogeneous fog nodes (log scale)" — no imbalance.
- → Internal inconsistency in the paper. Suggest deleting "and Workload
  Imbalance" from the title.

### 3.2 Exp1 failure rate: paper 10 % vs code 50 %
- **OLD:** did not state a failure rate for Exp1.
- **NEW:** "A failure rate of 10 % is applied to stress-test recovery behavior, as
  lower failure rates produce insufficient recovery events to meaningfully
  differentiate the ablation variants."
- **Code:** `EXP1_ABLATION_FAILURE_RATE = 0.50` (50 %).
- → **Direct numeric contradiction.** Either the code returns to 10 % (and Exp1
  is re-run), or the paper is corrected to 50 %. Note the current Exp1
  `results.csv` and Graph 1 were produced at **50 %**.

### 3.3 Exp2 workload conditions removed from paper but still in code
- **OLD:** "Three workload conditions are considered: stable traffic, a 50 %
  reporting-rate burst, and node degradation in which 20 % of fog nodes
  experience increasing queue occupancy and latency."
- **NEW:** that sentence is **deleted** (no mention of burst, degradation, or
  stable traffic anywhere in Exp2).
- **Code:** still applies ×1.5 burst from epoch 12 and degrades 20 % of nodes
  from epoch 21, in all four schemes.
- → The experiment does more than the paper now describes. Either restore the
  sentence (accurate, and it was true) or remove the behaviour from the code.
  Restoring the text is the lower-risk option — the conditions are implemented
  identically across all four schemes and make the setup stronger, not weaker.

### 3.4 Exp2 quantitative results are stale
NEW adds specific numbers that do **not** match the current `results.csv`
(measured after heterogeneity was implemented):

| Scheme | NEW paper | Measured now | Verdict |
|---|---|---|---|
| PLOSHA-RMFR | 0.16 → **0.72** µs | 0.16 → **1.05** µs | ❌ understates by ~32 % |
| Ref[37] FT-Workflow | 0.25 → 1.3 µs | 0.26 → 1.29 µs | ✅ matches |
| Ref[22] FedDQN | ~**3.5** µs | **3.93 – 4.23** µs | ❌ |
| Ref[38] FT-Serverless | **37 → 330** µs | **41.6 → 360.7** µs | ❌ |

Adding heterogeneity raised every latency, so three of four figures in the prose
are from an earlier (homogeneous) run. **The paper's numbers must be regenerated
from the current results before submission**, or a reviewer comparing text to
artifact will find mismatches.

### 3.5 Phase IV delegation clause — unchanged
Both versions state (NEW line 990, identical wording):

> "The delegated fog node then performs shadow aggregation in subsequent epochs
> and prepares to absorb future workload **without immediate sensor
> reassociation**."

Our Exp7 `capacity_aware` variant **does** reassociate sensors (10 % per epoch).
It is therefore more aggressive than Phase IV describes. Not fatal — Exp7 is
framed as *capacity-aware scheduling*, not as *delegation* — but if Exp7 enters
the paper the distinction must be stated explicitly.

---

## 4. Experiment 7 is absent from both versions

Neither version contains Experiment 7. NEW references exactly six figures:

```
System Model.png, exp1_ablation_aggregation.png,
exp2_scheduling_efficiency_line.png, exp3_failure_rate.png,
exp4_loss_exposure.png, exp5_recovery_comm.png, exp6_aflto.png
```

The heterogeneity result — capacity-aware scheduling holding ≈ 3× lower
capacity-normalised imbalance than static assignment across a 1×–10× sweep — is
currently **unpublished**. It is also the only experiment in the benchmark where
any scheme actually re-balances load.

**Note on figure filenames:** the paper expects `exp*_*.png`, while the plotting
script emits `graph*_*.png` (e.g. `graph1_ablation_aggregation.png`). Also
`exp6_aflto.png` vs our `graph6_aflto_ablation.png`. Files must be renamed when
inserted into Overleaf, or the plot script's output names changed.

---

## 5. Decisions required

| # | Question | Options |
|---|---|---|
| 1 | Which version is authoritative? | NEW appears to be the current Overleaf draft, but please confirm — you flagged it "maybe the wrong version" |
| 2 | Exp1 failure rate | (a) change code to 10 % and re-run Exp1, or (b) correct the paper to 50 % |
| 3 | Exp2 workload conditions | (a) restore burst/degradation sentence to the paper (recommended — it is what the code does), or (b) strip the behaviour from the code |
| 4 | Exp2 title | delete "and Workload Imbalance"? |
| 5 | Exp2 numbers | regenerate the prose figures from the current `results.csv`? |
| 6 | Experiment 7 | add to the paper, or leave unpublished? |
| 7 | Figure naming | rename on insert, or change the plot script's output filenames? |

No code will be modified until you decide.
