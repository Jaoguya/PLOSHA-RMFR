#ifndef FTWORKFLOW_CRYPTO_WRAPPER_HPP
#define FTWORKFLOW_CRYPTO_WRAPPER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "../../../src/crypto/paillier.hpp"
#include "../../../src/crypto/modified_ecdsa.hpp"

namespace ftworkflow {

struct AESCiphertext {
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;
    size_t totalBytes() const { return iv.size() + ciphertext.size() + tag.size(); }
};

struct PaillierCiphertext {
    BIGNUM* value;
    PaillierCiphertext();
    PaillierCiphertext(const PaillierCiphertext& other);
    PaillierCiphertext& operator=(const PaillierCiphertext& other);
    ~PaillierCiphertext();
    size_t byteSize() const;
};

class CryptoWrapper {
public:
    CryptoWrapper();
    ~CryptoWrapper();

    void generatePaillierKeys();
    std::vector<uint8_t> generateAESKey();
    void generateECDSAKeys();

    AESCiphertext aesEncrypt(const std::vector<uint8_t>& key,
                              const uint8_t* plaintext, size_t plaintext_len);
    std::vector<uint8_t> aesDecrypt(const std::vector<uint8_t>& key,
                                     const AESCiphertext& ct);

    PaillierCiphertext paillierEncrypt(uint32_t plaintext);
    uint64_t paillierDecrypt(const PaillierCiphertext& ct);
    PaillierCiphertext paillierAggregate(const PaillierCiphertext& c1,
                                         const PaillierCiphertext& c2);
    PaillierCiphertext aggregateMultiple(const std::vector<PaillierCiphertext>& cts);

    PaillierCiphertext teeTransform(const std::vector<uint8_t>& aes_key,
                                     const AESCiphertext& aes_ct);

    void ecdsaSign(const std::string& message);
    bool ecdsaVerify(const std::string& message);

    double calibrateBetaT(int num_trials = 100);

    double lastPaillierEncUs()  const { return last_paillier_enc_us_; }
    double lastTEETransformUs() const { return last_tee_transform_us_; }

    Paillier& paillier() { return paillier_; }

private:
    Paillier paillier_;
    ModifiedECDSA ecdsa_;
    BIGNUM* ecdsa_priv_key_;
    EC_POINT* ecdsa_pub_key_;
    Signature* last_signature_;

    double last_aes_enc_us_        = 0;
    double last_aes_dec_us_        = 0;
    double last_paillier_enc_us_   = 0;
    double last_paillier_dec_us_   = 0;
    double last_paillier_agg_us_   = 0;
    double last_tee_transform_us_  = 0;
    double last_ecdsa_sign_us_     = 0;
};

} // namespace ftworkflow

#endif // FTWORKFLOW_CRYPTO_WRAPPER_HPP
