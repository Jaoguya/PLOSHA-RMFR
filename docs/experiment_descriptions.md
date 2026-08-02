# Experiment Descriptions (paper-ready)

Covers the two experiments affected by the heterogeneity work: Experiment 2
(scheduling efficiency, now with heterogeneous fog capability) and Experiment 7
(workload imbalance under heterogeneous capability). Numbers are as measured on
the native x86-64 run; see each experiment's `run_metadata.txt` for the exact
commit, host, and parameters that produced them.

---

## Modelling heterogeneous fog capability

Both experiments instantiate the three conditions the scheme's evaluation
specifies — heterogeneous **processing capacity**, **queue occupancy**, and
**communication latency** — using a single deterministic model parameterised by
a heterogeneity ratio *r* (strongest : weakest node):

- **Queue capacity** is spread linearly across the *F* fog nodes, from
  `mean · 2/(1+r)` up to `mean · 2r/(1+r)`. The **total system capacity is held
  constant** and equal to the homogeneous case, so a heterogeneous run differs
  only in how capability is *distributed*, never in how much capability exists.
  Without this constraint, a comparison across *r* would confound distribution
  with aggregate provisioning.
- **Processing-latency headroom** scales inversely with capacity: a weaker node
  is correspondingly slower.
- **Communication latency** is spread over the same ratio *r* about a 5 ms mean,
  but indexed by a **deterministic permutation of the node identifier**
  (stride 7) rather than by the node index itself. This is deliberate: if link
  latency were monotonic in the same index as capacity, the two dimensions would
  be perfectly collinear — the fastest node would always also be the closest —
  and their effects could not be separated. The permutation yields a measured
  capacity/latency correlation of **+0.07**, i.e. effectively independent, while
  remaining fully deterministic and seed-independent.

Communication latency is added to processing latency when forming the node
latency term *L_i*, so the existing scheduling utility
*U_j = α_C·capacity + α_R·reliability + α_K·(1 − risk)* consumes it through the
established prediction path without any change to its weights.

At *r* = 1 the model degenerates exactly to the homogeneous configuration, so
every experiment that does not vary capability is unaffected.

---

## Experiment 2 — Scheduling Efficiency

**Objective.** Measure how effectively each scheme distributes workload across
heterogeneous fog nodes as the fleet grows.

**Independent variable.** Number of fog nodes, 5 → 50 in steps of 5.

**Schemes.** PLOSHA-RMFR, FedDQN [22], FT-Workflow [37], FT-Serverless [38].
(Robust IIoT [24] is an encrypted-aggregation scheme, not a scheduler, and does
not participate.)

**Conditions.** Sensors scale with the fleet at 100 per fog node (500 → 5000),
so per-node offered load is constant across the sweep. Heterogeneity ratio
*r* = 5. Failure rate is 0. The capacity-aware scheduling decision is applied to
routing (`apply_scheduling_decision = true`), so the scheduler genuinely
redistributes load rather than computing a decision and discarding it.
Each configuration runs 30 epochs × 30 iterations.
Three workload phases are applied identically to every scheme: stable traffic,
a ×1.5 reporting-rate burst from epoch 12, and degradation of 20 % of nodes from
epoch 21. The offered load additionally contains a deliberate hotspot, with 25 %
of sensors homed to a single node.

**Secondary metric.** Scheduling latency — wall-clock time from the availability of
candidate-node state until a node is selected. Per each scheme's own definition,
this excludes state collection, offline training, and execution.

**Result (SUPERSEDED — see note).** The figures below were measured at r = 4 with
the scheduling decision discarded, and predate both the ratio change to 5 and
`apply_scheduling_decision = true`. Experiment 2 must be re-run; imbalance is now
the primary metric and is not reported here at all. Scheduling latency rises with fleet size for every scheme
except FedDQN. PLOSHA-RMFR is lowest across the sweep (0.000158 ms at 5 nodes to
0.001055 ms at 50), with FT-Workflow closest above it (0.000256 → 0.001295 ms).
FedDQN is approximately flat (0.003932 → 0.004230 ms) because its decision cost
is dominated by a fixed-size neural forward pass that does not grow with the
number of fog nodes. FT-Serverless is roughly two orders of magnitude higher
(0.041553 → 0.360709 ms), reflecting a substantially heavier decision: DAG
function placement with a binary search over standby replica counts and
shortest-path computation.

**Interpretation and limits.** The schemes' decisions are not equal units of
work — PLOSHA performs an O(F) utility scan over candidate nodes, whereas
FT-Serverless performs full function placement. Each is timed according to its
own reference's definition of a scheduling decision, so the comparison is
faithful per-scheme but the absolute gaps partly reflect differing decision
scope rather than implementation efficiency alone. Node degradation is likewise
applied in each scheme's native capability model (latency, capacity factor, or
memory), so its severity is not numerically identical across schemes.

