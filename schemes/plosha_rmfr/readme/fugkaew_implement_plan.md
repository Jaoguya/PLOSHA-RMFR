# PLOSHA-RMFR: Reviewer-Grade Measurement & Mitigation Plan (Gramine Edition)

As a journal reviewer, I have analyzed every metric in PLOSHA-RMFR and classified them into three tiers. For each unmeasurable metric, I provide a **mitigation approach** that a reviewer would accept as methodologically sound.

> [!IMPORTANT]
> **Architectural Change: Gramine (Library OS) replaces the raw Intel SGX SDK.**
> Instead of manually partitioning code into trusted/untrusted halves with ECALL/OCALL interfaces and EDL files, the entire C++ application runs **unmodified** inside an SGX enclave via [Gramine](https://gramineproject.io). This dramatically simplifies development while preserving all security guarantees.

## Why Gramine?

| Concern | Raw SGX SDK | Gramine |
|---------|-------------|---------|
| Code partitioning | Manual trusted/untrusted split, EDL files | **None** — entire binary runs inside enclave |
| Crypto libraries | Must use `sgx_tcrypto` or port libraries | **Standard OpenSSL & GMP** via manifest |
| Attestation | Manual DCAP/EPID integration | **Built-in RA-TLS** with `ra_tls_attest.so` |
| Sealed storage | `sgx_seal_data()` / `sgx_unseal_data()` API | **Encrypted FS mounts** (`type = "encrypted"`) |
| Porting effort | High — rewrite everything for enclave model | **Minimal** — write normal C++, add a manifest |
| Debugging | Limited (sgx-gdb) | **Full GDB** + Intel VTune/Perf support |
| Performance overhead | Near-native for narrow critical paths | **4–17% overhead** for compute-bound workloads |

> [!NOTE]
> Gramine's overhead is concentrated in system-call-heavy paths (I/O, networking). For our workload — which is **compute-bound** (AES-GCM, Paillier, homomorphic aggregation) — the overhead is minimal and well within reviewer tolerance. The trade-off is explicitly disclosed in the paper as a methodological decision.

---

## Gramine Manifest Overview

The following manifest template governs how the PLOSHA-RMFR binary runs inside the enclave:

```toml
# plosha_rmfr.manifest.template

loader.entrypoint = "file:{{ gramine.libos }}"
libos.entrypoint = "/usr/bin/plosha_rmfr"

loader.env.LD_LIBRARY_PATH = "/lib:/usr/lib:/usr/lib/x86_64-linux-gnu"

# Filesystem mounts
fs.mounts = [
  { path = "/lib",                        uri = "file:{{ gramine.runtimedir() }}" },
  { path = "/usr/lib",                    uri = "file:/usr/lib" },
  { path = "/usr/lib/x86_64-linux-gnu",   uri = "file:/usr/lib/x86_64-linux-gnu" },
  { path = "/usr/bin/plosha_rmfr",        uri = "file:plosha_rmfr" },
  # Sealed storage for DSP/FSM state migration
  { type = "encrypted", path = "/sealed", uri = "file:sealed_data/", key_name = "_sgx_mrenclave" },
]

# SGX enclave configuration
sgx.enclave_size = "512M"
sgx.max_threads  = 16

# Trusted files — integrity-verified at load time
sgx.trusted_files = [
  "file:{{ gramine.libos }}",
  "file:plosha_rmfr",
  "file:/usr/lib/x86_64-linux-gnu/libssl.so.3",
  "file:/usr/lib/x86_64-linux-gnu/libcrypto.so.3",
  "file:/usr/lib/x86_64-linux-gnu/libgmp.so.10",
  "file:/usr/lib/x86_64-linux-gnu/libgmpxx.so.4",
  "file:/usr/lib/x86_64-linux-gnu/libstdc++.so.6",
]

# Performance diagnostics (disable in production)
sgx.enable_stats = true
```

---

## Tier 1: ✅ REAL HARDWARE MEASUREMENTS (No Flags)

These produce genuine, reproducible numbers from your SGX machine. The application runs inside Gramine-SGX, so all measurements reflect **real enclave execution** including Gramine's LibOS overhead.

| # | Metric | Method (Gramine) | Paper Ref |
|---|--------|-------------------|-----------|
| 1 | $T_{AES}$ Enc/Dec | **OpenSSL** AES-256-GCM called normally inside Gramine enclave (no `sgx_tcrypto` needed) | Phase III Step 3 |
| 2 | $T_{PEnc}$ Paillier Encrypt | **GMP** modular exponentiation ($g^m \cdot r^n \bmod n^2$), linked via manifest | Phase III Step 3 |
| 3 | $T_{PAdd}$ Homomorphic Add | Modular multiplication of two ciphertexts ($\bmod n^2$) using GMP | Phase III Step 4 |
| 4 | $T_{PDec}$ Paillier Decrypt | L-function computation with private key using GMP | Table 2 |
| 5 | $T_{TEE}$ Transformation | AES decrypt → Paillier encrypt **inside Gramine enclave** (end-to-end cost including LibOS overhead) | Phase III Step 3 |
| 6 | Micro-slot Aggregation | Aggregate $N$ Paillier ciphertexts via multiplication inside enclave | Phase III Step 4 |
| 7 | Hierarchical Aggregation | Aggregate $m^*$ micro-slot results inside enclave | Phase III Step 5 |
| 8 | EPC Paging Overhead | Measured via `sgx.enable_stats = true` in manifest — Gramine reports EENTER/EEXIT counts and paging events | SGX HW |
| 9 | $\|CT_{AES}\|$ size | Actual byte count: plaintext + 16B tag + 12B IV | Comm-Cost Table |
| 10 | $\|CT_P\|$ size | Actual Paillier ciphertext bytes (2× key bits / 8) | Comm-Cost Table |
| 11 | $T_{pred}$ EWMA | Real arithmetic: $\hat{S}(t+1) = \alpha S(t) + (1-\alpha)\hat{S}(t)$ | Phase II Step 2 |
| 12 | $T_{risk}$ Assessment | Real arithmetic: $Cap_i$, $FE_i$, $Risk_i$ computation | Phase II Steps 3-5 |
| 13 | Paillier Keygen | Real 2048-bit keypair generation time (GMP `mpz_nextprime`) | Phase I Step 3 |
| 14 | Scalability ($N_s$ sweep) | Aggregate 500→5000 readings, measure real wall-clock time inside enclave | Experiment 1 |
| 15 | Gramine Enclave Overhead | Measure `sgx.enable_stats` output: total EENTER/EEXIT count, enclave init time, per-syscall overhead | General TEE |
| 16 | Enclave Creation | Real `gramine-sgx ./plosha_rmfr` startup time (includes LibOS init + enclave creation) | Phase I |
| 17 | $T_{Sig}$ Signature | ECDSA sign via **OpenSSL** inside Gramine enclave (replaces `sgx_tcrypto` ECDSA) | Phase V Step 1 |
| 18 | AES-GCM Key Gen | OpenSSL `RAND_bytes()` + key derivation inside enclave (Gramine forwards `/dev/urandom` reads to `RDRAND`) | Phase I Step 2 |

> [!TIP]
> **Key Gramine advantage for Tier 1:** You write standard C++ with standard libraries (OpenSSL, GMP). No ECALL/OCALL wrappers, no EDL files, no `sgx_tcrypto` API. The manifest handles enclave setup. Benchmarking code is identical to native Linux benchmarking — just wrap with `std::chrono` or `clock_gettime`.

---

## Tier 2: ⚠️ CANNOT BE DIRECTLY MEASURED — With Reviewer-Safe Mitigations

These metrics require distributed infrastructure or real failure events. Below, I explain **why each is unmeasurable** and **what approach a reviewer would accept**.

---

### 2.1 Network Latency ($L_i(t)$) — Sensor-to-Fog Communication

**Why unmeasurable:** Requires physical IIoT sensors transmitting over a real network to fog nodes.

**Mitigation (Reviewer-safe):**
> Use **`tc` (Linux Traffic Control)** to add real, controlled latency to the loopback interface. This is a widely accepted methodology in IEEE papers (called "emulated network conditions"). You explicitly state:
> *"Network latency is emulated using Linux `tc netem` with delays of 1ms, 5ms, 10ms, 50ms to model LAN, campus, metro, and WAN fog deployments respectively."*

**Gramine integration note:** Network I/O exits the enclave via Gramine's OCALL path, so `tc netem` delays are experienced by the enclave transparently — no special handling needed.

**Why a reviewer won't flag this:** Network emulation with `tc netem` is a standard practice cited in hundreds of IEEE/ACM papers. The key is to **disclose it** and use **realistic delay profiles** from published IIoT network studies.

---

### 2.2 Queue Utilization ($Q_i(t)$) — Fog Processing Queue

**Why unmeasurable:** Requires real concurrent sensor connections creating actual contention.

**Mitigation (Reviewer-safe):**
> Implement a **real bounded queue** (e.g., `std::queue` with mutex) and use **pthreads** to spawn concurrent producer threads that submit encrypted sensor data at controlled rates. Measure real queue depth over time. State:
> *"Queue contention is generated using $P$ concurrent producer threads submitting AES-GCM encrypted readings at rates of 100–10,000 readings/sec to a bounded processing queue."*

**Gramine integration note:** Set `sgx.max_threads = 16` (or higher) in the manifest to support multithreaded workloads. Gramine's pthreads support is mature for producer-consumer patterns. Verify thread count via `sgx.enable_stats`.

**Why a reviewer won't flag this:** You are measuring **real thread contention and queue depth** on your hardware. The concurrency is genuine, not simulated with random numbers.

---

### 2.3 Fog-Node Failure & Recovery Latency

**Why unmeasurable:** Requires physically crashing a fog node and measuring how long a backup takes over.

**Mitigation (Reviewer-safe):**
> **Decompose recovery into its measurable atomic operations.** Instead of claiming end-to-end recovery latency, measure each component that recovery consists of:
>
> | Recovery Sub-operation | Real Measurement (Gramine) |
> |------------------------|---------------------------|
> | Detect failure (timeout) | Configurable parameter, stated as assumption |
> | Seal aggregation state | Write to Gramine **encrypted FS** mount (`/sealed/`) — measures real SGX-sealed write latency |
> | Transfer sealed blob (network) | Measure serialized blob size in bytes, report size not transfer time |
> | Unseal at backup node | Read from Gramine **encrypted FS** mount — measures real SGX-sealed read latency |
> | Resume aggregation from micro-slot | Real Paillier re-aggregation time for $\|D_i^{miss}\|$ slots |
>
> State: *"Recovery latency is decomposed into its constituent cryptographic and TEE operations, each measured independently on real SGX hardware via Gramine's encrypted filesystem. Network transfer time is computed analytically from measured payload sizes and assumed network bandwidth."*

**Gramine integration note:** Instead of calling `sgx_seal_data()` / `sgx_unseal_data()` directly, use Gramine's encrypted filesystem mount:
```toml
{ type = "encrypted", path = "/sealed", uri = "file:sealed_data/", key_name = "_sgx_mrenclave" }
```
Your C++ code simply does `std::ofstream("/sealed/dsp_state.bin")` and `std::ifstream("/sealed/dsp_state.bin")` — Gramine handles sealing/unsealing transparently.

**Why a reviewer won't flag this:** This is actually **stronger** than an end-to-end simulation because it provides reproducible atomic measurements. The reviewer can verify each component independently. Many top-tier papers use this decomposition approach.

---

### 2.4 Delegation State Transfer (DSP/FSM Migration)

**Why unmeasurable:** Requires two physical fog nodes with attested enclaves.

**Mitigation (Reviewer-safe):**
> Measure the **real cost of each sub-operation on a single machine** using two separate Gramine enclave instances:
> - Instance A: Write `DSP_i` to encrypted FS (`/sealed/dsp_out.bin`) → measure time and output blob size
> - Instance B: Read `DSP_i` from encrypted FS (`/sealed/dsp_in.bin`) → measure time
> - Report the **sealed payload size** ($|DSP|$, $|FSM|$) in bytes
>
> State: *"Delegation overhead is measured as the sum of encrypted-file write, read, and state reconstruction latencies on real SGX hardware via Gramine. Two Gramine enclave instances model source and destination fog nodes."*

**Gramine integration note:** Launch two separate `gramine-sgx ./plosha_rmfr` processes. Each gets its own enclave. To share sealed data between them, use `key_name = "_sgx_mrsigner"` (binds to signer, not specific enclave measurement) so both instances can read/write the same encrypted files.

**Why a reviewer won't flag this:** You are running **two real SGX enclaves** on the same physical machine. The cryptographic operations (encrypt/decrypt via Gramine's encrypted FS) are identical regardless of whether the enclaves are on the same or different machines. The only thing you cannot measure is network transfer, which you honestly report as a payload size.

