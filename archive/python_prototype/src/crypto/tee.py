"""
Trusted Execution Environment (TEE) Module — Gramine-SGX Edition
==================================================================
Simulates TEE enclave operations using ECDSA (not HMAC) for signing,
matching the Gramine-SGX path described in Experiment Plan §5.4.

Gramine runs an unmodified CPython interpreter inside an SGX enclave,
so the same tee_sign() / tee_verify() module is plain Python whether
it executes outside the enclave (for local testing) or inside it
(for the real measurements under 'gramine-sgx python3 tee_sign.py').

Paper Reference:
  - Phase I, Step 1: Attest(F_i) = 1 for TEE attestation
  - Phase III, Step 3: TEE transforms AES ciphertext → Paillier ciphertext
  - Phase V, Step 1: σ_i(t) = Sign_{sk_i^TEE}(H(T_i(t)))
  - §5.4: ECDSA with cryptography.hazmat.primitives
"""

import hashlib
import os

from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization

from src.crypto import aes_gcm
from src.crypto import paillier


class TEEEnclave:
    """
    Simulated Trusted Execution Environment (TEE) enclave.

    Gramine-SGX Edition: uses ECDSA-P256 for signing (not HMAC).

    Provides:
    - Remote attestation
    - Ciphertext transformation (AES → Paillier)
    - ECDSA digital signing within the enclave
    - Isolated key storage
    """

    def __init__(self, node_id: str):
        """
        Initialize a TEE enclave for a fog node.

        Args:
            node_id: Identifier of the fog node hosting this enclave.
        """
        self.node_id = node_id
        self.attested = False
        # TEE ECDSA key pair (generated during attestation)
        self._private_key = None
        self._public_key = None
        # Fog-scoped AES key (provisioned by KRM)
        self._aes_key = None
        # Paillier public key (provisioned by KRM)
        self._paillier_pk = None
        # Sequence tracking for replay resistance (Theorem 5)
        self._last_seq = -1

    def attest(self) -> bool:
        """
        Perform remote attestation with KRM.

        Returns:
            True if attestation succeeds (Attest(F_i) = 1).

        Paper: Phase I, Step 1 - Fog node admitted only if Attest(F_i) = 1
        §5.4: "signing key is sealed to the enclave"
        """
        # Generate ECDSA key pair upon successful attestation
        self._private_key = ec.generate_private_key(ec.SECP256R1())
        self._public_key = self._private_key.public_key()
        self.attested = True
        return True

    def provision_keys(self, aes_key: bytes, paillier_pk):
        """
        Securely provision cryptographic keys into the enclave.

        Args:
            aes_key: Fog-scoped AES-GCM key k_i.
            paillier_pk: Paillier public key pk_P.

        Paper: Phase I, Step 2-3 - KRM provisions k_i and pk_P to attested enclave
        """
        if not self.attested:
            raise RuntimeError("TEE not attested — cannot provision keys")
        self._aes_key = aes_key
        self._paillier_pk = paillier_pk

    def transform_ciphertext(self, aes_ciphertext: bytes):
        """
        Transform AES-GCM ciphertext to Paillier ciphertext inside the enclave.

        Process:
            1. d_j = Dec_{k_i}(CT_j)          — AES decryption inside enclave
            2. C_j = Enc_{pk_P}(d_j)          — Paillier encryption inside enclave
            3. Erase d_j from enclave memory   — Plaintext transient only

        Args:
            aes_ciphertext: CT_j = Enc_{k_i}(d_j)

        Returns:
            Tuple (paillier_ciphertext, plaintext_value) where plaintext is for
            simulation verification only. In real TEE, plaintext is erased.

        Paper: Phase III, Step 3 - TEE-assisted ciphertext transformation
        """
        if not self.attested or self._aes_key is None:
            raise RuntimeError("TEE not properly initialized")

        # Step 1: Decrypt AES ciphertext inside enclave → integer d_j
        plaintext_value = aes_gcm.decrypt(self._aes_key, aes_ciphertext)

        # Step 2: Encrypt with Paillier inside enclave
        paillier_ct = paillier.encrypt(self._paillier_pk, plaintext_value)

        # Step 3: In a real TEE, plaintext_value would be erased here
        # We return it for verification purposes only
        return paillier_ct, plaintext_value

    def sign(self, data: bytes) -> bytes:
        """
        Generate an ECDSA digital signature inside the TEE enclave.

        σ_i(t) = Sign_{sk_i^TEE}(H(T_i(t)))

        Args:
            data: Data to sign (typically H(T_i(t))).

        Returns:
            ECDSA-P256 signature bytes (DER-encoded).

        Paper: Phase V, Step 1 - Enclave signs aggregation commitment
        §5.4: private_key.sign(payload, ec.ECDSA(hashes.SHA256()))
        """
        if not self.attested or self._private_key is None:
            raise RuntimeError("TEE not attested — cannot sign")
        return self._private_key.sign(data, ec.ECDSA(hashes.SHA256()))

    def verify_signature(self, data: bytes, signature: bytes) -> bool:
        """
        Verify an ECDSA signature using the enclave's public key.

        Args:
            data: Original data that was signed.
            signature: DER-encoded ECDSA signature to verify.

        Returns:
            True if signature is valid.

        Paper: §5.4 - public_key.verify(signature, payload, ec.ECDSA(hashes.SHA256()))
        """
        try:
            self._public_key.verify(signature, data, ec.ECDSA(hashes.SHA256()))
            return True
        except Exception:
            return False

    def check_sequence(self, seq: int) -> bool:
        """
        Verify sequence number for replay resistance.

        Seq_i(t) > Seq_i^{last}

        Args:
            seq: Incoming sequence number.

        Returns:
            True if sequence is fresh (not replayed).

        Paper: Theorem 5 - Replay Resistance via unique sequence identifiers
        """
        if seq > self._last_seq:
            self._last_seq = seq
            return True
        return False

    def get_public_verification_key(self):
        """Return the ECDSA public key for external verification.

        Paper: §5.4 - "Verify at the cloud side using the enclave's public key."
        """
        if not self.attested:
            raise RuntimeError("TEE not attested")
        return self._public_key


