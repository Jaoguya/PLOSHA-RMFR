#include <iostream>
#include <gmpxx.h>
#include <vector>
#include <cassert>

class Paillier {
private:
    mpz_class p, q, lambda, mu;
    gmp_randclass rand_gen;

    mpz_class L(const mpz_class& x, const mpz_class& n) {
        return (x - 1) / n;
    }

    mpz_class modInverse(const mpz_class& a, const mpz_class& m) {
        mpz_class res;
        mpz_invert(res.get_mpz_t(), a.get_mpz_t(), m.get_mpz_t());
        return res;
    }

    mpz_class powerMod(const mpz_class& base, const mpz_class& exp, const mpz_class& mod) {
        mpz_class res;
        mpz_powm(res.get_mpz_t(), base.get_mpz_t(), exp.get_mpz_t(), mod.get_mpz_t());
        return res;
    }

public:
    mpz_class n, n_sq, g;

    Paillier() : rand_gen(gmp_randinit_default) {
        // Initialize random seed based on time
        rand_gen.seed(time(NULL));
    }

    void KeyGen(int bit_length = 512) {
        // Generate primes p and q
        mpz_class max_val = 1;
        max_val <<= (bit_length / 2);
        
        do {
            p = rand_gen.get_z_range(max_val);
            mpz_nextprime(p.get_mpz_t(), p.get_mpz_t());
        } while (mpz_sizeinbase(p.get_mpz_t(), 2) < bit_length / 2);

        do {
            q = rand_gen.get_z_range(max_val);
            mpz_nextprime(q.get_mpz_t(), q.get_mpz_t());
        } while (p == q || mpz_sizeinbase(q.get_mpz_t(), 2) < bit_length / 2);

        n = p * q;
        n_sq = n * n;
        
        // lambda = lcm(p-1, q-1)
        mpz_class p_minus_1 = p - 1;
        mpz_class q_minus_1 = q - 1;
        mpz_class gcd_pq;
        mpz_gcd(gcd_pq.get_mpz_t(), p_minus_1.get_mpz_t(), q_minus_1.get_mpz_t());
        lambda = (p_minus_1 * q_minus_1) / gcd_pq;

        // g = n + 1 (standard choice)
        g = n + 1;

        // mu = (L(g^lambda mod n^2))^-1 mod n
        mpz_class g_lambda = powerMod(g, lambda, n_sq);
        mpz_class l_val = L(g_lambda, n);
        mu = modInverse(l_val, n);
    }

    mpz_class Encrypt(const mpz_class& m) {
        mpz_class r;
        do {
            r = rand_gen.get_z_range(n);
        } while (r == 0); // r in (0, n)

        mpz_class g_m = powerMod(g, m, n_sq);
        mpz_class r_n = powerMod(r, n, n_sq);
        return (g_m * r_n) % n_sq;
    }

    mpz_class Decrypt(const mpz_class& c) {
        mpz_class c_lambda = powerMod(c, lambda, n_sq);
        mpz_class l_val = L(c_lambda, n);
        return (l_val * mu) % n;
    }

    mpz_class Aggregate(const mpz_class& c1, const mpz_class& c2) {
        // Homomorphic addition: c1 * c2 mod n^2
        return (c1 * c2) % n_sq;
    }
};

int main() {
    std::cout << "Initializing Paillier Cryptosystem..." << std::endl;
    Paillier paillier;
    
    std::cout << "Generating Keys (1024-bit)..." << std::endl;
    paillier.KeyGen(1024);
    std::cout << "Public Key n: " << paillier.n.get_str(16) << std::endl;

    // Test 1: Simple Encrypt and Decrypt
    mpz_class message1 = 123456789;
    std::cout << "\n--- Test 1: Encryption & Decryption ---" << std::endl;
    std::cout << "Original Message 1: " << message1 << std::endl;
    
    mpz_class cipher1 = paillier.Encrypt(message1);
    std::cout << "Ciphertext 1: " << cipher1.get_str(16).substr(0, 32) << "... (truncated)" << std::endl;
    
    mpz_class decrypted1 = paillier.Decrypt(cipher1);
    std::cout << "Decrypted Message 1: " << decrypted1 << std::endl;
    
    if (message1 == decrypted1) {
        std::cout << "SUCCESS: Decrypted message matches original!" << std::endl;
    } else {
        std::cout << "FAILED: Decryption mismatch." << std::endl;
        return 1;
    }

    // Test 2: Homomorphic Addition
    mpz_class message2 = 987654321;
    std::cout << "\n--- Test 2: Homomorphic Addition ---" << std::endl;
    std::cout << "Message 1: " << message1 << std::endl;
    std::cout << "Message 2: " << message2 << std::endl;
    std::cout << "Expected Sum: " << message1 + message2 << std::endl;

    mpz_class cipher2 = paillier.Encrypt(message2);
    mpz_class cipher_sum = paillier.Aggregate(cipher1, cipher2);
    mpz_class decrypted_sum = paillier.Decrypt(cipher_sum);
    
    std::cout << "Decrypted Sum: " << decrypted_sum << std::endl;
    
    if (decrypted_sum == (message1 + message2)) {
        std::cout << "SUCCESS: Homomorphic addition verified!" << std::endl;
    } else {
        std::cout << "FAILED: Homomorphic addition mismatch." << std::endl;
        return 1;
    }

    return 0;
}
