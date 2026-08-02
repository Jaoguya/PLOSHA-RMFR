"""
AES-GCM Encryption Module
===========================
Implements fog-scoped AES-256-GCM encryption for sensor-to-fog communication.

Gramine-SGX Edition — sensor readings are integer d_j ∈ [0, 65535],
encoded as 8-byte big-endian for encryption, matching the Experiment Plan
Section 3.3.  Output: nonce(12) + ct(8) + tag(16) = 36 bytes.

Paper Reference:
  - Phase I, Step 2: fog-scoped AES-GCM key k_i provisioned to enclave
  - Phase III, Step 3: CT_j = Enc_{k_i}(d_j)
  - Sensor layer uses lightweight AES-GCM instead of heavy Paillier
"""

import os
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def generate_key(key_size_bits: int = 256) -> bytes:
    """
    Generate a random AES-GCM key.

    Args:
        key_size_bits: Key size in bits (128, 192, or 256).

    Returns:
        Random key bytes.

    Paper: Phase I, Step 2 - KRM generates unique fog-scoped AES-GCM key k_i
    """
    return AESGCM.generate_key(bit_length=key_size_bits)


def encrypt(key: bytes, plaintext_value: int) -> bytes:
    """
    Encrypt a sensor reading using AES-256-GCM.

    CT_j = Enc_{k_i}(d_j)

    The plaintext integer d_j is encoded as 8-byte big-endian before
    encryption, matching the Experiment Plan Section 3.3:
        nonce = os.urandom(12)
        ct, tag = AES.new(k_i, MODE_GCM, nonce).encrypt_and_digest(d_j.to_bytes(8, 'big'))
        return nonce + ct + tag   # 12 + 8 + 16 = 36 bytes

    Args:
        key: AES-GCM key (k_i).
        plaintext_value: Sensor reading d_j (integer in [0, 65535]).

    Returns:
        Ciphertext bytes (nonce || ciphertext || tag).  36 bytes total.

    Paper: Phase III, Step 3 - Each sensed value d_j encrypted with fog-scoped key
    """
    aesgcm = AESGCM(key)
    nonce = os.urandom(12)  # 96-bit nonce for GCM
    # Encode integer as 8-byte big-endian (Experiment Plan §3.3)
    plaintext_bytes = int(plaintext_value).to_bytes(8, 'big')
    ciphertext = aesgcm.encrypt(nonce, plaintext_bytes, None)
    # Return nonce || ciphertext (AESGCM.encrypt appends the GCM tag)
    return nonce + ciphertext


def decrypt(key: bytes, ciphertext_blob: bytes) -> int:
    """
    Decrypt an AES-GCM ciphertext back to the sensor reading.

    d_j = Dec_{k_i}(CT_j)

    Args:
        key: AES-GCM key (k_i).
        ciphertext_blob: Nonce || ciphertext || tag.

    Returns:
        Decrypted sensor reading d_j (integer).

    Paper: Phase III, Step 3 - TEE enclave decrypts: d_j = Dec_{k_i}(CT_j)
    """
    nonce = ciphertext_blob[:12]
    ciphertext = ciphertext_blob[12:]
    aesgcm = AESGCM(key)
    plaintext_bytes = aesgcm.decrypt(nonce, ciphertext, None)
    return int.from_bytes(plaintext_bytes, 'big')
