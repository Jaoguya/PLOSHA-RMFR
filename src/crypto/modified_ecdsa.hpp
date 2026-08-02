#ifndef MODIFIED_ECDSA_HPP
#define MODIFIED_ECDSA_HPP

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <vector>
#include <string>

struct Signature {
    EC_POINT* R;
    BIGNUM* s;
    BIGNUM* r; // x-coordinate of R
};

class ModifiedECDSA {
private:
    EC_GROUP* group;
    BIGNUM* order;
    BN_CTX* ctx;
    
    // Computes SHA-256 hash of a message and reduces it modulo the curve order
    void HashToBignum(BIGNUM* hash_bn, const std::string& msg);

public:
    ModifiedECDSA(int curve_nid = NID_X9_62_prime256v1);
    ~ModifiedECDSA();

    // Generates a private key (d) and public key (Q) pair
    void KeyGen(BIGNUM* priv_key, EC_POINT* pub_key);

    // Generates a single modified ECDSA signature
    void Sign(Signature* sig, const std::string& msg, BIGNUM* priv_key);

    // Verifies a single signature
    bool Verify(const std::string& msg, const Signature* sig, EC_POINT* pub_key);

    // Batch verification of multiple signatures simultaneously
    bool BatchVerify(const std::vector<std::string>& msgs, 
                     const std::vector<Signature*>& sigs, 
                     const std::vector<EC_POINT*>& pub_keys);

    // Helper to allocate/free Signature struct members
    Signature* CreateSignature();
    void FreeSignature(Signature* sig);
};

#endif // MODIFIED_ECDSA_HPP
