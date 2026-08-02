import os
import csv
import numpy as np
from typing import Dict, List, Any, Tuple

PROTOCOL_PRIORITY = {
    "Modbus_TCP": 0.9,
    "MQTT": 0.8,
    "OPC_UA": 0.8,
    "HTTP": 0.3,
    "HTTPS": 0.4,
    "DNS": 0.2
}

class DatasetTransformer:
    def __init__(self, plosha_csv_path: str = None, random_seed: int = 42):
        self.plosha_csv_path = plosha_csv_path or os.path.join(os.path.dirname(__file__), "plosha_dataset.csv")
        self.random_seed = random_seed
        self._rng = np.random.RandomState(random_seed)

    def generate_massive_plosha_dataset(self, output_path: str, num_sensors: int = 12600, num_epochs: int = 100):
        """
        Generate a massive PLOSHA dataset from the Smart Manufacturing Kaggle dataset.
        Applies +/- 10% Gaussian jitter to duplicated machines to guarantee heterogeneity.
        """
        base_dir = os.path.dirname(__file__)
        smart_data_path = os.path.join(base_dir, "smart_manufacturing_data.csv")
        
        # Load base machines from Kaggle Dataset
        base_machines = {}
        with open(smart_data_path, 'r', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for r in reader:
                mid = int(r["machine_id"])
                if mid not in base_machines:
                    base_machines[mid] = []
                base_machines[mid].append(r)
        
        machine_ids = sorted(list(base_machines.keys()))
        num_base_machines = len(machine_ids)
        print(f"Loaded {num_base_machines} unique base machines from Smart Manufacturing Dataset.")
        
        fieldnames = ["Timestamp", "Sensor_ID", "Fog_Node_ID", "Temperature", "Pressure", 
                      "Vibration", "Is_Failure", "Protocol", "Connection_Duration", 
                      "Flow_Rate", "Bytes_Transferred", "Packet_Size", "Device_Role", 
                      "Source_IP", "Dest_IP", "Label"]
        
        print(f"Generating {num_sensors * num_epochs} rows for {num_sensors} virtual sensors...")
        
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            
            # For network features
            protocols = list(PROTOCOL_PRIORITY.keys())
            
            # Generate epochs
            for epoch in range(num_epochs):
                if epoch % 10 == 0:
                    print(f"Generating epoch {epoch}/{num_epochs}...")
                    
                for sid in range(num_sensors):
                    base_mid = machine_ids[sid % num_base_machines]
                    base_history = base_machines[base_mid]
                    # Select a random historical state from this machine for heterogeneity
                    base_r = base_history[(epoch + sid) % len(base_history)]
                    
                    # Apply Gaussian Jitter (mean=1.0, std=0.05) -> ~ +/- 10%
                    jitter = lambda val: max(0.0, float(val) * self._rng.normal(1.0, 0.05))
                    
                    temperature = jitter(base_r["temperature"])
                    pressure = jitter(base_r["pressure"])
                    vibration = jitter(base_r["vibration"])
                    
                    is_failure = int(base_r["anomaly_flag"])
                    label = "Malicious" if is_failure == 1 else "Normal"
                    
                    # Synthesize reasonable IIoT network values based on failure state
                    base_bytes = 512 if is_failure == 0 else 8192
                    bytes_transferred = int(jitter(base_bytes))
                    packet_size = 512
                    connection_duration = jitter(0.2 if is_failure == 0 else 0.8)
                    flow_rate = bytes_transferred / connection_duration
                    protocol = self._rng.choice(protocols)
                    
                    writer.writerow({
                        "Timestamp": f"2026-04-01 {epoch//60:02d}:{epoch%60:02d}:00",
                        "Sensor_ID": f"S{sid}",
                        "Fog_Node_ID": f"F{sid % 50}", # Will be reassigned logically by DES Engine anyway
                        "Temperature": round(temperature, 4),
                        "Pressure": round(pressure, 4),
                        "Vibration": round(vibration, 4),
                        "Is_Failure": is_failure,
                        "Protocol": protocol,
                        "Connection_Duration": round(connection_duration, 4),
                        "Flow_Rate": round(flow_rate, 2),
                        "Bytes_Transferred": bytes_transferred,
                        "Packet_Size": packet_size,
                        "Device_Role": "Sensor",
                        "Source_IP": f"10.0.{(sid//256)%256}.{sid%256}",
                        "Dest_IP": f"10.0.1.{sid % 50}",
                        "Label": label
                    })

if __name__ == '__main__':
    dt = DatasetTransformer()
    output_csv = os.path.join(os.path.dirname(__file__), "plosha_dataset.csv")
    dt.generate_massive_plosha_dataset(output_csv, num_sensors=12600, num_epochs=100)
    print("Done! Massive realistic dataset generated.")