---

### 2.5 Remote Attestation Latency

**Why unmeasurable:** Requires a network roundtrip to Intel's Provisioning Certification Service (PCS).

**Mitigation (Reviewer-safe):**
> **Option A (Best — now practical with Gramine):** Use Gramine's **built-in RA-TLS** (`ra_tls_attest.so` + `ra_tls_verify_dcap.so`). This provides a complete DCAP attestation flow locally. Measure the end-to-end RA-TLS handshake time against a local PCCS cache. This is a **real** attestation measurement.
>
> **Option B (Acceptable):** Measure Gramine's local attestation via `/dev/attestation/report` (which triggers `EREPORT` internally), and cite Intel's published benchmarks for the network portion. State:
> *"Local attestation latency is measured directly via Gramine's /dev/attestation pseudo-filesystem. Remote attestation overhead is reported as RA-TLS handshake cost plus published Intel PCS round-trip estimates."*

**Gramine integration note:** Gramine exposes attestation via a pseudo-filesystem:
- `/dev/attestation/attestation_type` — read to check supported attestation type
- `/dev/attestation/user_report_data` — write your 64-byte report data
- `/dev/attestation/report` — read to get `EREPORT` (local attestation)
- `/dev/attestation/quote` — read to get the full DCAP quote (remote attestation)

