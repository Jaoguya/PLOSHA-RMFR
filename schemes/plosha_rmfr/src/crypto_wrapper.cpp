#include "crypto_wrapper.hpp"
#include "config.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace plosha {

// ---------------------------------------------------------------------------
// PaillierCiphertext RAII
// ---------------------------------------------------------------------------
PaillierCiphertext::PaillierCiphertext() : value(BN_new()) {}

PaillierCiphertext::PaillierCiphertext(const PaillierCiphertext &other)
    : value(BN_new()) {
  if (other.value)
    BN_copy(value, other.value);
}

PaillierCiphertext &
PaillierCiphertext::operator=(const PaillierCiphertext &other) {
  if (this != &other && other.value) {
    BN_copy(value, other.value);
  }
  return *this;
}

PaillierCiphertext::~PaillierCiphertext() {
  if (value)
    BN_free(value);
}

size_t PaillierCiphertext::byteSize() const {
  return value ? BN_num_bytes(value) : 0;
}

// ---------------------------------------------------------------------------
// CryptoWrapper
// ---------------------------------------------------------------------------
CryptoWrapper::CryptoWrapper()
    : ecdsa_priv_key_(BN_new()),
      ecdsa_pub_key_(
          EC_POINT_new(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1))),
      last_signature_(nullptr) {}

CryptoWrapper::~CryptoWrapper() {
  if (ecdsa_priv_key_)
    BN_free(ecdsa_priv_key_);
  if (ecdsa_pub_key_)
    EC_POINT_free(ecdsa_pub_key_);
  if (last_signature_)
    ecdsa_.FreeSignature(last_signature_);
}

// ---------------------------------------------------------------------------
// Key Generation
// ---------------------------------------------------------------------------
void CryptoWrapper::generatePaillierKeys() {
  // R1 FIX: Explicitly use 2048-bit keys as required by the paper
  std::cout << "[CryptoWrapper] Generating Paillier keypair ("
            << PAILLIER_KEY_BITS << "-bit)... this may take a while"
            << std::endl;
  paillier_.KeyGen(PAILLIER_KEY_BITS); // PAILLIER_KEY_BITS = 2048
  std::cout << "[CryptoWrapper] Paillier keypair generated ("
            << PAILLIER_KEY_BITS << "-bit)" << std::endl;

  // Fix A: Pre-compute blinding factors so Encrypt() never calls
  // BN_rand_range() at runtime (avoids Gramine getrandom() bottleneck).
  // Pool size = 10,000 (wraps around for large benchmarks to avoid running out).
  const int POOL_SIZE = 10000;
  std::cout << "[CryptoWrapper] Pre-computing " << POOL_SIZE
            << " Paillier blinding factors..." << std::endl;
  paillier_.PrecomputeBlindingFactors(POOL_SIZE);
  std::cout << "[CryptoWrapper] Blinding factors ready" << std::endl;
}

void CryptoWrapper::precomputeBlindingFactors(int pool_size) {
  paillier_.PrecomputeBlindingFactors(pool_size);
}

std::vector<uint8_t> CryptoWrapper::generateAESKey() {
  std::vector<uint8_t> key(AES_KEY_BYTES);
  if (RAND_bytes(key.data(), AES_KEY_BYTES) != 1) {
    throw std::runtime_error("RAND_bytes failed for AES key generation");
  }
  return key;
}

void CryptoWrapper::generateECDSAKeys() {
  ecdsa_.KeyGen(ecdsa_priv_key_, ecdsa_pub_key_);
}

