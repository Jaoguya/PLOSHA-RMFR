#include "crypto_wrapper.hpp"
#include "config.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace ftworkflow {

// PaillierCiphertext RAII
PaillierCiphertext::PaillierCiphertext() : value(BN_new()) {}
PaillierCiphertext::PaillierCiphertext(const PaillierCiphertext& other) : value(BN_new()) {
    if (other.value) BN_copy(value, other.value);
}
PaillierCiphertext& PaillierCiphertext::operator=(const PaillierCiphertext& other) {
    if (this != &other && other.value) BN_copy(value, other.value);
    return *this;
}
PaillierCiphertext::~PaillierCiphertext() { if (value) BN_free(value); }
size_t PaillierCiphertext::byteSize() const { return value ? BN_num_bytes(value) : 0; }

// CryptoWrapper
CryptoWrapper::CryptoWrapper()
    : ecdsa_priv_key_(BN_new()),
      ecdsa_pub_key_(EC_POINT_new(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1))),
      last_signature_(nullptr) {}

CryptoWrapper::~CryptoWrapper() {
    if (ecdsa_priv_key_) BN_free(ecdsa_priv_key_);
    if (ecdsa_pub_key_) EC_POINT_free(ecdsa_pub_key_);
    if (last_signature_) ecdsa_.FreeSignature(last_signature_);
}

void CryptoWrapper::generatePaillierKeys() {
    std::cout << "[FTWorkflow-Crypto] Generating Paillier keypair ("
              << PAILLIER_KEY_BITS << "-bit)...\n";
    paillier_.KeyGen(PAILLIER_KEY_BITS);
    std::cout << "[FTWorkflow-Crypto] Paillier keypair generated\n";

    const int POOL_SIZE = 10000;
    std::cout << "[FTWorkflow-Crypto] Pre-computing " << POOL_SIZE
              << " blinding factors...\n";
    paillier_.PrecomputeBlindingFactors(POOL_SIZE);
    std::cout << "[FTWorkflow-Crypto] Blinding factors ready\n";
}

std::vector<uint8_t> CryptoWrapper::generateAESKey() {
    std::vector<uint8_t> key(AES_KEY_BYTES);
    if (RAND_bytes(key.data(), AES_KEY_BYTES) != 1)
        throw std::runtime_error("RAND_bytes failed");
    return key;
}

void CryptoWrapper::generateECDSAKeys() {
    ecdsa_.KeyGen(ecdsa_priv_key_, ecdsa_pub_key_);
}

AESCiphertext CryptoWrapper::aesEncrypt(const std::vector<uint8_t>& key,
                                         const uint8_t* plaintext, size_t plaintext_len) {
    auto start = std::chrono::high_resolution_clock::now();
    AESCiphertext ct;
    ct.iv.resize(AES_IV_BYTES);
    RAND_bytes(ct.iv.data(), AES_IV_BYTES);
    ct.ciphertext.resize(plaintext_len);
    ct.tag.resize(AES_TAG_BYTES);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_BYTES, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), ct.iv.data());
    int out_len = 0;
    EVP_EncryptUpdate(ctx, ct.ciphertext.data(), &out_len, plaintext, plaintext_len);
    int final_len = 0;
    EVP_EncryptFinal_ex(ctx, ct.ciphertext.data() + out_len, &final_len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_BYTES, ct.tag.data());
    EVP_CIPHER_CTX_free(ctx);

    auto end = std::chrono::high_resolution_clock::now();
    last_aes_enc_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return ct;
}

std::vector<uint8_t> CryptoWrapper::aesDecrypt(const std::vector<uint8_t>& key,
                                                const AESCiphertext& ct) {
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> plaintext(ct.ciphertext.size());
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_BYTES, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), ct.iv.data());
    int out_len = 0;
    EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, ct.ciphertext.data(), ct.ciphertext.size());
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_BYTES,
                        const_cast<uint8_t*>(ct.tag.data()));
    int final_len = 0;
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + out_len, &final_len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) throw std::runtime_error("AES-GCM decryption failed");

    auto end = std::chrono::high_resolution_clock::now();
    last_aes_dec_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return plaintext;
}

