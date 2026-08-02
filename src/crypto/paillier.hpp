#ifndef PAILLIER_HPP
#define PAILLIER_HPP

#include <openssl/bn.h>
#include <openssl/rand.h>
#include <vector>
#include <cstddef>

class Paillier {
private:
    BIGNUM *p, *q, *lambda, *mu;
    BN_CTX *ctx;

    // Fix A: Pre-computed blinding factors (r^n mod n^2) to avoid
    // repeated getrandom() syscalls that cause entropy starvation in Gramine SGX.
    std::vector<BIGNUM*> blinding_pool_;
    size_t pool_index_ = 0;

    void L(BIGNUM *res, BIGNUM *x, BIGNUM *n);

public:
    BIGNUM *n, *n_sq, *g;

    Paillier();
    ~Paillier();

    void KeyGen(int bit_length = 1024);
    void PrecomputeBlindingFactors(int pool_size);
    void Encrypt(BIGNUM *c, BIGNUM *m);
    void Decrypt(BIGNUM *m, BIGNUM *c);
    void Aggregate(BIGNUM *c_sum, BIGNUM *c1, BIGNUM *c2);
};

#endif // PAILLIER_HPP