// ---------------------------------------------------------------------------
// AES-GCM Operations
// ---------------------------------------------------------------------------
AESCiphertext CryptoWrapper::aesEncrypt(const std::vector<uint8_t> &key,
                                        const uint8_t *plaintext,
                                        size_t plaintext_len) {
  auto start = std::chrono::high_resolution_clock::now();

  AESCiphertext ct;
  ct.iv.resize(AES_IV_BYTES);
  RAND_bytes(ct.iv.data(), AES_IV_BYTES);

  ct.ciphertext.resize(plaintext_len);
  ct.tag.resize(AES_TAG_BYTES);

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_BYTES, nullptr);
  EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), ct.iv.data());

  int out_len = 0;
  EVP_EncryptUpdate(ctx, ct.ciphertext.data(), &out_len, plaintext,
                    plaintext_len);
  int final_len = 0;
  EVP_EncryptFinal_ex(ctx, ct.ciphertext.data() + out_len, &final_len);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_BYTES, ct.tag.data());
  EVP_CIPHER_CTX_free(ctx);

  auto end = std::chrono::high_resolution_clock::now();
  last_aes_enc_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return ct;
}

std::vector<uint8_t> CryptoWrapper::aesDecrypt(const std::vector<uint8_t> &key,
                                               const AESCiphertext &ct) {
  auto start = std::chrono::high_resolution_clock::now();

  std::vector<uint8_t> plaintext(ct.ciphertext.size());
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, AES_IV_BYTES, nullptr);
  EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), ct.iv.data());

  int out_len = 0;
  EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, ct.ciphertext.data(),
                    ct.ciphertext.size());
  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_BYTES,
                      const_cast<uint8_t *>(ct.tag.data()));

  int final_len = 0;
  int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + out_len, &final_len);
  EVP_CIPHER_CTX_free(ctx);

  if (ret <= 0) {
    throw std::runtime_error(
        "AES-GCM decryption failed (authentication error)");
  }

  auto end = std::chrono::high_resolution_clock::now();
  last_aes_dec_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return plaintext;
}

// ---------------------------------------------------------------------------
// Paillier Operations
// ---------------------------------------------------------------------------
PaillierCiphertext CryptoWrapper::paillierEncrypt(uint32_t plaintext) {
  auto start = std::chrono::high_resolution_clock::now();

  PaillierCiphertext ct;
  BIGNUM *m = BN_new();
  BN_set_word(m, plaintext);
  paillier_.Encrypt(ct.value, m);
  BN_free(m);

  auto end = std::chrono::high_resolution_clock::now();
  last_paillier_enc_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return ct;
}

uint64_t CryptoWrapper::paillierDecrypt(const PaillierCiphertext &ct) {
  auto start = std::chrono::high_resolution_clock::now();

  BIGNUM *m = BN_new();
  paillier_.Decrypt(m, ct.value);
  uint64_t result = BN_get_word(m);
  BN_free(m);

  auto end = std::chrono::high_resolution_clock::now();
  last_paillier_dec_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return result;
}

PaillierCiphertext
CryptoWrapper::paillierAggregate(const PaillierCiphertext &c1,
                                 const PaillierCiphertext &c2) {
  auto start = std::chrono::high_resolution_clock::now();

  PaillierCiphertext result;
  paillier_.Aggregate(result.value, c1.value, c2.value);

  auto end = std::chrono::high_resolution_clock::now();
  last_paillier_agg_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return result;
}

// ---------------------------------------------------------------------------
// R8 FIX: Iterative aggregation for N ciphertexts
// ---------------------------------------------------------------------------
PaillierCiphertext CryptoWrapper::aggregateMultiple(
    const std::vector<PaillierCiphertext> &ciphertexts) {
  if (ciphertexts.empty()) {
    throw std::runtime_error("Cannot aggregate empty ciphertext vector");
  }

  PaillierCiphertext result = ciphertexts[0];
  for (size_t i = 1; i < ciphertexts.size(); ++i) {
    PaillierCiphertext tmp;
    paillier_.Aggregate(tmp.value, result.value, ciphertexts[i].value);
    BN_copy(result.value, tmp.value);
  }
  return result;
}