No SGX SDK calls needed. Just `open()` / `read()` / `write()` on these pseudo-files from your C++ code.

**Why a reviewer won't flag this:** Citing vendor-published benchmarks for infrastructure components you cannot control is standard practice. With Gramine's RA-TLS, Option A is now far simpler to implement than with the raw SDK.

---

### 2.6 AFLTO Threshold Convergence

**Why unmeasurable:** Requires running the full system for many epochs with varying real workloads.

**Mitigation (Reviewer-safe):**
> Implement AFLTO as a **standalone numerical computation** (can run inside or outside the enclave). Feed it **deterministic synthetic workload traces** that follow published IIoT patterns (e.g., sinusoidal load, spike events, gradual degradation). Plot convergence curves. State:
> *"AFLTO convergence is evaluated using deterministic synthetic workload traces modeled after published IIoT operational patterns [cite]. Threshold evolution is plotted over 1000 epochs."*

**Gramine integration note:** No special handling. AFLTO is pure arithmetic — runs identically inside Gramine with negligible overhead.

**Why a reviewer won't flag this:** AFLTO is purely a mathematical feedback loop with no cryptographic component. Using deterministic traces (not random!) with disclosed parameters is standard for control-system evaluation. The key word is **deterministic** — a reviewer can reproduce your exact curves.

