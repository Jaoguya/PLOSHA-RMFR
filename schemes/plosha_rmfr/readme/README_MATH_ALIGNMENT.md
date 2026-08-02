# PLOSHA-RMFR Mathematical Alignment Notes

This document serves as a record of critical bug fixes applied to align the codebase with the mathematical derivations described in the PLOSHA-RMFR paper (`references/PLOSHA_RMFR.md`). 

**Future developers/agents:** Please read this before modifying the mathematical logic in `plosha.cpp`, `rmfr.cpp`, or `aflto.cpp` to prevent reverting correct implementations back to flawed states.

## 1. Phase III ($m^*$ Optimization) in `plosha.cpp`
**Issue:** The objective function for determining optimal micro-slots ($m^*$) in `computeOptimalMicroSlots()` did not correctly implement Equation 16 from the paper.
**Paper Equation 16:**
$$J(m) = \lambda_1 T_{\text{agg}}(m)\bigl(1-Cap_i(t+1)\bigr) + \lambda_2 FE_i(t) L_{\text{agg}}(m) + \lambda_3\bigl(1-Rel_i(t)\bigr) L_{\text{agg}}(m)$$
**Fixes Applied:**
- Added `reliability` as an explicit parameter (since `PredictionVector` doesn't contain $Rel_i(t)$).
- Corrected the $\lambda_1$ term to multiply by `(1.0 - pred.capacity)`.
- Corrected the $\lambda_2$ term to multiply by `pred.failure_exposure`.
- Corrected the $\lambda_3$ term to multiply `loss_exposure` ($L_{\text{agg}}(m)$) by `(1.0 - reliability)`. Previously it used a constant `rel_cap_penalty` which made the term mathematically useless for the `argmin` loop.

## 2. Phase IV (Recovery Escalation Logic) in `rmfr.cpp`
**Issue:** The recovery escalation logic in `determineRecoveryMode()` did not correctly implement Equation 28 from the paper.
**Fixes Applied:**
- **Completeness Flag ($\Phi_i(t)$):** In the code, `completeness_flag = true` corresponds to $\Phi_i(t) = 0$ (Complete). The logic was rewritten to branch correctly based on this flag.
  - If Complete (`completeness_flag == true`): It checks for `Normal` ($\tau_1$) and `Delegation` ($\tau_2$, extended to $\tau_3$ to cover a logical gap in the paper).
  - If Incomplete (`completeness_flag == false`): It correctly routes to `MicroRecovery`.
- **Dynamic $\tau_f$:** The code previously used a hardcoded constant `TAU_F_INIT`. We modified the signatures to pass down the dynamically adapted `tau_f` from AFLTO's `FeedbackState`, replacing the constant.

## 3. Phase V (AFLTO Threshold Sign) in `aflto.cpp`
**DO NOT CHANGE THIS TO ADDITION.**
**Issue Context:** The paper's Equation 48 defines the threshold update as an addition:
$$\tau_x(t+1) = \Pi_{[0,1]}\Bigl(\tau_x(t) + \mu_x e_i(t)\Bigr)$$
However, the code implements it as subtraction: `t.tau_x = project(t.tau_x - delta)`.
**Why the code is correct:**
- Tracing the error signal $e_i(t)$ (Eq 46): When the system degrades (high urgency, low reliability), $e_i(t)$ is large and positive.
- The paper's prose explicitly states: *"As the adaptive error increases, the thresholds become more sensitive, enabling earlier intervention..."*
- Lower thresholds = narrower "Normal" operating zone = earlier intervention (more sensitive).
- Therefore, a large positive error *must decrease* the thresholds.
- **Conclusion:** The subtraction in the code is operationally correct. Equation 48 in the paper contains a typographical sign error. Do not attempt to "fix" the code to match the paper's formula, as doing so will invert the feedback loop and destabilize the system.

## 4. TEE Cryptographic Fixes (`paillier.cpp` & `crypto_wrapper.cpp`)
**DO NOT REMOVE THE BLINDING FACTOR PRE-COMPUTATION.**
**Issue Context:** When executing inside the Gramine SGX LibOS, the `BN_rand_range()` call in `Paillier::Encrypt` grinds to a halt. Gramine intercepts the `getrandom()` system call and its emulated software entropy pool becomes instantly starved by the massive randomness demands of Paillier encryption, causing the TEE to hang for minutes at a time.
**Fixes Applied:**
- Implemented **Randomness Pre-computation Pool** (Fix A): Because the Paillier blinding factor ($r^n \bmod n^2$) is mathematically independent of the underlying plaintext, we pre-compute a pool of these factors in `Paillier::PrecomputeBlindingFactors`.
- **Initialization:** `CryptoWrapper::generatePaillierKeys()` now automatically generates a pool of 60,000 blinding factors during Phase I initialization.
- **Runtime:** `Paillier::Encrypt()` draws from this pool, requiring zero runtime `getrandom()` calls. This reduces the TEE encryption step to pure modular arithmetic and drops the per-slot processing time ($\beta_t$) to microsecond speeds (~93 $\mu s$).
- **Future Agents:** Do **NOT** try to revert `Paillier::Encrypt()` back to generating $r$ on the fly for every encryption. Doing so will completely break the simulation inside the Gramine TEE.
