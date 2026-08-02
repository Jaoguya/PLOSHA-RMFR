# Experiment Condition Test

Verification that every scheme in a comparative experiment faces **identical
conditions** and is **measured identically**. Run these checks after any change
to a scheme's node model, metric, or experiment configuration.

Branch: `main` (merged from `KuyKeaw`) · Last verified: 2026-07-28

---

## 1. Conditions that must be identical across all four schemes (Exp2)

| Condition | Required value | PLOSHA | Ref[22] FedDQN | Ref[37] FT-Workflow | Ref[38] FT-Serverless |
|---|---|---|---|---|---|
| Fog-node sweep | 5 → 50 step 5 | ✅ | ✅ | ✅ | ✅ |
| Sensors | `fog × 100` | ✅ | ✅ | ✅ | ✅ (set in `run_exp2.sh`) |
| Epochs / iterations | 30 × 30 | ✅ | ✅ | ✅ | ✅ |
| Failure rate | 0.0 | ✅ | ✅ | ✅ | ✅ |
| Workload burst | ×1.5 from epoch 12 | ✅ | ✅ | ✅ | ✅ |
| Node degradation | 20 % of nodes from epoch 21 | ✅ | ✅ | ✅ | ✅ |
| Hotspot | 25 % of sensors → node 0 | ✅ | ✅ | ✅ | ✅ |
| **Heterogeneity ratio** | **5.0** | ✅ | ✅ | ✅ | ✅ (native random capability) |
| **Scheduling decision applied to routing** | `apply_scheduling_decision = true` | ✅ | n/a (VM-level) | n/a (static map) | n/a (own placement) |
| Seed | fixed | ✅ | ✅ | ✅ | ✅ (12345) |
| Execution | native, no SGX | ✅ | ✅ | ✅ | ✅ |

### Known non-identical (inherent, must be disclosed)

- **Degradation severity** is applied in each scheme's native capability model:
  PLOSHA and FT-Workflow use an absolute 200 ms latency penalty, FedDQN a ×3
  capacity factor, FT-Serverless halves memory. The *timing* is identical; the
  *magnitude* is not numerically equivalent and cannot be made so without
  inventing a conversion between the models.
- **Scheduling-decision scope** differs by design: PLOSHA performs an O(F)
  utility scan, FedDQN a fixed-size DNN forward pass, FT-Serverless full DAG
  placement with standby search. Each is timed per its own reference's
  definition, so absolute gaps partly reflect decision scope, not efficiency.
- **FT-Serverless heterogeneity** comes from its own random cloudlet
  initialisation (memory, alpha, cold-start, link delay), not from the shared
  `heterogeneity_ratio`. It is heterogeneous, but not by the same construction.

---

## 2. Heterogeneity model — must be identical in PLOSHA, FT-Workflow, FedDQN

For ratio `r` over `F` nodes, node `i`:

```
frac   = i / (F - 1)
scale  = (2 / (1 + r)) * (1 + frac * (r - 1))     // capacity multiplier
```

Invariants:

| Invariant | Why it matters |
|---|---|
| `r = 1.0` ⇒ every node identical | experiments that do not model heterogeneity stay bit-identical |
| strongest / weakest = `r` | the ratio means what it says |
| **total capacity constant vs. homogeneous** | otherwise a heterogeneous run merely has more/less aggregate capacity and the comparison confounds distribution with provisioning |
| processing-latency headroom ∝ `1 / scale` | a weaker node is genuinely slower |
| comm latency spread by a **permuted** index `(i*7+3) % F` | must NOT be collinear with capacity, or the fastest node is always also the closest and the two dimensions cannot be separated |

Measured on the model (F = 20, r = 4, the value verified at the time): capacity
40→160 (ratio 4.00), comm latency 3.12→12.50 ms (ratio 4.00), total capacity
1991 vs 2000 homogeneous (0.45 %), **corr(capacity, comm latency) = +0.07**.
The production value is now r = 5; the invariants are ratio-independent.

---

## 3. Metric definitions — must be identical

**Workload imbalance is Experiment 2's PRIMARY metric.** "Scheduling efficiency"
is how *well* a scheduler distributes work across heterogeneous nodes, not how
*fast* it decides — a scheduler that decides in nanoseconds while overloading the
weakest node is not efficient. Decision latency is a secondary property.

For the metric to be meaningful the scheduler must actually route traffic, so
Exp2 sets `apply_scheduling_decision = true` in PLOSHA: the capacity-aware
selection is applied to sensor placement instead of computed and discarded. If
that flag is off, `I_W` degenerates to the offered input distribution restated
and cannot discriminate between schemes.

**Workload imbalance** (capacity-normalised coefficient of variation):

```
W_i  = (node i's share of load) / (node i's share of capacity)
I_W  = sqrt( (1/F) * SUM (W_i - W_bar)^2 ) / (W_bar + eps)
```

`I_W = 0` ⇔ load allocated exactly in proportion to capability. An equal split
across unequal nodes is **not** balanced.