# =========================================================================
# Module-level convenience functions (§5.4 — identical inside/outside enclave)
# =========================================================================

def tee_sign(payload: bytes, private_key) -> bytes:
    """Sign payload with an EC private key (replaces OP-TEE TA call).

    Paper §5.4: "private_key.sign(payload, ec.ECDSA(hashes.SHA256()))"
    """
    return private_key.sign(payload, ec.ECDSA(hashes.SHA256()))


def tee_verify(payload: bytes, signature: bytes, public_key) -> bool:
    """Verify at the cloud side using the enclave's public key.

    Paper §5.4: "public_key.verify(signature, payload, ec.ECDSA(hashes.SHA256()))"
    """
    try:
        public_key.verify(signature, payload, ec.ECDSA(hashes.SHA256()))
        return True
    except Exception:
        return False


def get_sgx_quote(report_data: bytes) -> bytes:
    """Read the SGX quote Gramine exposes at /dev/attestation,
    used once at startup to prove this enclave is the signed
    tee_sign.py running under the expected manifest measurement.

    Paper §5.4: "Read the SGX quote Gramine exposes at /dev/attestation"
    """
    with open('/dev/attestation/user_report_data', 'wb') as f:
        f.write(report_data.ljust(64, b'\x00'))
    with open('/dev/attestation/quote', 'rb') as f:
        return f.read()


def compute_hash(data_tuple: tuple) -> bytes:
    """
    Compute hash of an aggregation package tuple.

    H(T_i(t)) where T_i(t) = (C_i^{final}, Mode_i, Rel_i, RecStatus_i)

    Args:
        data_tuple: Tuple of values to hash.

    Returns:
        SHA-256 hash bytes.
    """
    hasher = hashlib.sha256()
    for item in data_tuple:
        hasher.update(str(item).encode('utf-8'))
    return hasher.digest()
