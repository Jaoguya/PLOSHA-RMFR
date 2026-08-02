"""
Dataset Transformer Module
=============================
Loads the single IoTSyn IIoT Network CSV and produces scheme-specific
data representations for PLOSHA-RMFR and all 4 baselines.

The IoTSyn dataset contains IIoT network traffic rows with columns:
    Timestamp, Source_IP, Dest_IP, Source_Port, Dest_Port, Protocol,
    Packet_Size, Connection_Duration, Packets_Sent, Bytes_Transferred,
    Flow_Rate, Device_Role, Function_Code, Attack_Type, Attack_Phase, Label

Each scheme interprets this raw data differently:
    - PLOSHA-RMFR: Packet_Size → sensor readings, Dest_IP → fog nodes
    - Ref [22] FedDQN: Connection_Duration → task exec time, Protocol → priority
    - Ref [24] PPDA: Bytes_Transferred → Paillier plaintext, ES clustering
    - Ref [37] HFWPF: Rows → workflow DAG tasks with deadlines
    - Ref [38] SEC: Flow_Rate → stream data rates, Protocol → function chains

Paper Reference:
    Experiment Plan §3.1-3.2: "d_j represents an industrial measurement
    encoded as an integer so Paillier encryption can operate on it.
    The integer is in the range [0, 65535] (16-bit unsigned)."
"""

import os
import csv
import hashlib
from datetime import datetime
from typing import List, Dict, Tuple, Optional, Any

import numpy as np


# ---------------------------------------------------------------------------
# Default dataset path (relative to project root)
# ---------------------------------------------------------------------------
_DEFAULT_CSV = os.path.join(
    os.path.dirname(__file__),
    "IoTSyn_IIoT_Network_20260404_134500.csv",
)

# Protocol priority mapping for Ref [22] K-Means task classification
# Higher values = higher industrial priority
PROTOCOL_PRIORITY = {
    "Modbus_TCP": 0.9,     # Critical SCADA control
    "DNP3": 0.85,          # Utility/power grid
    "OPC_UA": 0.8,         # Industrial automation
    "EtherNet_IP": 0.7,    # Factory-floor control
    "BACnet": 0.6,         # Building automation
    "MQTT": 0.4,           # Telemetry / monitoring
}

# Device-role → VM-tier mapping for Ref [37]
DEVICE_ROLE_VM_TIER = {
    "PLC": "compute_heavy",
    "SCADA_Server": "compute_heavy",
    "RTU": "compute_medium",
    "Engineering_Workstation": "compute_medium",
    "HMI": "io_heavy",
    "Historian": "storage_heavy",
    "External": "external",
}

# Protocol → serverless function chain for Ref [38]
PROTOCOL_FUNCTION_CHAIN = {
    "Modbus_TCP": ["parse_modbus", "validate_crc", "extract_registers", "aggregate", "store"],
    "DNP3": ["parse_dnp3", "validate_auth", "extract_points", "aggregate", "store"],
    "OPC_UA": ["parse_opcua", "decrypt_session", "extract_nodes", "aggregate", "store"],
    "EtherNet_IP": ["parse_eip", "validate_session", "extract_io", "aggregate", "store"],
    "BACnet": ["parse_bacnet", "validate_apdu", "extract_objects", "store"],
    "MQTT": ["parse_mqtt", "extract_payload", "store"],
}