---

### 2.7 System Availability (%)

**Why unmeasurable:** Requires sustained multi-node operation with real failures over extended periods.

**Mitigation (Reviewer-safe):**
> **Derive it analytically** from your measured components:
> $$\text{Availability} = 1 - P(\text{failure}) \times L_{agg}(m^*)$$
> where $P(\text{failure})$ is a swept parameter (2%–20% as in Experiment 4) and $L_{agg}(m^*) = 1/m^*$ is the paper's own formula.
>
> State: *"System availability is computed analytically from the aggregation-loss localization property (Theorem 6) under varying failure probabilities."*

**Why a reviewer won't flag this:** This is a **direct application of the paper's own theoretical result**. You are validating the math, not fabricating data.

---

### 2.8 Aggregation Completeness ($V_i(t)$)

**Why unmeasurable:** Requires real sensor dropout events.

**Mitigation (Reviewer-safe):**
> Use **controlled packet dropping** — in your multi-threaded queue benchmark (§2.2), intentionally skip $X\%$ of sensor submissions to model dropout. Measure the resulting $V_i(t) = N_{recv}/N_{exp}$.
>
> State: *"Sensor dropout is modeled by deterministically omitting readings at rates of 0%, 5%, 10%, 20% per micro-slot."*

**Why a reviewer won't flag this:** The dropout is **controlled and reproducible**, not random. You are testing the framework's response to known incompleteness levels.

---

### 2.9 Recovery Communication Overhead (KB)

**Why unmeasurable:** Requires actual network transfer between nodes during failover.

**Mitigation (Reviewer-safe):**
> **Report payload sizes, not transfer times.** Measure the exact byte size of:
> - $|DSP_i(t)|$ — delegation state package (written to Gramine encrypted FS)
> - $|FSM_i(t)|$ — failover state migration (written to Gramine encrypted FS)
> - $|C_{micro,k}|$ — individual micro-slot aggregate
>
> State: *"Communication overhead is reported as serialized payload sizes in bytes. Network transfer time can be derived by dividing by the deployment's available bandwidth."*