**Primary metric — workload imbalance, not decision latency.** "Scheduling
efficiency" is how *well* the scheduler distributes work across heterogeneous
nodes, not how *fast* it reaches a decision; a scheduler that decides in
nanoseconds while overloading the weakest node is not efficient. The primary
metric is therefore capacity-normalised workload imbalance *I_W* (defined under
Experiment 7 below), with decision latency reported as a secondary property.

For that metric to be meaningful the scheduler must actually route traffic, so
Experiment 2 sets `apply_scheduling_decision = true`: PLOSHA's capacity-aware
selection is applied to sensor placement rather than computed and discarded.
The baselines route per their own designs — FedDQN selects a virtual machine
*within* the node a task arrives at, FT-Workflow uses a static sensor-to-node
map, and FT-Serverless re-places DAG functions across cloudlets — so the
comparison measures each scheme's own distribution behaviour under identical
heterogeneous conditions.

---

## Experiment 7 — Workload Imbalance under Heterogeneous Capability

**Objective.** Test whether capacity-aware scheduling reduces workload imbalance
when fog nodes differ in capability, and how that advantage scales with the
degree of heterogeneity.

**Independent variable.** Heterogeneity ratio *r* ∈ {1, 2, 4, 6, 8, 10}
(strongest : weakest node capacity), at a fixed fleet of 20 fog nodes and 2000
sensors.

**Variants (internal to PLOSHA-RMFR).**
- *Static assignment* — sensors remain with their initial node; the computed
  scheduling decision is not applied to routing.
- *Capacity-aware* — the scheduling decision is applied: at each epoch a slice
  (10 %) of the source node's sensors is re-associated toward a better-suited
  node, and only when the source is genuinely more loaded **per unit of
  capacity** than the target, which prevents oscillation.

**Conditions.** 30 epochs × 10 iterations per point, zero failure rate, and the
same 25 % hotspot as Experiment 2. Total system capacity is constant across all
values of *r*.

**Metric.** Capacity-normalised workload imbalance,
*I_W = std(W_i) / (mean(W_i) + ε)*, where *W_i* is node *i*'s **share of load
divided by its share of capacity**. Under this definition *I_W* = 0 means load
is allocated exactly in proportion to capability. This normalisation is
essential: with unequal nodes, an equal split is *not* balanced, and an
unnormalised count-based metric would penalise a correctly capacity-proportional
allocation.

**Result (measured).**

| *r* | Static assignment | Capacity-aware | Reduction |
|-----|------------------|----------------|-----------|
| 1×  | 0.918 | 0.249 | 73 % |
| 2×  | 1.482 | 0.377 | 75 % |
| 4×  | 2.584 | 0.775 | 70 % |
| 6×  | 3.778 | 1.200 | 68 % |
| 8×  | 4.866 | 1.584 | 67 % |
| 10× | 5.993 | 1.970 | 67 % |

Static assignment degrades approximately linearly in *r* (slope ≈ 0.56 per
ratio step), since an equal split over increasingly unequal nodes departs
further from capacity-proportional allocation. Capacity-aware scheduling grows
at roughly one third that rate (≈ 0.19 per step), holding a consistent 3–4×
advantage across the full range and widening in absolute terms from 0.67 at
*r* = 1 to 4.02 at *r* = 10.

**Interpretation and limits.**
- Part of the advantage is not attributable to heterogeneity: at *r* = 1, with
  identical nodes, capacity-aware scheduling already reduces imbalance by 73 %
  because it also relieves the 25 % hotspot. Separating the hotspot effect from
  the capability effect would require a third variant.
- Dispersion is substantial for the capacity-aware variant (standard deviation
  up to ±1.55 at *r* = 10, versus ≈ 0 for static) because re-association is
  gradual, so early epochs are still converging. Reported values are means over
  all epochs and iterations, not steady-state values.
- Adding communication-latency heterogeneity changed the outcome by less than
  0.5 % (e.g. 1.969 → 1.970 at *r* = 10), and left static assignment bit-identical
  — expected, since nothing is re-routed in that variant and link speed cannot
  influence placement. The imbalance advantage is therefore robust to the
  addition of the third heterogeneity dimension.
- This is an internal comparison within PLOSHA-RMFR (capacity-aware scheduling
  versus static assignment). Extending it to a cross-scheme comparison would
  require adding equivalent capability-aware re-routing and a common imbalance
  definition to the baselines.

**Disclosed parameter choices.** The evaluation specifies heterogeneous
capacity, queue occupancy, and communication latency as conditions but does not
state their magnitudes. The ratio *r* = 5 used in Experiment 2, the sweep range
1×–10× in Experiment 7, the 5 ms mean communication latency, and the 10 %
per-epoch migration slice are our choices, recorded in `run_metadata.txt`
alongside every result set.
