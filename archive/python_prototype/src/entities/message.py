"""
Message Entity Module
=======================
Models tagged communication messages for measuring recovery
communication overhead.

Experiment Plan Section 9.3:
    class Message:
        def __init__(self, type_, size_bytes, is_recovery=False):
            ...

    # At end of epoch:
    recovery_bytes = sum(m.size_bytes for m in epoch_messages
                          if m.is_recovery) / 1024  # KB
"""


class Message:
    """
    Tagged communication message for overhead tracking.

    Every recovery-related message is tagged with is_recovery=True
    so the simulator can separately measure recovery communication
    overhead (Exp. 6).

    Paper §9.3: "Tag every recovery-related message in the simulation"
    """

    def __init__(self, type_: str, size_bytes: int,
                 is_recovery: bool = False):
        """
        Args:
            type_: Message type (e.g., 'DSP', 'FSM', 'CT_AES', 'META').
            size_bytes: Size of the message payload in bytes.
            is_recovery: True if this message is part of recovery.
        """
        self.type_ = type_
        self.size_bytes = size_bytes
        self.is_recovery = is_recovery

    def __repr__(self):
        tag = " [RECOVERY]" if self.is_recovery else ""
        return f"Message({self.type_}, {self.size_bytes}B{tag})"
