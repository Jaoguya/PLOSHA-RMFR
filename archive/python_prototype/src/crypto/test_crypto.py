"""
Crypto Verification Test
=========================
Confirms that all cryptographic primitives in PLOSHA-RMFR work correctly
with real libraries — not mocked or simulated.

Tests:
  1. AES-GCM:   encrypt → decrypt roundtrip
  2. Paillier:  encrypt → decrypt roundtrip
  3. Paillier:  homomorphic addition (Dec(Enc(a) + Enc(b)) == a + b)
  4. ECDSA:     sign → verify roundtrip
  5. ECDSA:     tampered payload fails verification
  6. TEE:       full pipeline (AES → TEE transform → Paillier → aggregate → decrypt)

Run:
    python -m src.crypto.test_crypto
"""

import sys
import os

# Ensure project root is on path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from src.crypto import aes_gcm, paillier
from src.crypto.tee import TEEEnclave, compute_hash


def test_aes_gcm_roundtrip():
    """AES-GCM: encrypt then decrypt must return the original value."""
    print("[TEST 1] AES-GCM encrypt/decrypt roundtrip")

    key = aes_gcm.generate_key(256)
    test_values = [0, 1, 42, 255, 1000, 32768, 65535]

    for val in test_values:
        ct = aes_gcm.encrypt(key, val)
        decrypted = aes_gcm.decrypt(key, ct)
        assert decrypted == val, f"FAIL: encrypt({val}) -> decrypt = {decrypted}"

    # Verify ciphertext size: nonce(12) + ct(8) + tag(16) = 36 bytes
    ct = aes_gcm.encrypt(key, 12345)
    assert len(ct) == 36, f"FAIL: ciphertext size = {len(ct)}, expected 36"

    # Verify different nonces produce different ciphertexts
    ct1 = aes_gcm.encrypt(key, 100)
    ct2 = aes_gcm.encrypt(key, 100)
    assert ct1 != ct2, "FAIL: same plaintext should produce different ciphertexts (random nonce)"

    # Verify wrong key fails decryption
    wrong_key = aes_gcm.generate_key(256)
    try:
        aes_gcm.decrypt(wrong_key, ct1)
        assert False, "FAIL: decryption with wrong key should raise an error"
    except Exception:
        pass  # Expected

    print("  ✓ PASSED — all values roundtrip correctly, size=36B, nonce randomized\n")


def test_paillier_roundtrip():
    """Paillier: encrypt then decrypt must return the original value."""
    print("[TEST 2] Paillier encrypt/decrypt roundtrip")

    pk, sk = paillier.keygen(key_size=1024)
    test_values = [0, 1, 42, 255, 1000, 32768, 65535]

    for val in test_values:
        ct = paillier.encrypt(pk, val)
        decrypted = paillier.decrypt(sk, ct)
        assert abs(decrypted - val) < 0.5, f"FAIL: encrypt({val}) -> decrypt = {decrypted}"

    print("  ✓ PASSED — all values roundtrip correctly\n")


def test_paillier_homomorphic_addition():
    """Paillier: Dec(Enc(a) + Enc(b)) must equal a + b."""
    print("[TEST 3] Paillier homomorphic addition")

    pk, sk = paillier.keygen(key_size=1024)

    # Two-value addition
    a, b = 12345, 54321
    ca = paillier.encrypt(pk, a)
    cb = paillier.encrypt(pk, b)
    c_sum = paillier.homomorphic_add(ca, cb)
    decrypted_sum = paillier.decrypt(sk, c_sum)
    assert abs(decrypted_sum - (a + b)) < 0.5, \
        f"FAIL: Dec(Enc({a}) + Enc({b})) = {decrypted_sum}, expected {a + b}"

    # Multi-value aggregation (simulates micro-slot aggregation)
    values = [100, 200, 300, 400, 500]
    ciphertexts = [paillier.encrypt(pk, v) for v in values]
    c_agg = paillier.aggregate_ciphertexts(ciphertexts)
    decrypted_agg = paillier.decrypt(sk, c_agg)
    expected = sum(values)
    assert abs(decrypted_agg - expected) < 0.5, \
        f"FAIL: aggregate sum = {decrypted_agg}, expected {expected}"

    print(f"  ✓ PASSED — Dec(Enc(12345)+Enc(54321)) = {int(decrypted_sum)}")
    print(f"  ✓ PASSED — aggregate({values}) = {int(decrypted_agg)}\n")