**Gramine integration note:** Payload sizes are measured by checking the file size of the sealed blobs on the host side (`sealed_data/` directory). Gramine's encrypted FS adds a small overhead per file for the MAC and metadata — report both the logical and physical (on-disk) sizes.

**Why a reviewer won't flag this:** Reporting payload sizes is **more useful** than reporting transfer times because it is bandwidth-independent. Reviewers prefer this because it allows comparison across different network configurations.

---

## Tier 3: 🚫 MUST BE STATED AS ASSUMPTIONS (Not Measured)

These items should be explicitly listed as **system parameters** in your paper, not measured.

| Parameter | Reviewer-safe approach |
|-----------|----------------------|
| Failure detection timeout | State as configurable: *"Failure detection timeout is set to $T_{detect} = 500\text{ms}$"* |
| Number of fog nodes ($n$) | State as experimental parameter: *"$n$ is varied from 5 to 50"* |
| EWMA smoothing coefficient ($\alpha$) | State as design parameter: *"$\alpha = 0.3$ based on [cite]"* |
| Weight vectors ($\omega_w, \omega_q, \omega_l$) | State as configuration: *"Equal weighting $\omega = 1/3$ is used unless otherwise specified"* |
| Network bandwidth | State as assumption: *"100 Mbps LAN connectivity between fog nodes"* |
| Gramine LibOS overhead | State as known trade-off: *"Gramine introduces 4–17% overhead for compute-bound workloads [cite Gramine benchmarks]; this is accepted as a development-ease trade-off"* |

---

## Summary: What Your Benchmark Should Produce

| Output | Type | Reviewer Risk |
|--------|------|---------------|
| Crypto operation latencies (AES via OpenSSL, Paillier via GMP) | **Real measured** (inside Gramine enclave) | None |
| TEE transformation latency (end-to-end inside enclave) | **Real measured** | None |
| EPC paging overhead | **Real measured** (`sgx.enable_stats`) | None |
| Aggregation scalability (500–5000 sensors) | **Real measured** | None |
| Payload sizes (CT, DSP, FSM) | **Real measured** (encrypted FS blob sizes) | None |
| Gramine enclave overhead (EENTER/EEXIT, init) | **Real measured** | None |
| Queue contention under load | **Real measured** (threaded) | None |
| Network latency effects | **Emulated** (`tc netem`) | Low — if disclosed |
| AFLTO convergence | **Deterministic numerical** | Low — if traces disclosed |
| Recovery latency | **Decomposed real** (encrypted FS) | None — stronger than simulation |
| System availability | **Analytical derivation** | None — uses paper's own theorems |
| RA-TLS attestation | **Real measured** (Gramine built-in) | None |

---

## Development Workflow with Gramine

```
1. Write standard C++ code (OpenSSL, GMP, pthreads, standard I/O)
         │
         ▼
2. Compile normally:  g++ -O2 -o plosha_rmfr *.cpp -lssl -lcrypto -lgmp -lpthread
         │
         ▼
3. Create manifest:  gramine-manifest -Darch_libdir=/usr/lib/x86_64-linux-gnu \
                        plosha_rmfr.manifest.template > plosha_rmfr.manifest
         │
         ▼
4. Sign for SGX:     gramine-sgx-sign --manifest plosha_rmfr.manifest \
                        --key enclave-key.pem --output plosha_rmfr.manifest.sgx
         │
         ▼
5. Run in enclave:   gramine-sgx ./plosha_rmfr
         │
         ▼
6. Debug (if needed): gramine-sgx -d ./plosha_rmfr   (attaches GDB)
```

> [!TIP]
> **For development without SGX hardware**, use `gramine-direct ./plosha_rmfr` to run the same binary through Gramine's Linux PAL — no enclave, but same LibOS path. This lets you develop and test on any Linux machine, then deploy to SGX hardware for final benchmarks.

---

> [!CAUTION]
> **The single biggest red flag for a reviewer** is reporting end-to-end distributed metrics (recovery time, system availability, communication overhead) as if they were measured on a single machine without disclosure. The mitigations above avoid this by either (a) decomposing into measurable atomic operations, (b) reporting sizes instead of times, or (c) using analytical derivation from the paper's own formulas.
>
> **Gramine-specific disclosure:** The paper must explicitly state that the TEE runtime is Gramine (LibOS) rather than the raw Intel SGX SDK. Report Gramine's version and note that the LibOS introduces a small overhead compared to native SDK partitioning. This is honest and reviewers will appreciate the transparency — Gramine is widely cited in recent SGX research.
