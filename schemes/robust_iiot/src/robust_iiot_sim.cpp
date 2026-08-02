#include "robust_iiot_sim.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cassert>

// ============================================================================
// Constructor / Destructor
// ============================================================================

RobustIIoTSimulation::RobustIIoTSimulation()
    : ecdsa_(NID_X9_62_prime256v1),
      num_sensors_(0),
      num_edge_servers_(0),
      paillier_key_bits_(1024),
      dp_epsilon_(1.0)
{
    rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    cloud_center_.priv_key = BN_new();
    cloud_center_.pub_key = EC_POINT_new(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
}

RobustIIoTSimulation::~RobustIIoTSimulation() {
    // Free cloud center keys
    if (cloud_center_.priv_key) BN_free(cloud_center_.priv_key);
    if (cloud_center_.pub_key) EC_POINT_free(cloud_center_.pub_key);

    // Free sensor keys
    for (auto& s : sensors_) {
        if (s.priv_key) BN_free(s.priv_key);
        if (s.pub_key) EC_POINT_free(s.pub_key);
    }

    // Free edge server keys
    for (auto& es : edge_servers_) {
        if (es.priv_key) BN_free(es.priv_key);
        if (es.pub_key) EC_POINT_free(es.pub_key);
    }
}

// ============================================================================
// Helpers
// ============================================================================

double RobustIIoTSimulation::LaplaceSample(double scale) {
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    double u = uniform(rng_) - 0.5;
    return -scale * ((u > 0) - (u < 0)) * std::log(1.0 - 2.0 * std::abs(u));
}

void RobustIIoTSimulation::QuantizeToBignum(BIGNUM* out, double value, int scale_factor) {
    // Quantize to integer: round(value * scale_factor)
    long long int_val = static_cast<long long>(std::round(value * scale_factor));
    if (int_val < 0) int_val = 0; // Paillier operates on non-negative integers
    BN_set_word(out, static_cast<unsigned long>(int_val));
}

// ============================================================================
// Phase 0: Load Dataset
// ============================================================================

bool RobustIIoTSimulation::LoadDataset(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open dataset: " << csv_path << std::endl;
        return false;
    }

    std::string line;
    // Skip header
    std::getline(file, line);

    all_readings_.clear();
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string token;
        SensorReading r;

        std::getline(ss, r.timestamp, ',');
        std::getline(ss, r.sensor_id, ',');
        std::getline(ss, r.fog_node_id, ',');

        std::getline(ss, token, ','); r.temperature = std::stod(token);
        std::getline(ss, token, ','); r.pressure = std::stod(token);
        std::getline(ss, token, ','); r.vibration = std::stod(token);
        std::getline(ss, token, ','); r.is_failure = std::stoi(token);

        all_readings_.push_back(r);
    }

    file.close();
    std::cout << "Loaded " << all_readings_.size() << " readings from dataset." << std::endl;
    return !all_readings_.empty();
}

// ============================================================================
// Phase 1: System Initialization
// ============================================================================

