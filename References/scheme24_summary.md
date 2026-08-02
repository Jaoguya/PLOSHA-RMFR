# Scheme 24 — Robust IIoT (PPDA): Technical Reference Summary

**Paper**: Shang et al., "A Robust Privacy-Preserving Data Aggregation Scheme for Edge-Supported IIoT," IEEE TII, Vol. 20, No. 3, March 2024.

## System Architecture

4 entity types:
1. **Administrator (AD)** — initiates tasks, receives final analysis results
2. **Cloud Center (CC)** — sufficient compute; decrypts aggregated ciphertexts; holds Paillier private key
3. **Edge Servers (ESs)** — intermediate aggregators bridging CC and sensors; batch verify + homomorphic aggregate
4. **Sensors (Ss)** — resource-constrained; collect + encrypt + sign data; send to ES

## Cryptographic Primitives

### Paillier Cryptosystem
- **KeyGen**: Choose primes p0, q0 (|p0|=|q0|=k1=1024 bits). N=p0·q0, λ=lcm(p0-1, q0-1), g=N+1, μ=(L(g^λ mod N²))⁻¹ mod N
- **Encrypt**: C = g^M · r^N mod N² (r ∈ Z*_N random)
- **Decrypt**: M = L(C^λ mod N²) · μ mod N
- **Homomorphic Add**: C_sum = C1 · C2 mod N² → Dec(C_sum) = M1 + M2

### Modified ECDSA with Batch Verification
- **Curve**: P-256 (prime256v1), |q|=k2=256 bits
- **KeyGen**: Private d ∈ [1,n-1], Public Q = dG
- **Sign(d, m)**: k random → P=(x,y)=kG, r=x mod n, e=H(m), s=k⁻¹(e+dr) mod n → output (P, s)
- **BatchVerify**: Check (Σ e_i·s_i⁻¹)G + Σ r_i·s_i⁻¹·Q_i = Σ P_i

### Differential Privacy
- Laplace noise ε-differential privacy: ESi adds Lap(Δf/ε) noise to aggregated ciphertext

## PPDA Workflow

### Phase 1: System Initialization
- CC generates Paillier keys (N, g) public, (λ, μ) private
- CC chooses elliptic curve params, generates ECDSA keypair

### Phase 2: Registration
- ESs and Ss each generate ECDSA keypairs, sign their identity, send to CC for mutual authentication

### Phase 3: Data Collection
1. **Data Request**: AD sends task T to CC → CC signs request → broadcasts to ESs → ESs forward to Ss
2. **Data Generation**: Sensor Sij encrypts data mij as cij = g^mij · Rij^N mod N², signs δij = sig(dSij, H(IDSij||cij||Tij))
3. **Data Aggregation**: ESi batch-verifies all t signatures, homomorphically aggregates ciphertexts: ci = Π cij, adds Laplace noise ĉi = ci · g^m̂i, signs and sends to CC
4. **Data Decryption**: CC batch-verifies ES signatures, decrypts: M̂i = Σ mij + m̂i

## Experimental Parameters (from paper)
- CC: 1, ES: 2, Sensors: 20 (10 per ES) — baseline
- Paillier key: 1024-bit primes → 2048-bit N
- ECDSA: P-256 curve, SHA-256 hash
- Identity: 2 bytes, Timestamp: 4 bytes
- Ciphertext size: 256 bytes, Hash: 32 bytes

## Key Metrics
- Aggregation latency (end-to-end: encrypt + sign + batch verify + aggregate + decrypt)
- Communication overhead (bytes between S↔ES, ES↔CC)
- Computational cost at S, ES, CC sides
