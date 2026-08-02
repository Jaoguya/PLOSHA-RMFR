#ifndef PLOSHA_CRYPTO_WRAPPER_HPP
#define PLOSHA_CRYPTO_WRAPPER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "../../../src/crypto/paillier.hpp"
#include "../../../src/crypto/modified_ecdsa.hpp"

namespace plosha {

// AES-GCM ciphertext: IV + encrypted data + tag
struct AESCiphertext {
    std::vector<uint8_t> iv;         // 12 bytes
    std::vector<uint8_t> ciphertext; // variable length
    std::vector<uint8_t> tag;        // 16 bytes

    size_t totalBytes() const { return iv.size() + ciphertext.size() + tag.size(); }
};

// Wrapper holding a Paillier ciphertext (BIGNUM) with RAII
struct PaillierCiphertext {
    BIGNUM* value;
    PaillierCiphertext();
    PaillierCiphertext(const PaillierCiphertext& other);
    PaillierCiphertext& operator=(const PaillierCiphertext& other);
    ~PaillierCiphertext();
    size_t byteSize() const;
};

// Centralized crypto operations with built-in timing
class CryptoWrapper {
public:
    CryptoWrapper();
    ~CryptoWrapper();

    // ---- Key Generation ----
    // Generate Paillier keypair (2048-bit)
    void generatePaillierKeys();

    // Generate a random AES-256-GCM key for a fog node
    std::vector<uint8_t> generateAESKey();

    // Generate ECDSA keypair for TEE signing
    void generateECDSAKeys();

    // ---- AES-GCM Operations (sensor-side encryption) ----
    AESCiphertext aesEncrypt(const std::vector<uint8_t>& key,
                              const uint8_t* plaintext,
                              size_t plaintext_len);

    std::vector<uint8_t> aesDecrypt(const std::vector<uint8_t>& key,
                                     const AESCiphertext& ct);

    // ---- Paillier Operations ----
    void precomputeBlindingFactors(int pool_size);
    PaillierCiphertext paillierEncrypt(uint32_t plaintext);
    uint64_t paillierDecrypt(const PaillierCiphertext& ct);
    PaillierCiphertext paillierAggregate(const PaillierCiphertext& c1,
                                         const PaillierCiphertext& c2);

    // R8 FIX: Iterative aggregation of N ciphertexts (associative fold)
    PaillierCiphertext aggregateMultiple(const std::vector<PaillierCiphertext>& ciphertexts);

    // ---- TEE Transform: AES decrypt → Paillier encrypt ----
    // Returns the Paillier ciphertext and records the transformation latency
    PaillierCiphertext teeTransform(const std::vector<uint8_t>& aes_key,
                                     const AESCiphertext& aes_ct);

    // ---- ECDSA Operations ----
    // Sign a hash of the aggregation package
    void ecdsaSign(const std::string& message);
    bool ecdsaVerify(const std::string& message);

    // ---- R2 FIX: β_t calibration ----
    // Run num_trials TEE-transform + aggregate cycles, return average seconds/slot
    double calibrateBetaT(int num_trials = 100);

    // ---- Timing Accessors (last operation, microseconds) ----
    double lastAESEncryptUs()     const { return last_aes_enc_us_; }
    double lastAESDecryptUs()     const { return last_aes_dec_us_; }
    double lastPaillierEncUs()    const { return last_paillier_enc_us_; }
    double lastPaillierDecUs()    const { return last_paillier_dec_us_; }
    double lastPaillierAggUs()    const { return last_paillier_agg_us_; }
    double lastTEETransformUs()   const { return last_tee_transform_us_; }
    double lastECDSASignUs()      const { return last_ecdsa_sign_us_; }

    // Access raw Paillier for advanced operations
    Paillier& paillier() { return paillier_; }

private:
    Paillier paillier_;
    ModifiedECDSA ecdsa_;

    // ECDSA key storage
    BIGNUM* ecdsa_priv_key_;
    EC_POINT* ecdsa_pub_key_;
    Signature* last_signature_;

    // Timing records (microseconds)
    double last_aes_enc_us_        = 0;
    double last_aes_dec_us_        = 0;
    double last_paillier_enc_us_   = 0;
    double last_paillier_dec_us_   = 0;
    double last_paillier_agg_us_   = 0;
    double last_tee_transform_us_  = 0;
    double last_ecdsa_sign_us_     = 0;
};

} // namespace plosha

#endif // PLOSHA_CRYPTO_WRAPPER_HPP
