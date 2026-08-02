#include <iostream>
#include <vector>
#include <cassert>
#include <openssl/bn.h>
#include <openssl/rand.h>

class PaillierSSL {
private:
    BIGNUM *p, *q, *lambda, *mu;
    BN_CTX *ctx;

    // L(x) = (x - 1) / n
    void L(BIGNUM *res, BIGNUM *x, BIGNUM *n) {
        BIGNUM *one = BN_new();
        BN_one(one);
        BN_sub(res, x, one);
        BN_div(res, NULL, res, n, ctx);
        BN_free(one);
    }

public:
    BIGNUM *n, *n_sq, *g;

    PaillierSSL() {
        p = BN_new();
        q = BN_new();
        lambda = BN_new();
        mu = BN_new();
        n = BN_new();
        n_sq = BN_new();
        g = BN_new();
        ctx = BN_CTX_new();
    }

    ~PaillierSSL() {
        BN_free(p); BN_free(q);
        BN_free(lambda); BN_free(mu);
        BN_free(n); BN_free(n_sq); BN_free(g);
        BN_CTX_free(ctx);
    }

    void KeyGen(int bit_length = 512) {
        // Generate primes p and q
        BN_generate_prime_ex(p, bit_length / 2, 1, NULL, NULL, NULL);
        BN_generate_prime_ex(q, bit_length / 2, 1, NULL, NULL, NULL);

        // n = p * q
        BN_mul(n, p, q, ctx);
        
        // n_sq = n * n
        BN_mul(n_sq, n, n, ctx);

        // lambda = lcm(p-1, q-1)
        BIGNUM *p_minus_1 = BN_new();
        BIGNUM *q_minus_1 = BN_new();
        BIGNUM *gcd_pq = BN_new();
        BIGNUM *one = BN_new();
        BN_one(one);

        BN_sub(p_minus_1, p, one);
        BN_sub(q_minus_1, q, one);
        BN_gcd(gcd_pq, p_minus_1, q_minus_1, ctx);
        
        // lambda = (p-1)*(q-1) / gcd
        BN_mul(lambda, p_minus_1, q_minus_1, ctx);
        BN_div(lambda, NULL, lambda, gcd_pq, ctx);

        // g = n + 1
        BN_add(g, n, one);

        // mu = (L(g^lambda mod n^2))^-1 mod n
        BIGNUM *g_lambda = BN_new();
        BIGNUM *l_val = BN_new();
        
        BN_mod_exp(g_lambda, g, lambda, n_sq, ctx);
        L(l_val, g_lambda, n);
        BN_mod_inverse(mu, l_val, n, ctx);

        BN_free(p_minus_1); BN_free(q_minus_1);
        BN_free(gcd_pq); BN_free(one);
        BN_free(g_lambda); BN_free(l_val);
    }

    void Encrypt(BIGNUM *c, BIGNUM *m) {
        BIGNUM *r = BN_new();
        // r in (0, n)
        do {
            BN_rand_range(r, n);
        } while (BN_is_zero(r));

        BIGNUM *g_m = BN_new();
        BIGNUM *r_n = BN_new();
        
        BN_mod_exp(g_m, g, m, n_sq, ctx);
        BN_mod_exp(r_n, r, n, n_sq, ctx);
        
        BN_mod_mul(c, g_m, r_n, n_sq, ctx);

        BN_free(r); BN_free(g_m); BN_free(r_n);
    }

    void Decrypt(BIGNUM *m, BIGNUM *c) {
        BIGNUM *c_lambda = BN_new();
        BIGNUM *l_val = BN_new();

        BN_mod_exp(c_lambda, c, lambda, n_sq, ctx);
        L(l_val, c_lambda, n);
        BN_mod_mul(m, l_val, mu, n, ctx);

        BN_free(c_lambda); BN_free(l_val);
    }

    void Aggregate(BIGNUM *c_sum, BIGNUM *c1, BIGNUM *c2) {
        // Homomorphic addition: c1 * c2 mod n^2
        BN_mod_mul(c_sum, c1, c2, n_sq, ctx);
    }
};

int main() {
    std::cout << "Initializing Paillier Cryptosystem (OpenSSL BIGNUM)..." << std::endl;
    PaillierSSL paillier;
    
    std::cout << "Generating Keys (1024-bit)..." << std::endl;
    paillier.KeyGen(1024);
    
    char* n_hex = BN_bn2hex(paillier.n);
    std::cout << "Public Key n: " << n_hex << std::endl;
    OPENSSL_free(n_hex);

    // Test 1: Simple Encrypt and Decrypt
    BIGNUM *message1 = BN_new();
    BIGNUM *cipher1 = BN_new();
    BIGNUM *decrypted1 = BN_new();
    
    BN_dec2bn(&message1, "123456789");
    
    std::cout << "\n--- Test 1: Encryption & Decryption ---" << std::endl;
    char* msg1_str = BN_bn2dec(message1);
    std::cout << "Original Message 1: " << msg1_str << std::endl;
    OPENSSL_free(msg1_str);
    
    paillier.Encrypt(cipher1, message1);
    char* cipher1_hex = BN_bn2hex(cipher1);
    std::cout << "Ciphertext 1: " << std::string(cipher1_hex).substr(0, 32) << "... (truncated)" << std::endl;
    OPENSSL_free(cipher1_hex);
    
    paillier.Decrypt(decrypted1, cipher1);
    char* dec1_str = BN_bn2dec(decrypted1);
    std::cout << "Decrypted Message 1: " << dec1_str << std::endl;
    
    if (BN_cmp(message1, decrypted1) == 0) {
        std::cout << "SUCCESS: Decrypted message matches original!" << std::endl;
    } else {
        std::cout << "FAILED: Decryption mismatch." << std::endl;
        return 1;
    }
    OPENSSL_free(dec1_str);

    // Test 2: Homomorphic Addition
    BIGNUM *message2 = BN_new();
    BIGNUM *cipher2 = BN_new();
    BIGNUM *cipher_sum = BN_new();
    BIGNUM *decrypted_sum = BN_new();
    BIGNUM *expected_sum = BN_new();
    
    BN_dec2bn(&message2, "987654321");
    BN_add(expected_sum, message1, message2);

    std::cout << "\n--- Test 2: Homomorphic Addition ---" << std::endl;
    char* msg2_str = BN_bn2dec(message2);
    char* exp_str = BN_bn2dec(expected_sum);
    std::cout << "Message 2: " << msg2_str << std::endl;
    std::cout << "Expected Sum: " << exp_str << std::endl;
    OPENSSL_free(msg2_str); OPENSSL_free(exp_str);

    paillier.Encrypt(cipher2, message2);
    paillier.Aggregate(cipher_sum, cipher1, cipher2);
    paillier.Decrypt(decrypted_sum, cipher_sum);
    
    char* dec_sum_str = BN_bn2dec(decrypted_sum);
    std::cout << "Decrypted Sum: " << dec_sum_str << std::endl;
    
    if (BN_cmp(expected_sum, decrypted_sum) == 0) {
        std::cout << "SUCCESS: Homomorphic addition verified!" << std::endl;
    } else {
        std::cout << "FAILED: Homomorphic addition mismatch." << std::endl;
        return 1;
    }
    OPENSSL_free(dec_sum_str);

    BN_free(message1); BN_free(cipher1); BN_free(decrypted1);
    BN_free(message2); BN_free(cipher2); BN_free(cipher_sum);
    BN_free(decrypted_sum); BN_free(expected_sum);

    return 0;
}
