"""
Paillier Homomorphic Encryption Module
========================================
Implements Paillier public-key cryptosystem for privacy-preserving aggregation.

Paper Reference:
  - Phase I, Step 3: KRM generates Paillier key pair (pk_P, sk_P)
  - Phase III, Step 3: C_j = Enc_{pk_P}(d_j)
  - Phase III, Step 4: C_{micro,k} = Π_{j∈δ_k} C_j  (homomorphic addition)
  - Phase III, Step 5: C_{agg,i} = Π_{k=1}^{m*} C_{micro,k}
  - Additive homomorphic property: Dec(C1 * C2) = m1 + m2
"""

import phe


def keygen(key_size: int = 1024):
    """
    Generate a Paillier key pair.

    Args:
        key_size: Security parameter λ in bits.

    Returns:
        Tuple (public_key, private_key).

    Paper: Phase I, Step 3 - KRM generates (pk_P, sk_P) using security parameter λ
    """
    public_key, private_key = phe.generate_paillier_keypair(n_length=key_size)
    return public_key, private_key


def encrypt(public_key, plaintext_value: float):
    """
    Encrypt a value using Paillier encryption.

    C_j = Enc_{pk_P}(d_j)

    Args:
        public_key: Paillier public key (pk_P).
        plaintext_value: Value to encrypt.

    Returns:
        Paillier EncryptedNumber.

    Paper: Phase III, Step 3 - TEE transforms to homomorphic ciphertext
    """
    return public_key.encrypt(float(plaintext_value))


def decrypt(private_key, ciphertext) -> float:
    """
    Decrypt a Paillier ciphertext.

    d_j = Dec_{sk_P}(C_j)

    Args:
        private_key: Paillier private key (sk_P).
        ciphertext: Paillier EncryptedNumber.

    Returns:
        Decrypted value.

    Paper: sk_P remains sealed within KRM, never disclosed to fog nodes
    """
    return private_key.decrypt(ciphertext)


def homomorphic_add(ciphertext_a, ciphertext_b):
    """
    Homomorphic addition of two Paillier ciphertexts.

    C_result = C_a * C_b mod n^2  =>  Dec(C_result) = m_a + m_b

    Args:
        ciphertext_a: First Paillier ciphertext.
        ciphertext_b: Second Paillier ciphertext.

    Returns:
        Sum ciphertext.

    Paper: Phase III, Step 4 - Micro-slot aggregation via multiplicative homomorphism
           C_{micro,k} = Π_{j∈δ_k} C_j
    """
    return ciphertext_a + ciphertext_b


def aggregate_ciphertexts(ciphertexts: list):
    """
    Aggregate a list of Paillier ciphertexts via homomorphic addition.

    C_agg = Π_{j} C_j  =>  Dec(C_agg) = Σ d_j

    Args:
        ciphertexts: List of Paillier EncryptedNumber objects.

    Returns:
        Aggregate ciphertext, or None if list is empty.

    Paper: Phase III, Steps 4-5 - Micro-slot then fog-level aggregation
    """
    if not ciphertexts:
        return None
    result = ciphertexts[0]
    for ct in ciphertexts[1:]:
        result = result + ct
    return result
