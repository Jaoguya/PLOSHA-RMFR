#ifndef ROBUST_IIOT_SIM_HPP
#define ROBUST_IIOT_SIM_HPP

#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <map>

// Forward declarations for OpenSSL types used by shared crypto lib
#include <openssl/bn.h>
#include <openssl/ec.h>

// Shared crypto library
#include "../../../src/crypto/paillier.hpp"
#include "../../../src/crypto/modified_ecdsa.hpp"

// ============================================================================
// Data structures representing the PPDA system entities
// ============================================================================

// A single sensor reading parsed from plosha_dataset.csv
struct SensorReading {
    std::string timestamp;
    std::string sensor_id;
    std::string fog_node_id;
    double temperature;
    double pressure;
    double vibration;
    int is_failure;
};

// Sensor entity in the PPDA scheme
struct PPDASensor {
    std::string id;
    std::string edge_server_id;
    BIGNUM* priv_key;    // ECDSA private key
    EC_POINT* pub_key;   // ECDSA public key
};

// Edge Server entity
struct PPDAEdgeServer {
    std::string id;
    std::vector<int> subordinate_sensor_indices;  // indices into the sensor array
    BIGNUM* priv_key;
    EC_POINT* pub_key;
};

// Cloud Center entity
struct PPDACloudCenter {
    BIGNUM* priv_key;
    EC_POINT* pub_key;
    // Paillier keys are held by the Paillier object
};

// Timing results for a single aggregation round
struct AggregationTimings {
    double sensor_encrypt_ms;       // Total time for all sensors to encrypt
    double sensor_sign_ms;          // Total time for all sensors to sign
    double es_batch_verify_ms;      // Total time for ESs to batch-verify
    double es_aggregate_ms;         // Total time for ESs to homomorphically aggregate
    double es_noise_ms;             // Total time for ESs to add Laplace noise
    double es_sign_ms;              // Total time for ESs to sign aggregates
    double cc_batch_verify_ms;      // Total time for CC to batch-verify ES signatures
    double cc_decrypt_ms;           // Total time for CC to decrypt aggregates
    double total_ms;                // End-to-end aggregation latency
};

// ============================================================================
// Main simulation class
// ============================================================================

class RobustIIoTSimulation {
private:
    // Crypto engines (shared library)
    Paillier paillier_;
    ModifiedECDSA ecdsa_;

    // System entities
    PPDACloudCenter cloud_center_;
    std::vector<PPDAEdgeServer> edge_servers_;
    std::vector<PPDASensor> sensors_;

    // Dataset
    std::vector<SensorReading> all_readings_;

    // RNG for Laplace noise
    std::mt19937 rng_;

    // Configuration
    int num_sensors_;
    int num_edge_servers_;
    int paillier_key_bits_;
    double dp_epsilon_;        // Differential privacy budget

    // Helper: generate Laplace noise
    double LaplaceSample(double scale);

    // Helper: quantize a double sensor value to a BIGNUM integer
    void QuantizeToBignum(BIGNUM* out, double value, int scale_factor = 1000);

public:
    RobustIIoTSimulation();
    ~RobustIIoTSimulation();

    // Phase 0: Load dataset from CSV
    bool LoadDataset(const std::string& csv_path);

    // Phase 1: System initialization — generate all keys
    // num_sensors and num_es control how many entities to create
    // If num_sensors > dataset sensors, readings are reused cyclically
    void Initialize(int num_sensors, int num_edge_servers,
                    int paillier_bits = 1024, double dp_epsilon = 1.0);

    // Phase 2: Registration (mutual authentication)
    // Returns time in ms for the registration phase
    double RunRegistration();

    // Phase 3+4: Run a full aggregation round over a batch of readings
    // readings_per_sensor: how many readings each sensor reports (workload intensity)
    // Returns detailed timing breakdown
    AggregationTimings RunAggregationRound(int readings_per_sensor = 1);

    // Run aggregation with a specific number of micro-slots (for exp5)
    // Returns the loss exposure fraction when micro_slots_failed fail
    double RunLossExposure(int total_micro_slots, int micro_slots_failed);

    // Accessors
    int GetNumSensors() const { return num_sensors_; }
    int GetNumEdgeServers() const { return num_edge_servers_; }
    const std::vector<SensorReading>& GetReadings() const { return all_readings_; }
};

#endif // ROBUST_IIOT_SIM_HPP