void RobustIIoTSimulation::Initialize(int num_sensors, int num_edge_servers,
                                       int paillier_bits, double dp_epsilon) {
    num_sensors_ = num_sensors;
    num_edge_servers_ = num_edge_servers;
    paillier_key_bits_ = paillier_bits;
    dp_epsilon_ = dp_epsilon;

    // --- Paillier Key Generation (CC holds the keys) ---
    std::cout << "Generating Paillier keys (" << paillier_bits << "-bit)..." << std::endl;
    paillier_.KeyGen(paillier_bits);

    // --- ECDSA Key Generation for Cloud Center ---
    ecdsa_.KeyGen(cloud_center_.priv_key, cloud_center_.pub_key);

    // --- Create Edge Servers ---
    // Free old ES keys if reinitializing
    for (auto& es : edge_servers_) {
        if (es.priv_key) BN_free(es.priv_key);
        if (es.pub_key) EC_POINT_free(es.pub_key);
    }
    edge_servers_.clear();
    edge_servers_.resize(num_edge_servers);

    for (int i = 0; i < num_edge_servers; i++) {
        edge_servers_[i].id = "ES" + std::to_string(i);
        edge_servers_[i].priv_key = BN_new();
        edge_servers_[i].pub_key = EC_POINT_new(
            EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
        ecdsa_.KeyGen(edge_servers_[i].priv_key, edge_servers_[i].pub_key);
    }

    // --- Create Sensors ---
    // Free old sensor keys if reinitializing
    for (auto& s : sensors_) {
        if (s.priv_key) BN_free(s.priv_key);
        if (s.pub_key) EC_POINT_free(s.pub_key);
    }
    sensors_.clear();
    sensors_.resize(num_sensors);

    for (int j = 0; j < num_sensors; j++) {
        sensors_[j].id = "S" + std::to_string(j);
        // Assign to edge server round-robin
        sensors_[j].edge_server_id = "ES" + std::to_string(j % num_edge_servers);
        sensors_[j].priv_key = BN_new();
        sensors_[j].pub_key = EC_POINT_new(
            EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
        ecdsa_.KeyGen(sensors_[j].priv_key, sensors_[j].pub_key);

        // Register sensor with its edge server
        edge_servers_[j % num_edge_servers].subordinate_sensor_indices.push_back(j);
    }

    std::cout << "Initialized: " << num_sensors << " sensors, "
              << num_edge_servers << " edge servers." << std::endl;
}

// ============================================================================
// Phase 2: Registration (mutual authentication)
// ============================================================================

double RobustIIoTSimulation::RunRegistration() {
    auto t_start = std::chrono::high_resolution_clock::now();

    // Each ES registers with CC: sign(ID||timestamp), CC verifies, CC signs back
    for (auto& es : edge_servers_) {
        std::string reg_msg = es.id + "|REG|" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        // ES signs registration message
        Signature* es_sig = ecdsa_.CreateSignature();
        ecdsa_.Sign(es_sig, reg_msg, es.priv_key);

        // CC verifies ES signature
        bool valid = ecdsa_.Verify(reg_msg, es_sig, es.pub_key);
        assert(valid && "ES registration signature verification failed");

        // CC signs confirmation
        std::string cc_msg = "CC|ACK|" + es.id;
        Signature* cc_sig = ecdsa_.CreateSignature();
        ecdsa_.Sign(cc_sig, cc_msg, cloud_center_.priv_key);

        // ES verifies CC signature
        valid = ecdsa_.Verify(cc_msg, cc_sig, cloud_center_.pub_key);
        assert(valid && "CC confirmation signature verification failed");

        ecdsa_.FreeSignature(es_sig);
        ecdsa_.FreeSignature(cc_sig);
    }

    // Each Sensor registers with CC (same mutual auth flow)
    for (auto& sensor : sensors_) {
        std::string reg_msg = sensor.id + "|REG|" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

        Signature* s_sig = ecdsa_.CreateSignature();
        ecdsa_.Sign(s_sig, reg_msg, sensor.priv_key);

        bool valid = ecdsa_.Verify(reg_msg, s_sig, sensor.pub_key);
        assert(valid && "Sensor registration signature verification failed");

        std::string cc_msg = "CC|ACK|" + sensor.id;
        Signature* cc_sig = ecdsa_.CreateSignature();
        ecdsa_.Sign(cc_sig, cc_msg, cloud_center_.priv_key);

        valid = ecdsa_.Verify(cc_msg, cc_sig, cloud_center_.pub_key);
        assert(valid && "CC confirmation to sensor verification failed");

        ecdsa_.FreeSignature(s_sig);
        ecdsa_.FreeSignature(cc_sig);
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    std::cout << "Registration completed in " << elapsed_ms << " ms" << std::endl;
    return elapsed_ms;
}

// ============================================================================
// Phase 3+4: Full Aggregation Round
// ============================================================================

AggregationTimings RobustIIoTSimulation::RunAggregationRound(int readings_per_sensor) {
    AggregationTimings timings = {};
    auto t_total_start = std::chrono::high_resolution_clock::now();

    // -----------------------------------------------------------------------
    // STEP 1: Sensor — Encrypt + Sign
    // Each sensor encrypts its reading with Paillier and signs with ECDSA
    // -----------------------------------------------------------------------

    // Store per-ES: ciphertexts, signatures, messages, public keys
    struct ESInbox {
        std::vector<BIGNUM*> ciphertexts;
        std::vector<Signature*> signatures;
        std::vector<std::string> messages;
        std::vector<EC_POINT*> pub_keys;
    };
    std::vector<ESInbox> es_inboxes(num_edge_servers_);

    auto t_enc_start = std::chrono::high_resolution_clock::now();
    auto t_sign_start = t_enc_start; // Will be updated

    // Encryption phase
    for (int j = 0; j < num_sensors_; j++) {
        int es_idx = j % num_edge_servers_;

        for (int r = 0; r < readings_per_sensor; r++) {
            // Get a reading (cycle through dataset if needed)
            int reading_idx = (j * readings_per_sensor + r) % all_readings_.size();
            const SensorReading& reading = all_readings_[reading_idx];

            // Quantize temperature to integer for Paillier
            BIGNUM* plaintext = BN_new();
            QuantizeToBignum(plaintext, reading.temperature);

            // Real Paillier encryption: C = g^m * r^N mod N^2
            BIGNUM* ciphertext = BN_new();
            paillier_.Encrypt(ciphertext, plaintext);

            es_inboxes[es_idx].ciphertexts.push_back(ciphertext);
            BN_free(plaintext);
        }
    }
    auto t_enc_end = std::chrono::high_resolution_clock::now();
    timings.sensor_encrypt_ms = std::chrono::duration<double, std::milli>(t_enc_end - t_enc_start).count();

    // Signing phase
    t_sign_start = std::chrono::high_resolution_clock::now();
    for (int j = 0; j < num_sensors_; j++) {
        int es_idx = j % num_edge_servers_;

        for (int r = 0; r < readings_per_sensor; r++) {
            int ct_idx = 0;
            // Find the ciphertext index for this sensor in the ES inbox
            int local_sensor_order = j / num_edge_servers_;
            ct_idx = local_sensor_order * readings_per_sensor + r;
            if (ct_idx >= (int)es_inboxes[es_idx].ciphertexts.size())
                ct_idx = es_inboxes[es_idx].ciphertexts.size() - 1;

            // Build message: ID || ciphertext_hex || timestamp
            char* ct_hex = BN_bn2hex(es_inboxes[es_idx].ciphertexts[ct_idx]);
            std::string msg = sensors_[j].id + "|" + std::string(ct_hex) + "|" +
                std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            OPENSSL_free(ct_hex);

            // Real ECDSA signature
            Signature* sig = ecdsa_.CreateSignature();
            ecdsa_.Sign(sig, msg, sensors_[j].priv_key);

            es_inboxes[es_idx].signatures.push_back(sig);
            es_inboxes[es_idx].messages.push_back(msg);
            es_inboxes[es_idx].pub_keys.push_back(sensors_[j].pub_key);
        }
    }
    auto t_sign_end = std::chrono::high_resolution_clock::now();
    timings.sensor_sign_ms = std::chrono::duration<double, std::milli>(t_sign_end - t_sign_start).count();

    // -----------------------------------------------------------------------
    // STEP 2: Edge Server — Batch Verify + Aggregate + Noise + Sign
    // -----------------------------------------------------------------------

    // Batch verification
    auto t_bv_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_edge_servers_; i++) {
        if (es_inboxes[i].signatures.empty()) continue;

        // Real batch verification of all sensor signatures for this ES
        bool batch_valid = ecdsa_.BatchVerify(
            es_inboxes[i].messages,
            es_inboxes[i].signatures,
            es_inboxes[i].pub_keys
        );
        // In a real system, invalid batches would trigger individual verification
        // For the simulation, we assert correctness
        assert(batch_valid && "Batch verification failed at ES");
    }
    auto t_bv_end = std::chrono::high_resolution_clock::now();
    timings.es_batch_verify_ms = std::chrono::duration<double, std::milli>(t_bv_end - t_bv_start).count();

    // Homomorphic aggregation
    auto t_agg_start = std::chrono::high_resolution_clock::now();
    std::vector<BIGNUM*> aggregated_ciphertexts(num_edge_servers_);
    for (int i = 0; i < num_edge_servers_; i++) {
        if (es_inboxes[i].ciphertexts.empty()) {
            aggregated_ciphertexts[i] = nullptr;
            continue;
        }

        // Start with first ciphertext, aggregate the rest
        aggregated_ciphertexts[i] = BN_new();
        BN_copy(aggregated_ciphertexts[i], es_inboxes[i].ciphertexts[0]);

        BIGNUM* temp = BN_new();
        for (size_t c = 1; c < es_inboxes[i].ciphertexts.size(); c++) {
            // Real homomorphic addition: C_agg = C_agg * C_i mod N^2
            paillier_.Aggregate(temp, aggregated_ciphertexts[i],
                                es_inboxes[i].ciphertexts[c]);
            BN_copy(aggregated_ciphertexts[i], temp);
        }
        BN_free(temp);
    }
    auto t_agg_end = std::chrono::high_resolution_clock::now();
    timings.es_aggregate_ms = std::chrono::duration<double, std::milli>(t_agg_end - t_agg_start).count();

    // Add Laplace noise for differential privacy
    auto t_noise_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_edge_servers_; i++) {
        if (!aggregated_ciphertexts[i]) continue;

        // Generate Laplace noise and encrypt it
        double noise = LaplaceSample(1.0 / dp_epsilon_);
        BIGNUM* noise_bn = BN_new();
        QuantizeToBignum(noise_bn, std::abs(noise));

        BIGNUM* noise_ct = BN_new();
        paillier_.Encrypt(noise_ct, noise_bn);

        // Multiply into aggregate: ĉi = ci * g^m̂i
        BIGNUM* noisy_agg = BN_new();
        paillier_.Aggregate(noisy_agg, aggregated_ciphertexts[i], noise_ct);
        BN_copy(aggregated_ciphertexts[i], noisy_agg);

        BN_free(noise_bn);
        BN_free(noise_ct);
        BN_free(noisy_agg);
    }
    auto t_noise_end = std::chrono::high_resolution_clock::now();
    timings.es_noise_ms = std::chrono::duration<double, std::milli>(t_noise_end - t_noise_start).count();

    // ES signs aggregate and sends to CC
    auto t_es_sign_start = std::chrono::high_resolution_clock::now();
    std::vector<Signature*> es_signatures(num_edge_servers_);
    std::vector<std::string> es_messages(num_edge_servers_);
    std::vector<EC_POINT*> es_pub_keys(num_edge_servers_);

    for (int i = 0; i < num_edge_servers_; i++) {
        if (!aggregated_ciphertexts[i]) {
            es_signatures[i] = nullptr;
            continue;
        }

        char* agg_hex = BN_bn2hex(aggregated_ciphertexts[i]);
        es_messages[i] = edge_servers_[i].id + "|" + std::string(agg_hex) + "|" +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        OPENSSL_free(agg_hex);

        es_signatures[i] = ecdsa_.CreateSignature();
        ecdsa_.Sign(es_signatures[i], es_messages[i], edge_servers_[i].priv_key);
        es_pub_keys[i] = edge_servers_[i].pub_key;
    }
    auto t_es_sign_end = std::chrono::high_resolution_clock::now();
    timings.es_sign_ms = std::chrono::duration<double, std::milli>(t_es_sign_end - t_es_sign_start).count();

    // -----------------------------------------------------------------------
    // STEP 3: Cloud Center — Batch Verify ES signatures + Decrypt
    // -----------------------------------------------------------------------

    // Filter out null entries for batch verify
    std::vector<std::string> valid_es_msgs;
    std::vector<Signature*> valid_es_sigs;
    std::vector<EC_POINT*> valid_es_pks;
    std::vector<BIGNUM*> valid_agg_cts;

    for (int i = 0; i < num_edge_servers_; i++) {
        if (es_signatures[i] && aggregated_ciphertexts[i]) {
            valid_es_msgs.push_back(es_messages[i]);
            valid_es_sigs.push_back(es_signatures[i]);
            valid_es_pks.push_back(es_pub_keys[i]);
            valid_agg_cts.push_back(aggregated_ciphertexts[i]);
        }
    }

    auto t_cc_bv_start = std::chrono::high_resolution_clock::now();
    if (!valid_es_sigs.empty()) {
        bool cc_batch_valid = ecdsa_.BatchVerify(valid_es_msgs, valid_es_sigs, valid_es_pks);
        assert(cc_batch_valid && "CC batch verification of ES signatures failed");
    }
    auto t_cc_bv_end = std::chrono::high_resolution_clock::now();
    timings.cc_batch_verify_ms = std::chrono::duration<double, std::milli>(t_cc_bv_end - t_cc_bv_start).count();

    // Decrypt each aggregated ciphertext
    auto t_cc_dec_start = std::chrono::high_resolution_clock::now();
    for (auto* ct : valid_agg_cts) {
        BIGNUM* plaintext_sum = BN_new();
        paillier_.Decrypt(plaintext_sum, ct);
        // The result is the sum of all sensor readings + noise
        // In a real system, AD would analyze this
        BN_free(plaintext_sum);
    }
    auto t_cc_dec_end = std::chrono::high_resolution_clock::now();
    timings.cc_decrypt_ms = std::chrono::duration<double, std::milli>(t_cc_dec_end - t_cc_dec_start).count();

    auto t_total_end = std::chrono::high_resolution_clock::now();
    timings.total_ms = std::chrono::duration<double, std::milli>(t_total_end - t_total_start).count();

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    for (auto& inbox : es_inboxes) {
        for (auto* ct : inbox.ciphertexts) BN_free(ct);
        for (auto* sig : inbox.signatures) ecdsa_.FreeSignature(sig);
    }
    for (auto* ct : aggregated_ciphertexts) {
        if (ct) BN_free(ct);
    }
    for (auto* sig : es_signatures) {
        if (sig) ecdsa_.FreeSignature(sig);
    }

    return timings;
}

// ============================================================================
// Loss Exposure Experiment
// ============================================================================

double RobustIIoTSimulation::RunLossExposure(int total_micro_slots, int micro_slots_failed) {
    // Robust IIoT (Ref[24]) performs MONOLITHIC aggregation at the ES level.
    // It does NOT support micro-slot partitioning — all sensor data is
    // aggregated in a single Paillier homomorphic operation per edge server.
    //
    // When a failure occurs during this monolithic aggregation, the ENTIRE
    // aggregation result for that edge server is lost, regardless of how
    // many "micro-slots" the experiment parameter specifies.
    //
    // The micro-slot parameter is only meaningful for schemes that natively
    // support partitioned aggregation (like PLOSHA-RMFR).

    // With monolithic aggregation, failure_rate determines how many ESes fail.
    // Each failed ES loses ALL its data — micro-slots provide no isolation.
    double failure_rate = 0.10; // matches default experimental setup
    int num_failed_es = std::max(1, static_cast<int>(num_edge_servers_ * failure_rate));

    // Loss exposure: fraction of total data from failed ESes
    // This is independent of total_micro_slots because Ref[24] doesn't partition
    double loss_fraction = static_cast<double>(num_failed_es) /
                           static_cast<double>(num_edge_servers_);

    return loss_fraction;
}
