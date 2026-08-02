#include "../modified_ecdsa.hpp"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "Initializing Modified ECDSA (secp256r1)..." << std::endl;
    ModifiedECDSA ecdsa;

    // --- TEST 1: Single Signature ---
    std::cout << "\n--- Test 1: Single Signature ---" << std::endl;
    BIGNUM* priv_key1 = BN_new();
    EC_POINT* pub_key1 = EC_POINT_new(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
    
    ecdsa.KeyGen(priv_key1, pub_key1);
    std::string msg1 = "Sensor_Reading_ZoneA_123";
    
    Signature* sig1 = ecdsa.CreateSignature();
    ecdsa.Sign(sig1, msg1, priv_key1);
    
    bool ver1 = ecdsa.Verify(msg1, sig1, pub_key1);
    if (ver1) {
        std::cout << "SUCCESS: Single signature verified correctly!" << std::endl;
    } else {
        std::cout << "FAILED: Single signature verification failed." << std::endl;
        return 1;
    }

    // --- TEST 2: Batch Verification ---
    std::cout << "\n--- Test 2: Batch Verification (100 signatures) ---" << std::endl;
    
    const int BATCH_SIZE = 100;
    std::vector<BIGNUM*> priv_keys(BATCH_SIZE);
    std::vector<EC_POINT*> pub_keys(BATCH_SIZE);
    std::vector<std::string> msgs(BATCH_SIZE);
    std::vector<Signature*> sigs(BATCH_SIZE);

    // Generate keys, messages, and signatures
    for (int i = 0; i < BATCH_SIZE; i++) {
        priv_keys[i] = BN_new();
        pub_keys[i] = EC_POINT_new(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
        ecdsa.KeyGen(priv_keys[i], pub_keys[i]);
        
        msgs[i] = "Sensor_Data_" + std::to_string(i);
        sigs[i] = ecdsa.CreateSignature();
        ecdsa.Sign(sigs[i], msgs[i], priv_keys[i]);
    }

    // Benchmark Sequential Verification
    auto start_seq = std::chrono::high_resolution_clock::now();
    bool seq_valid = true;
    for (int i = 0; i < BATCH_SIZE; i++) {
        if (!ecdsa.Verify(msgs[i], sigs[i], pub_keys[i])) {
            seq_valid = false;
        }
    }
    auto end_seq = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_seq = end_seq - start_seq;
    
    std::cout << "Sequential Verification Valid: " << (seq_valid ? "True" : "False") << std::endl;
    std::cout << "Sequential Time (" << BATCH_SIZE << " sigs): " << time_seq.count() << " ms" << std::endl;

    // Benchmark Batch Verification
    auto start_batch = std::chrono::high_resolution_clock::now();
    bool batch_valid = ecdsa.BatchVerify(msgs, sigs, pub_keys);
    auto end_batch = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_batch = end_batch - start_batch;

    std::cout << "Batch Verification Valid: " << (batch_valid ? "True" : "False") << std::endl;
    std::cout << "Batch Time (" << BATCH_SIZE << " sigs): " << time_batch.count() << " ms" << std::endl;

    if (batch_valid) {
        std::cout << "SUCCESS: Batch verification verified correctly!" << std::endl;
    } else {
        std::cout << "FAILED: Batch verification failed." << std::endl;
        return 1;
    }

    // Cleanup
    BN_free(priv_key1);
    EC_POINT_free(pub_key1);
    ecdsa.FreeSignature(sig1);
    for (int i = 0; i < BATCH_SIZE; i++) {
        BN_free(priv_keys[i]);
        EC_POINT_free(pub_keys[i]);
        ecdsa.FreeSignature(sigs[i]);
    }

    return 0;
}
