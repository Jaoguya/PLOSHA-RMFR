"""
IIoT Sensor Entity Module
===========================
Models resource-constrained Industrial IoT sensors that generate
operational measurements and encrypt them with AES-GCM.

Gramine-SGX Edition — sensor readings are integer d_j ∈ [0, 65535]
(16-bit unsigned), matching the Experiment Plan Section 3.1-3.2.
Supports both trace-driven (Intel Berkeley Lab) and synthetic modes.

Paper Reference:
  - System Model: S = {S_1, S_2, ..., S_m} - set of IIoT sensors
  - "Sensors continuously generate operational data such as vibration,
     temperature, pressure, and equipment-health measurements."
  - Phase III, Step 3: CT_j = Enc_{k_i}(d_j) using fog-scoped AES-GCM key
"""

import random
from typing import Optional, List, Tuple

from src.crypto import aes_gcm


class Sensor:
    """
    IIoT Sensor entity (S_j).

    Generates simulated industrial sensor readings as integers in
    [0, max_sensor_value] and encrypts them using the fog-scoped
    AES-GCM key k_i before transmission to the assigned fog node.
    """

    def __init__(self, sensor_id: int, assigned_fog_id: int,
                 aes_key: Optional[bytes] = None,
                 max_value: int = 65535):
        """
        Initialize a sensor.

        Args:
            sensor_id: Unique sensor identifier j.
            assigned_fog_id: ID of assigned fog node (Γ(S_j) = F_i).
            aes_key: Fog-scoped AES-GCM key k_i (provisioned by KRM).
            max_value: Maximum sensor reading (default 65535 per plan §3.1).
        """
        self.sensor_id = sensor_id
        self.assigned_fog_id = assigned_fog_id
        self.aes_key = aes_key
        self.active = True   # Act_j(t): 1 if active, 0 if disconnected
        self.max_value = max_value

        # Trace data for Intel Berkeley Lab trace support (§3.2)
        # Each entry is (timestamp, value) — loaded externally
        self.trace_data: Optional[List[Tuple[float, int]]] = None
        self._trace_index: int = 0

    def generate_reading(self) -> int:
        """
        Generate a simulated industrial sensor reading.

        Returns integer d_j ∈ [0, max_sensor_value].

        If trace_data is loaded, reads from the trace sequentially.
        Otherwise, generates synthetic uniform random integers (§3.2 fallback).

        Paper: "d_j represents an industrial measurement — temperature,
               vibration, pressure — encoded as an integer so Paillier
               encryption can operate on it."
        """
        if not self.active:
            return 0

        if self.trace_data is not None and len(self.trace_data) > 0:
            return self._generate_from_trace()

        # Synthetic fallback: uniform random integers in [0, 65535] (§3.2)
        return random.randint(0, self.max_value)

    def _generate_from_trace(self) -> int:
        """Read next value from loaded trace data, wrapping around."""
        _, value = self.trace_data[self._trace_index % len(self.trace_data)]
        self._trace_index += 1
        # Clamp to valid range
        return max(0, min(self.max_value, int(value)))

    def encrypt_reading(self, reading: int) -> bytes:
        """
        Encrypt a sensor reading using fog-scoped AES-GCM.

        CT_j = Enc_{k_i}(d_j)

        Args:
            reading: Plaintext sensor value d_j (integer).

        Returns:
            AES-GCM ciphertext CT_j (36 bytes: 12 nonce + 8 ct + 16 tag).

        Paper: Phase III, Step 3 - Lightweight AES-GCM at sensor layer
               instead of heavy Paillier encryption
        """
        if self.aes_key is None:
            raise RuntimeError(f"Sensor {self.sensor_id}: AES key not provisioned")
        return aes_gcm.encrypt(self.aes_key, reading)

    def sense_and_encrypt(self):
        """
        Combined: generate a reading and encrypt it.

        Returns:
            Tuple (plaintext_value, ciphertext).
        """
        reading = self.generate_reading()
        ciphertext = self.encrypt_reading(reading)
        return reading, ciphertext

    def load_trace(self, trace_entries: List[Tuple[float, int]]):
        """
        Load trace data for trace-driven simulation.

        Args:
            trace_entries: List of (timestamp, integer_value) pairs.
                           Values should already be scaled to [0, max_value].

        Paper: Section 3.2 - "Using a real trace is strongly preferred.
               The Intel Berkeley Lab dataset is freely available."
        """
        self.trace_data = trace_entries
        self._trace_index = 0

    def deactivate(self):
        """Simulate sensor disconnection/outage."""
        self.active = False

    def activate(self):
        """Restore sensor to active state."""
        self.active = True

    def __repr__(self):
        return (f"Sensor(id={self.sensor_id}, fog={self.assigned_fog_id}, "
                f"active={self.active})")