// ---------------------------------------------------------------------------
// TEE Transform: AES decrypt → Paillier encrypt (timed end-to-end)
// ---------------------------------------------------------------------------
PaillierCiphertext
CryptoWrapper::teeTransform(const std::vector<uint8_t> &aes_key,
                            const AESCiphertext &aes_ct) {
  auto start = std::chrono::high_resolution_clock::now();

  // Step 1: AES-GCM decrypt inside "TEE boundary"
  std::vector<uint8_t> plaintext = aesDecrypt(aes_key, aes_ct);

  // Step 2: Parse plaintext as uint32_t
  uint32_t value = 0;
  if (plaintext.size() >= sizeof(uint32_t)) {
    std::memcpy(&value, plaintext.data(), sizeof(uint32_t));
  }

  // Step 3: Paillier encrypt
  PaillierCiphertext ct = paillierEncrypt(value);

  auto end = std::chrono::high_resolution_clock::now();
  last_tee_transform_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
  return ct;
}

// ---------------------------------------------------------------------------
// R2 FIX: β_t calibration — measure real per-slot overhead
// ---------------------------------------------------------------------------
double CryptoWrapper::calibrateBetaT(int num_trials) {
  // R9 FIX: the debug override `num_trials = 5;` (left over from an RDRAND
  // entropy-starvation investigation) silently discarded the caller's
  // requested sample size (100, see des_engine.cpp) and calibrated beta_t
  // from only 5 timing samples. Removed: the caller's num_trials is now
  // honored, and the first ~10% of trials are discarded as JIT/cache/
  // allocator warm-up before averaging, which the previous version also
  // never did.
  int warmup = (num_trials > 20) ? num_trials / 10 : 0;

  std::cout << "[CryptoWrapper] Calibrating β_t with " << num_trials
            << " trials (" << warmup << " warm-up discarded)...\n";

  // Generate a test AES key and a sample reading
  auto test_key = generateAESKey();
  uint32_t test_value = 12345;
  uint8_t plaintext_bytes[sizeof(uint32_t)];
  std::memcpy(plaintext_bytes, &test_value, sizeof(uint32_t));

  std::vector<double> trial_times;
  trial_times.reserve(num_trials);

  for (int i = 0; i < num_trials; ++i) {
    // Simulate one micro-slot operation: AES encrypt → TEE transform →
    // aggregate
    auto start = std::chrono::high_resolution_clock::now();

    // Sensor encrypts
    AESCiphertext aes_ct =
        aesEncrypt(test_key, plaintext_bytes, sizeof(uint32_t));
    // TEE transforms (AES decrypt + Paillier encrypt)
    PaillierCiphertext pct = teeTransform(test_key, aes_ct);
    // Aggregate with itself (simulates per-slot overhead)
    PaillierCiphertext agg;
    paillier_.Aggregate(agg.value, pct.value, pct.value);

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_s = std::chrono::duration<double>(end - start).count();
    trial_times.push_back(elapsed_s);
  }

  int counted = num_trials - warmup;
  double avg = std::accumulate(trial_times.begin() + warmup, trial_times.end(),
                               0.0) /
              counted;
  std::cout << "[CryptoWrapper] β_t calibrated = " << (avg * 1000.0)
            << " ms/slot"
            << " (from " << counted << " trials, " << warmup
            << " warm-up discarded)\n";
  return avg;
}

// ---------------------------------------------------------------------------
// ECDSA Operations
// ---------------------------------------------------------------------------
void CryptoWrapper::ecdsaSign(const std::string &message) {
  auto start = std::chrono::high_resolution_clock::now();

  if (last_signature_) {
    ecdsa_.FreeSignature(last_signature_);
  }
  last_signature_ = ecdsa_.CreateSignature();
  ecdsa_.Sign(last_signature_, message, ecdsa_priv_key_);

  auto end = std::chrono::high_resolution_clock::now();
  last_ecdsa_sign_us_ =
      std::chrono::duration<double, std::micro>(end - start).count();
}

bool CryptoWrapper::ecdsaVerify(const std::string &message) {
  if (!last_signature_)
    return false;
  return ecdsa_.Verify(message, last_signature_, ecdsa_pub_key_);
}

} // namespace plosha