PaillierCiphertext CryptoWrapper::paillierEncrypt(uint32_t plaintext) {
    auto start = std::chrono::high_resolution_clock::now();
    PaillierCiphertext ct;
    BIGNUM* m = BN_new();
    BN_set_word(m, plaintext);
    paillier_.Encrypt(ct.value, m);
    BN_free(m);
    auto end = std::chrono::high_resolution_clock::now();
    last_paillier_enc_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return ct;
}

uint64_t CryptoWrapper::paillierDecrypt(const PaillierCiphertext& ct) {
    auto start = std::chrono::high_resolution_clock::now();
    BIGNUM* m = BN_new();
    paillier_.Decrypt(m, ct.value);
    uint64_t result = BN_get_word(m);
    BN_free(m);
    auto end = std::chrono::high_resolution_clock::now();
    last_paillier_dec_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return result;
}

PaillierCiphertext CryptoWrapper::paillierAggregate(const PaillierCiphertext& c1,
                                                     const PaillierCiphertext& c2) {
    auto start = std::chrono::high_resolution_clock::now();
    PaillierCiphertext result;
    paillier_.Aggregate(result.value, c1.value, c2.value);
    auto end = std::chrono::high_resolution_clock::now();
    last_paillier_agg_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return result;
}

PaillierCiphertext CryptoWrapper::aggregateMultiple(
    const std::vector<PaillierCiphertext>& cts) {
    if (cts.empty()) throw std::runtime_error("Cannot aggregate empty vector");
    PaillierCiphertext result = cts[0];
    for (size_t i = 1; i < cts.size(); ++i) {
        PaillierCiphertext tmp;
        paillier_.Aggregate(tmp.value, result.value, cts[i].value);
        BN_copy(result.value, tmp.value);
    }
    return result;
}

PaillierCiphertext CryptoWrapper::teeTransform(const std::vector<uint8_t>& aes_key,
                                                const AESCiphertext& aes_ct) {
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> plaintext = aesDecrypt(aes_key, aes_ct);
    uint32_t value = 0;
    if (plaintext.size() >= sizeof(uint32_t))
        std::memcpy(&value, plaintext.data(), sizeof(uint32_t));
    PaillierCiphertext ct = paillierEncrypt(value);
    auto end = std::chrono::high_resolution_clock::now();
    last_tee_transform_us_ = std::chrono::duration<double, std::micro>(end - start).count();
    return ct;
}

double CryptoWrapper::calibrateBetaT(int num_trials) {
    std::cout << "[FTWorkflow-Crypto] Calibrating β_t with " << num_trials << " AES trials...\n";
    auto test_key = generateAESKey();
    uint32_t test_value = 12345;
    uint8_t plaintext_bytes[sizeof(uint32_t)];
    std::memcpy(plaintext_bytes, &test_value, sizeof(uint32_t));

    std::vector<double> trial_times;
    trial_times.reserve(num_trials);
    for (int i = 0; i < num_trials; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        // AES-only: encrypt, decrypt, re-encrypt (simulates aggregation pipeline)
        AESCiphertext aes_ct = aesEncrypt(test_key, plaintext_bytes, sizeof(uint32_t));
        auto pt = aesDecrypt(test_key, aes_ct);
        AESCiphertext aes_ct2 = aesEncrypt(test_key, pt.data(), pt.size());
        auto end = std::chrono::high_resolution_clock::now();
        trial_times.push_back(std::chrono::duration<double>(end - start).count());
    }

    double avg = std::accumulate(trial_times.begin(), trial_times.end(), 0.0) / num_trials;
    std::cout << "[FTWorkflow-Crypto] β_t calibrated = " << (avg * 1000.0)
              << " ms/slot (from " << num_trials << " trials)\n";
    return avg;
}

void CryptoWrapper::ecdsaSign(const std::string& message) {
    auto start = std::chrono::high_resolution_clock::now();
    if (last_signature_) ecdsa_.FreeSignature(last_signature_);
    last_signature_ = ecdsa_.CreateSignature();
    ecdsa_.Sign(last_signature_, message, ecdsa_priv_key_);
    auto end = std::chrono::high_resolution_clock::now();
    last_ecdsa_sign_us_ = std::chrono::duration<double, std::micro>(end - start).count();
}

bool CryptoWrapper::ecdsaVerify(const std::string& message) {
    if (!last_signature_) return false;
    return ecdsa_.Verify(message, last_signature_, ecdsa_pub_key_);
}

} // namespace ftworkflow