| Scheme | Load counted as | Trap avoided |
|---|---|---|
| PLOSHA | assigned readings at submission | not drained queues (would give a constant `1/F`) |
| FT-Workflow | assigned readings | not capped queue occupancy (cap truncates the hotspot) |
| FedDQN | **assigned + rejected** tasks | rejected tasks are the hotspot's real burden; counting only accepted hides it |
| FT-Serverless | functions on `active_cloudlet` | post-placement, not origin distribution (origin is identical for all schemes and blind to the algorithm) |

**Convergence** uses a flat threshold `I_W < 0.1` in all schemes. A
`0.1 × num_fog` scaling is invalid: at 50 nodes it is 5.0 against a maximum
`I_W` of ≈1.6, so the test is trivially true.

**Scheduling latency** is timed around the decision only — excludes state
collection, offline training, and execution.

---

## 4. Automated checks

```bash
# A. heterogeneity ratio identical in the three code-configured schemes
grep -ohE "heterogeneity_ratio = 5\.0|EXP9_HETEROGENEITY_RATIO = 5\.0" \
  schemes/plosha_rmfr/src/des_engine.cpp \
  schemes/fault_tolerant_workflow/src/ft_engine.cpp \
  schemes/fed_dqn/src/exp9_main.cpp            # expect 3 lines

# B. identical spread formula present in all three
grep -c '2.0 / (1.0 + ' schemes/plosha_rmfr/src/des_engine.cpp \
  schemes/fault_tolerant_workflow/src/ft_engine.cpp \
  schemes/fed_dqn/src/fed_dqn_sim.cpp          # expect >=1 each

# C. FedDQN counts rejected tasks in imbalance
grep -c "tasks_rejected) / total_tasks" schemes/fed_dqn/src/fed_dqn_sim.cpp   # expect 2

# D. convergence threshold is flat 0.1 everywhere
grep -c "workload_imbalance < 0.1)" schemes/plosha_rmfr/src/des_engine.cpp \
  schemes/fault_tolerant_workflow/src/ft_engine.cpp
grep -c "episode_imbalance < 0.1)" schemes/fed_dqn/src/fed_dqn_sim.cpp

# E. communication latency modelled in PLOSHA + FT-Workflow
grep -c comm_latency_ms_ schemes/plosha_rmfr/src/fog_node.cpp \
  schemes/fault_tolerant_workflow/src/fog_node.cpp

# F. FT-Serverless uses the shared hotspot and post-placement load
grep -c "current_sensors \* 0.25" schemes/ft_serverless_edge/src/ft_experiments.cpp
grep -c "fn.active_cloudlet"      schemes/ft_serverless_edge/src/ft_experiments.cpp

# G. real DQN, no cost injection
grep -c "dqn.forward\|dqn.getBestAction\|dqn.train" schemes/fed_dqn/src/fed_dqn_sim.cpp
grep -rniE "dqn_forward_accum|dummy forward|artificially inflate" schemes/*/src/*   # expect none

# H. scripts parse
for s in run_exp1.sh run_exp2.sh run_exp_common.sh; do bash -n $s; done
```

### Result of last run (branch `main`)

| Check | Expected | Got |
|---|---|---|
| A ratio 5.0 × 3 | 3 lines | ✅ 3 |
| B spread formula | ≥1 each | ✅ 2 / 2 / 1 |
| C rejected counted | 2 | ✅ 2 |
| D flat 0.1 × 3 | 1 / 1 / 1 | ✅ 1 / 1 / 1 |
| E comm latency | ≥1 each | ✅ 3 / 2 |
| F hotspot + placement | ≥1 each | ✅ 1 / 3 |
| G real DQN, no fake code | ≥1, none | ✅ 2, none |
| H scripts parse | all OK | ✅ 3/3 |
| FedDQN `Configure` arity | decl == def | ✅ 1 / 1 |

**Compilation: VERIFIED.** All four schemes build clean on the AWS instance
(2026-07-28). One real defect was caught only by compiling: the merge left
`cpu_dist(rng_)` in FedDQN while the other side had replaced that distribution
with `mean_cpu`, so the file did not compile. Static checks alone would not have
found it -- always build before trusting a result:

```bash
cd schemes/fed_dqn/src && make exp9_scheduling_efficiency
cd ../../plosha_rmfr/src && make
cd ../../fault_tolerant_workflow/src && make
cd ../../ft_serverless_edge/src && make
```

---

## 5. Disclosed parameter choices

The paper states these conditions but no magnitudes. The following are **our
choices**, recorded in each `run_metadata.txt`:

| Parameter | Value | Used by |
|---|---|---|
| Heterogeneity ratio (Exp2) | 5.0 | PLOSHA, FT-Workflow, FedDQN |
| Heterogeneity sweep (Exp7) | 1× – 10× | PLOSHA |
| Mean communication latency | 5 ms | PLOSHA, FT-Workflow |
| Comm-latency index permutation | `(i*7+3) % F` | PLOSHA, FT-Workflow |
| Migration slice (Exp7) | 10 % of source node per epoch | PLOSHA capacity-aware |
| Exp1 ablation failure rate | 0.50 | PLOSHA |

> Ratio resolved to **5.0** across all schemes on merge, matching `newest.md`.