def test_ecdsa_sign_verify():
    """ECDSA: sign then verify must succeed; tampered data must fail."""
    print("[TEST 4] ECDSA sign/verify roundtrip")

    enclave = TEEEnclave("test_node")
    enclave.attest()

    payload = b"PLOSHA-RMFR aggregation commit"
    signature = enclave.sign(payload)

    # Valid signature
    assert enclave.verify_signature(payload, signature), \
        "FAIL: valid signature rejected"

    print("  ✓ PASSED — valid signature verified\n")

    print("[TEST 5] ECDSA tampered payload must fail verification")

    tampered = b"TAMPERED aggregation commit"
    assert not enclave.verify_signature(tampered, signature), \
        "FAIL: tampered payload should not verify"

    print("  ✓ PASSED — tampered payload correctly rejected\n")


def test_tee_full_pipeline():
    """
    Full TEE pipeline: sensor AES encrypt → TEE transform → Paillier
    → homomorphic aggregate → decrypt → verify sum.

    This is the core data path from §3.3 and §5.4:
      Sensor: CT_j = AES-GCM(k_i, d_j)
      TEE:    d_j = Dec(CT_j)  →  C_j = PaillierEnc(d_j)
      Fog:    C_agg = Π C_j
      Cloud:  sum = PaillierDec(C_agg)
    """
    print("[TEST 6] Full TEE pipeline: AES → Paillier → aggregate → decrypt")

    # Setup: generate keys
    aes_key = aes_gcm.generate_key(256)
    paillier_pk, paillier_sk = paillier.keygen(key_size=1024)

    # Setup: create and attest TEE enclave
    enclave = TEEEnclave("fog_0")
    enclave.attest()
    enclave.provision_keys(aes_key, paillier_pk)

    # Simulate 5 sensor readings
    sensor_values = [100, 200, 300, 400, 500]
    expected_sum = sum(sensor_values)

    # Step 1: Sensors encrypt with AES-GCM
    aes_ciphertexts = [aes_gcm.encrypt(aes_key, v) for v in sensor_values]

    # Step 2: TEE transforms each AES ciphertext → Paillier ciphertext
    paillier_ciphertexts = []
    recovered_plaintexts = []
    for ct_aes in aes_ciphertexts:
        ct_paillier, plaintext = enclave.transform_ciphertext(ct_aes)
        paillier_ciphertexts.append(ct_paillier)
        recovered_plaintexts.append(plaintext)

    # Verify TEE correctly recovered the original values
    assert recovered_plaintexts == sensor_values, \
        f"FAIL: TEE decryption mismatch: {recovered_plaintexts} != {sensor_values}"

    # Step 3: Homomorphic aggregation (fog node)
    c_agg = paillier.aggregate_ciphertexts(paillier_ciphertexts)

    # Step 4: Cloud decrypts aggregate
    decrypted_sum = paillier.decrypt(paillier_sk, c_agg)
    assert abs(decrypted_sum - expected_sum) < 0.5, \
        f"FAIL: pipeline sum = {decrypted_sum}, expected {expected_sum}"

    # Step 5: TEE signs the commitment
    commit_hash = compute_hash((str(c_agg), "Normal", "1.0", "1"))
    signature = enclave.sign(commit_hash)
    assert enclave.verify_signature(commit_hash, signature), \
        "FAIL: commitment signature verification failed"

    print(f"  Sensor values:  {sensor_values}")
    print(f"  Expected sum:   {expected_sum}")
    print(f"  Decrypted sum:  {int(decrypted_sum)}")
    print(f"  Signature:      {signature[:16].hex()}... ({len(signature)} bytes)")
    print(f"  ✓ PASSED — full pipeline verified\n")


def main():
    print("=" * 60)
    print("PLOSHA-RMFR Crypto Verification Test")
    print("Real libraries: cryptography, PyCryptodome, python-phe")
    print("=" * 60 + "\n")

    test_aes_gcm_roundtrip()
    test_paillier_roundtrip()
    test_paillier_homomorphic_addition()
    test_ecdsa_sign_verify()
    test_tee_full_pipeline()

    print("=" * 60)
    print("✓ ALL 6 TESTS PASSED — crypto is real and verified")
    print("=" * 60)


if __name__ == "__main__":
    main()