class DatasetTransformer:
    """
    Unified dataset transformer for PLOSHA-RMFR and all 4 baselines.

    Loads the raw IoTSyn CSV once and provides scheme-specific
    transformation methods that produce the data representation
    appropriate for each paper's algorithm.
    """

    def __init__(self, csv_path: str = None, random_seed: int = 42):
        """
        Initialize the dataset transformer.

        Args:
            csv_path: Path to the IoTSyn CSV file. Uses default if None.
            random_seed: Seed for deterministic operations.
        """
        self.csv_path = csv_path or _DEFAULT_CSV
        self.random_seed = random_seed
        self._rng = np.random.RandomState(random_seed)

        # Raw data (loaded lazily)
        self._raw_rows: Optional[List[Dict[str, Any]]] = None
        self._header: Optional[List[str]] = None

        # Cached derivations
        self._internal_ips: Optional[List[str]] = None
        self._max_packet_size: Optional[int] = None
        self._max_bytes_transferred: Optional[int] = None
        self._max_flow_rate: Optional[float] = None
        self._max_connection_duration: Optional[float] = None

    # =========================================================================
    # Raw dataset loading
    # =========================================================================

    def load_raw_dataset(self) -> List[Dict[str, Any]]:
        """
        Load the IoTSyn CSV, skipping comment lines (# prefix).

        Returns:
            List of row dictionaries with typed values.

        Raises:
            FileNotFoundError: If the CSV file does not exist.
        """
        if self._raw_rows is not None:
            return self._raw_rows

        if not os.path.exists(self.csv_path):
            raise FileNotFoundError(
                f"Dataset not found: {self.csv_path}\n"
                f"Place the IoTSyn CSV in src/dataset/"
            )

        rows = []
        with open(self.csv_path, "r", encoding="utf-8") as f:
            # Skip comment lines
            data_lines = [line for line in f if not line.startswith("#")]

        reader = csv.DictReader(data_lines)
        self._header = reader.fieldnames

        for row in reader:
            typed_row = self._type_row(row)
            if typed_row is not None:
                rows.append(typed_row)

        self._raw_rows = rows

        # Cache extremes for normalization
        if rows:
            self._max_packet_size = max(r["Packet_Size"] for r in rows)
            self._max_bytes_transferred = max(
                r["Bytes_Transferred"] for r in rows
            )
            self._max_flow_rate = max(r["Flow_Rate"] for r in rows)
            self._max_connection_duration = max(
                r["Connection_Duration"] for r in rows
            )

        # Extract unique internal IPs (10.0.1.x as destinations)
        self._internal_ips = sorted(set(
            r["Dest_IP"] for r in rows
            if r["Dest_IP"].startswith("10.")
        ))

        return self._raw_rows

    @staticmethod
    def _type_row(row: Dict[str, str]) -> Optional[Dict[str, Any]]:
        """Convert string CSV row to typed dictionary."""
        try:
            return {
                "Timestamp": row["Timestamp"].strip('"'),
                "Source_IP": row["Source_IP"].strip(),
                "Dest_IP": row["Dest_IP"].strip(),
                "Source_Port": int(row["Source_Port"]),
                "Dest_Port": int(row["Dest_Port"]),
                "Protocol": row["Protocol"].strip(),
                "Packet_Size": int(row["Packet_Size"]),
                "Connection_Duration": float(row["Connection_Duration"]),
                "Packets_Sent": int(row["Packets_Sent"]),
                "Bytes_Transferred": int(row["Bytes_Transferred"]),
                "Flow_Rate": float(row["Flow_Rate"]),
                "Device_Role": row["Device_Role"].strip(),
                "Function_Code": int(row["Function_Code"]),
                "Attack_Type": row["Attack_Type"].strip(),
                "Attack_Phase": row["Attack_Phase"].strip(),
                "Label": row["Label"].strip(),
            }
        except (ValueError, KeyError):
            return None

    # =========================================================================
    # Common utilities
    # =========================================================================

    def assign_sensors_to_fog(
        self, num_fog_nodes: int
    ) -> Dict[int, List[str]]:
        """
        Deterministic sensor-to-fog assignment using internal Dest_IPs.

        Each unique internal IP is treated as a sensor endpoint.
        Assignment uses consistent hashing so the mapping is stable
        across runs with the same seed.

        Args:
            num_fog_nodes: Number of fog nodes to distribute across.

        Returns:
            Dict mapping fog_node_id → list of assigned Dest_IPs.
        """
        self.load_raw_dataset()
        assignment: Dict[int, List[str]] = {
            i: [] for i in range(num_fog_nodes)
        }

        for ip in self._internal_ips:
            # Consistent hash: SHA-256 of IP → mod num_fog_nodes
            h = int(hashlib.sha256(ip.encode()).hexdigest(), 16)
            fog_id = h % num_fog_nodes
            assignment[fog_id].append(ip)

        return assignment

    def derive_epochs(
        self, rows_per_epoch: int = 100
    ) -> List[List[Dict[str, Any]]]:
        """
        Partition dataset rows into aggregation epochs.

        Each epoch contains `rows_per_epoch` consecutive rows,
        representing sensor reports collected during one aggregation
        interval.

        Args:
            rows_per_epoch: Number of data rows per epoch.

        Returns:
            List of epochs, each a list of row dicts.
        """
        self.load_raw_dataset()
        epochs = []
        for i in range(0, len(self._raw_rows), rows_per_epoch):
            epoch_rows = self._raw_rows[i: i + rows_per_epoch]
            if epoch_rows:
                epochs.append(epoch_rows)
        return epochs

    def derive_failure_events(
        self, num_epochs: int, num_fog_nodes: int
    ) -> Dict[int, List[int]]:
        """
        Extract failure injection schedule from malicious traffic rows.

        Rows with Label=="Malicious" indicate attack activity. The timing
        and target IPs of these rows drive deterministic failure injection
        rather than using random probability.

        Args:
            num_epochs: Total number of epochs in the simulation.
            num_fog_nodes: Number of fog nodes.

        Returns:
            Dict mapping epoch_index → list of fog_node_ids to fail.
        """
        self.load_raw_dataset()

        # Count malicious rows per internal destination IP
        malicious_by_ip: Dict[str, int] = {}
        for row in self._raw_rows:
            if row["Label"] == "Malicious" and row["Dest_IP"].startswith("10."):
                ip = row["Dest_IP"]
                malicious_by_ip[ip] = malicious_by_ip.get(ip, 0) + 1

        # Map IPs to fog nodes
        ip_to_fog = {}
        for ip in self._internal_ips:
            h = int(hashlib.sha256(ip.encode()).hexdigest(), 16)
            ip_to_fog[ip] = h % num_fog_nodes

        # Distribute malicious events across epochs
        total_malicious = sum(malicious_by_ip.values())
        if total_malicious == 0:
            return {}

        # Walk through rows sequentially, accumulating malicious hits
        failure_schedule: Dict[int, List[int]] = {}
        rows_per_epoch = max(1, len(self._raw_rows) // num_epochs)

        for idx, row in enumerate(self._raw_rows):
            if row["Label"] != "Malicious":
                continue
            if not row["Dest_IP"].startswith("10."):
                continue

            epoch_idx = min(idx // rows_per_epoch, num_epochs - 1)
            fog_id = ip_to_fog.get(row["Dest_IP"])
            if fog_id is not None:
                if epoch_idx not in failure_schedule:
                    failure_schedule[epoch_idx] = []
                if fog_id not in failure_schedule[epoch_idx]:
                    failure_schedule[epoch_idx].append(fog_id)

        return failure_schedule

    def get_workload_from_trace(
        self, num_epochs: int, num_fog_nodes: int
    ) -> List[Dict[int, float]]:
        """
        Compute per-epoch, per-fog-node workload intensity from arrival rates.

        Workload is the fraction of total per-fog capacity consumed by
        the number of reports arriving in that epoch.

        Args:
            num_epochs: Number of epochs.
            num_fog_nodes: Number of fog nodes.

        Returns:
            List of dicts, one per epoch: {fog_id: workload_intensity}.
        """
        self.load_raw_dataset()

        ip_to_fog = {}
        for ip in self._internal_ips:
            h = int(hashlib.sha256(ip.encode()).hexdigest(), 16)
            ip_to_fog[ip] = h % num_fog_nodes

        rows_per_epoch = max(1, len(self._raw_rows) // num_epochs)
        epochs_workload = []

        for ep in range(num_epochs):
            start = ep * rows_per_epoch
            end = min(start + rows_per_epoch, len(self._raw_rows))
            epoch_rows = self._raw_rows[start:end]

            # Count arrivals per fog node
            arrivals: Dict[int, int] = {i: 0 for i in range(num_fog_nodes)}
            for row in epoch_rows:
                if row["Dest_IP"].startswith("10."):
                    fog_id = ip_to_fog.get(row["Dest_IP"], 0)
                    arrivals[fog_id] = arrivals.get(fog_id, 0) + 1

            # Normalize: capacity = rows_per_epoch / num_fog_nodes
            capacity = max(1, rows_per_epoch / num_fog_nodes)
            workload = {
                fid: min(1.0, count / capacity)
                for fid, count in arrivals.items()
            }
            epochs_workload.append(workload)

        return epochs_workload

    # =========================================================================
    # PLOSHA-RMFR transformation
    # =========================================================================

    def transform_for_plosha(
        self,
        num_sensors: int,
        num_fog_nodes: int,
        max_sensor_value: int = 65535,
    ) -> Dict[str, Any]:
        """
        Transform dataset for PLOSHA-RMFR.

        Produces:
            - sensor_traces: Dict[sensor_id → List[(timestamp, int_value)]]
              Each sensor gets a trace of integer readings d_j ∈ [0, 65535]
              derived from Packet_Size values.
            - fog_assignment: Dict[fog_id → List[sensor_id]]
            - failure_schedule: Dict[epoch → List[fog_id]]
            - epoch_workloads: List[Dict[fog_id → float]]

        Paper: Experiment Plan §3.1-3.2
            "d_j represents an industrial measurement encoded as an integer
            so Paillier encryption can operate on it."
        """
        self.load_raw_dataset()
        rows = self._raw_rows

        # --- Step 1: Extract sensor readings from Packet_Size ---
        # Scale Packet_Size to [0, max_sensor_value]
        max_ps = self._max_packet_size or 1

        # Filter to normal internal traffic for sensor readings
        normal_rows = [r for r in rows if r["Label"] == "Normal"]
        if not normal_rows:
            # Fallback: use all internal-destination rows
            normal_rows = [r for r in rows if r["Dest_IP"].startswith("10.")]

        # --- Step 2: Build per-sensor traces ---
        # Assign each unique (Source_IP, Dest_IP) pair as a "sensor"
        sensor_pairs = sorted(set(
            (r["Source_IP"], r["Dest_IP"])
            for r in normal_rows
            if r["Dest_IP"].startswith("10.")
        ))

        # If we have more requested sensors than pairs, wrap around
        sensor_traces: Dict[int, List[Tuple[float, int]]] = {}
        pair_rows: Dict[Tuple[str, str], List[Dict]] = {}
        for r in normal_rows:
            if r["Dest_IP"].startswith("10."):
                pair = (r["Source_IP"], r["Dest_IP"])
                if pair not in pair_rows:
                    pair_rows[pair] = []
                pair_rows[pair].append(r)

        for sid in range(num_sensors):
            if sensor_pairs:
                pair = sensor_pairs[sid % len(sensor_pairs)]
                src_rows = pair_rows.get(pair, [])
            else:
                src_rows = normal_rows

            trace = []
            for row in src_rows:
                # Scale Packet_Size → [0, max_sensor_value]
                scaled = int(row["Packet_Size"] / max_ps * max_sensor_value)
                scaled = max(0, min(max_sensor_value, scaled))
                # Use row index as timestamp proxy
                trace.append((0.0, scaled))  # timestamp filled by caller

            if not trace:
                # Ensure every sensor has at least one reading
                trace.append((0.0, self._rng.randint(0, max_sensor_value)))

            sensor_traces[sid] = trace

        # --- Step 3: Fog assignment ---
        fog_assignment: Dict[int, List[int]] = {
            i: [] for i in range(num_fog_nodes)
        }
        for sid in range(num_sensors):
            fog_id = sid % num_fog_nodes
            fog_assignment[fog_id].append(sid)

        # --- Step 4: Failure schedule from malicious rows ---
        num_epochs = max(1, len(rows) // max(1, num_sensors))
        failure_schedule = self.derive_failure_events(
            num_epochs, num_fog_nodes
        )

        # --- Step 5: Epoch workloads ---
        epoch_workloads = self.get_workload_from_trace(
            num_epochs, num_fog_nodes
        )

        return {
            "sensor_traces": sensor_traces,
            "fog_assignment": fog_assignment,
            "failure_schedule": failure_schedule,
            "epoch_workloads": epoch_workloads,
            "num_data_rows": len(rows),
            "num_normal_rows": len(normal_rows),
        }

    # =========================================================================
    # Ref [22] — FedDQN: Task scheduling with K-Means + DQN
    # =========================================================================

    def transform_for_ref22(
        self, num_sensors: int, num_fog_nodes: int
    ) -> Dict[str, Any]:
        """
        Transform dataset for Ref [22] FedDQN.

        FedDQN performs task scheduling with:
            - K-Means clustering on (execution_time, deadline) → K=3 priority buckets
            - State vector s = [queue_length, cpu_requirement, priority] (Eq. 25)
            - DQN action = select VM with highest Q-value

        Produces:
            - tasks: List[dict] with execution_time, cpu_requirement,
              priority, deadline per task.
            - queue_states: Per-epoch queue state vectors.

        Paper Ref [22]: Choppara & Mangalampalli, IEEE Access 2025
            "classifying tasks based on their execution time and deadlines"
        """
        self.load_raw_dataset()
        rows = self._raw_rows

        max_dur = self._max_connection_duration or 1.0
        max_ps = self._max_packet_size or 1

        tasks = []
        for row in rows:
            # execution_time ← Connection_Duration
            exec_time = row["Connection_Duration"]

            # cpu_requirement ← normalized Packet_Size
            cpu_req = row["Packet_Size"] / max_ps

            # priority ← protocol-based mapping
            protocol = row["Protocol"]
            priority = PROTOCOL_PRIORITY.get(protocol, 0.5)

            # deadline ← derived from protocol urgency + flow rate
            # Higher flow rate → tighter deadline
            max_fr = self._max_flow_rate or 1.0
            flow_factor = row["Flow_Rate"] / max_fr
            # Industrial protocols get tighter deadlines
            base_deadline = exec_time * (2.0 + (1.0 - priority))
            deadline = base_deadline * (1.0 + 0.5 * (1.0 - flow_factor))
            deadline = max(exec_time * 1.1, deadline)  # At least 10% slack

            tasks.append({
                "execution_time": exec_time,
                "cpu_requirement": cpu_req,
                "priority": priority,
                "deadline": deadline,
                "protocol": protocol,
                "device_role": row["Device_Role"],
                "label": row["Label"],
            })

        # Derive per-epoch queue states
        rows_per_epoch = max(1, len(rows) // max(1, num_sensors // num_fog_nodes))
        queue_states = []
        for i in range(0, len(tasks), rows_per_epoch):
            epoch_tasks = tasks[i: i + rows_per_epoch]
            if epoch_tasks:
                avg_queue = len(epoch_tasks) / max(1, num_fog_nodes)
                avg_cpu = np.mean([t["cpu_requirement"] for t in epoch_tasks])
                avg_pri = np.mean([t["priority"] for t in epoch_tasks])
                queue_states.append({
                    "queue_length": min(1.0, avg_queue / rows_per_epoch),
                    "cpu_requirement": avg_cpu,
                    "priority": avg_pri,
                })

        return {
            "tasks": tasks,
            "queue_states": queue_states,
            "num_tasks": len(tasks),
            "num_fog_nodes": num_fog_nodes,
        }

    # =========================================================================
    # Ref [24] — PPDA: Privacy-preserving data aggregation
    # =========================================================================

    def transform_for_ref24(
        self,
        num_sensors: int,
        num_fog_nodes: int,
        max_sensor_value: int = 65535,
    ) -> Dict[str, Any]:
        """
        Transform dataset for Ref [24] PPDA.

        PPDA performs Paillier-encrypted aggregation with ECDSA batch
        verification at Edge Servers (ES).

        Produces:
            - sensor_readings: List[dict] with sensor_id, es_id,
              plaintext m_ij for Paillier encryption.
            - es_batches: Dict[es_id → batch_size] for batch verification.

        Paper Ref [24]: Shang et al., IEEE TII 2024
            "c_ij = g^{m_ij} · R_ij^N mod N²" (Paillier encryption)
        """
        self.load_raw_dataset()
        rows = self._raw_rows

        max_bt = self._max_bytes_transferred or 1

        # Each row → one sensor reading
        readings = []
        for idx, row in enumerate(rows):
            sensor_id = idx % num_sensors

            # m_ij ← Bytes_Transferred scaled to integer for Paillier
            m_ij = int(row["Bytes_Transferred"] / max_bt * max_sensor_value)
            m_ij = max(0, min(max_sensor_value, m_ij))

            # ES assignment via Dest_IP consistent hash
            if row["Dest_IP"].startswith("10."):
                h = int(hashlib.sha256(
                    row["Dest_IP"].encode()
                ).hexdigest(), 16)
                es_id = h % num_fog_nodes
            else:
                es_id = sensor_id % num_fog_nodes

            readings.append({
                "sensor_id": sensor_id,
                "es_id": es_id,
                "m_ij": m_ij,
                "timestamp": row["Timestamp"],
            })

        # Batch sizes per ES (for batch verification cost)
        es_batches: Dict[int, int] = {i: 0 for i in range(num_fog_nodes)}
        for r in readings:
            es_batches[r["es_id"]] += 1

        return {
            "sensor_readings": readings,
            "es_batches": es_batches,
            "num_readings": len(readings),
            "num_edge_servers": num_fog_nodes,
        }

    # =========================================================================
    # Ref [37] — HFWPF: Hybrid fault-tolerant workflow scheduling
    # =========================================================================

    def transform_for_ref37(
        self, num_sensors: int, num_fog_nodes: int
    ) -> Dict[str, Any]:
        """
        Transform dataset for Ref [37] HFWPF.

        HFWPF performs workflow-level fault-tolerant scheduling with
        replication and resubmission across VMs.

        Produces:
            - workflow_tasks: List[dict] with exec_time, data_size,
              deadline, vm_tier, and DAG dependency info.
            - dag_edges: List[(parent_task_id, child_task_id)] for
              precedence constraints.

        Paper Ref [37]: Ren & Yao, IEEE TSC 2026
            "w(X) = E(X) + √D(X)" (Eq. 1, stochastic exec time)
            "resubmission if dl_ij - st_ij ≥ 2×(T_boot + max TT + ψ)" (Eq. 8)
        """
        self.load_raw_dataset()
        rows = self._raw_rows

        max_dur = self._max_connection_duration or 1.0
        max_bt = self._max_bytes_transferred or 1
        max_fr = self._max_flow_rate or 1.0

        # Build workflow tasks
        tasks = []
        for idx, row in enumerate(rows):
            exec_time = row["Connection_Duration"]
            data_size = row["Bytes_Transferred"]

            # Deadline derived from Flow_Rate
            # Higher flow rate → tighter deadline (more urgent)
            flow_norm = row["Flow_Rate"] / max_fr
            deadline = exec_time * (3.0 - 1.5 * flow_norm)
            deadline = max(exec_time * 1.2, deadline)

            # VM tier from Device_Role
            vm_tier = DEVICE_ROLE_VM_TIER.get(
                row["Device_Role"], "compute_medium"
            )

            tasks.append({
                "task_id": idx,
                "exec_time": exec_time,
                "data_size": data_size,
                "deadline": deadline,
                "vm_tier": vm_tier,
                "device_role": row["Device_Role"],
                "protocol": row["Protocol"],
                "label": row["Label"],
            })

        # Build DAG edges: sequential dependency within same
        # Source_IP → Dest_IP flow
        flow_last_task: Dict[Tuple[str, str], int] = {}
        dag_edges = []
        for task in tasks:
            row = rows[task["task_id"]]
            flow_key = (row["Source_IP"], row["Dest_IP"])
            if flow_key in flow_last_task:
                parent_id = flow_last_task[flow_key]
                dag_edges.append((parent_id, task["task_id"]))
            flow_last_task[flow_key] = task["task_id"]

        return {
            "workflow_tasks": tasks,
            "dag_edges": dag_edges,
            "num_tasks": len(tasks),
            "num_vms": num_fog_nodes,
        }

    # =========================================================================
    # Ref [38] — SEC: Fault-tolerant data stream processing
    # =========================================================================

    def transform_for_ref38(
        self, num_sensors: int, num_fog_nodes: int
    ) -> Dict[str, Any]:
        """
        Transform dataset for Ref [38] SEC.

        SEC performs serverless data stream processing with MT-LSTM
        prediction and active-standby failover.

        Produces:
            - stream_requests: List[dict] with data_rate, function_chain,
              cloudlet_id per stream.
            - cloudlet_mapping: Dict[cloudlet_id → List[stream_ids]]
            - timescale_groups: Per-stream g_m = ⌈log₂ ρ̃_m⌉ - 1 (Eq. 6)

        Paper Ref [38]: Xu et al., IEEE TSC 2026
            "ρ̂_{m,τ+1}" (predicted data rate)
            "g_m = ⌈log₂ ρ̃_m⌉ - 1" (Eq. 6, timescale groups)
        """
        self.load_raw_dataset()
        rows = self._raw_rows

        max_fr = self._max_flow_rate or 1.0

        # Each unique Source_IP flow → one stream request
        flows: Dict[str, List[Dict]] = {}
        for row in rows:
            src = row["Source_IP"]
            if src not in flows:
                flows[src] = []
            flows[src].append(row)

        stream_requests = []
        cloudlet_mapping: Dict[int, List[int]] = {
            i: [] for i in range(num_fog_nodes)
        }

        for stream_id, (src_ip, flow_rows) in enumerate(flows.items()):
            # Average data rate for this stream
            avg_rate = np.mean([r["Flow_Rate"] for r in flow_rows])

            # Function chain from dominant protocol
            protocol_counts: Dict[str, int] = {}
            for r in flow_rows:
                p = r["Protocol"]
                protocol_counts[p] = protocol_counts.get(p, 0) + 1
            dominant_protocol = max(protocol_counts, key=protocol_counts.get)
            function_chain = PROTOCOL_FUNCTION_CHAIN.get(
                dominant_protocol,
                ["parse", "process", "store"],
            )

            # Cloudlet assignment via Dest_IP
            dest_ips = [r["Dest_IP"] for r in flow_rows
                        if r["Dest_IP"].startswith("10.")]
            if dest_ips:
                primary_dest = max(set(dest_ips), key=dest_ips.count)
                h = int(hashlib.sha256(
                    primary_dest.encode()
                ).hexdigest(), 16)
                cloudlet_id = h % num_fog_nodes
            else:
                cloudlet_id = stream_id % num_fog_nodes

            # Timescale groups: g_m = max(1, ⌈log₂ ρ̃_m⌉ - 1)  (Eq. 6)
            rate_normalized = max(1.0, avg_rate)
            g_m = max(1, int(np.ceil(np.log2(rate_normalized))) - 1)

            stream_requests.append({
                "stream_id": stream_id,
                "source_ip": src_ip,
                "data_rate": avg_rate,
                "data_rate_normalized": avg_rate / max_fr,
                "function_chain": function_chain,
                "num_functions": len(function_chain),
                "cloudlet_id": cloudlet_id,
                "timescale_groups": g_m,
                "dominant_protocol": dominant_protocol,
                "num_rows": len(flow_rows),
            })

            cloudlet_mapping[cloudlet_id].append(stream_id)

        return {
            "stream_requests": stream_requests,
            "cloudlet_mapping": cloudlet_mapping,
            "num_streams": len(stream_requests),
            "num_cloudlets": num_fog_nodes,
        }

    # =========================================================================
    # Summary / info
    # =========================================================================

    def get_dataset_summary(self) -> Dict[str, Any]:
        """Return a summary of the loaded dataset."""
        self.load_raw_dataset()
        rows = self._raw_rows

        normal_count = sum(1 for r in rows if r["Label"] == "Normal")
        malicious_count = sum(1 for r in rows if r["Label"] == "Malicious")

        protocols = {}
        for r in rows:
            p = r["Protocol"]
            protocols[p] = protocols.get(p, 0) + 1

        device_roles = {}
        for r in rows:
            d = r["Device_Role"]
            device_roles[d] = device_roles.get(d, 0) + 1

        return {
            "total_rows": len(rows),
            "normal_rows": normal_count,
            "malicious_rows": malicious_count,
            "attack_rate": malicious_count / max(1, len(rows)),
            "num_internal_ips": len(self._internal_ips),
            "protocols": protocols,
            "device_roles": device_roles,
            "max_packet_size": self._max_packet_size,
            "max_bytes_transferred": self._max_bytes_transferred,
            "max_flow_rate": self._max_flow_rate,
            "max_connection_duration": self._max_connection_duration,
            "first_timestamp": rows[0]["Timestamp"] if rows else None,
            "last_timestamp": rows[-1]["Timestamp"] if rows else None,
        }
