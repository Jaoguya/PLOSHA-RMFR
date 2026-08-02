#include "modified_ecdsa.hpp"
#include <iostream>
#include <openssl/sha.h>

ModifiedECDSA::ModifiedECDSA(int curve_nid) {
  group = EC_GROUP_new_by_curve_name(curve_nid);
  order = BN_new();
  ctx = BN_CTX_new();
  EC_GROUP_get_order(group, order, ctx);
}

ModifiedECDSA::~ModifiedECDSA() {
  EC_GROUP_free(group);
  BN_free(order);
  BN_CTX_free(ctx);
}

void ModifiedECDSA::HashToBignum(BIGNUM *hash_bn, const std::string &msg) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(msg.c_str()), msg.length(),
         hash);
  BN_bin2bn(hash, SHA256_DIGEST_LENGTH, hash_bn);
  BN_nnmod(hash_bn, hash_bn, order, ctx);
}

Signature *ModifiedECDSA::CreateSignature() {
  Signature *sig = new Signature();
  sig->R = EC_POINT_new(group);
  sig->s = BN_new();
  sig->r = BN_new();
  return sig;
}

void ModifiedECDSA::FreeSignature(Signature *sig) {
  if (sig) {
    if (sig->R)
      EC_POINT_free(sig->R);
    if (sig->s)
      BN_free(sig->s);
    if (sig->r)
      BN_free(sig->r);
    delete sig;
  }
}

void ModifiedECDSA::KeyGen(BIGNUM *priv_key, EC_POINT *pub_key) {
  // d in [1, order-1]
  BN_rand_range(priv_key, order);

  // Q = dG
  EC_POINT_mul(group, pub_key, priv_key, NULL, NULL, ctx);
}

void ModifiedECDSA::Sign(Signature *sig, const std::string &msg,
                         BIGNUM *priv_key) {
  BIGNUM *k = BN_new();
  BIGNUM *e = BN_new();
  BIGNUM *rd = BN_new();
  BIGNUM *ed = BN_new();

  // 1. Choose random k
  BN_rand_range(k, order);

  // 2. R = kG
  EC_POINT_mul(group, sig->R, k, NULL, NULL, ctx);

  // 3. r = x-coordinate of R
  EC_POINT_get_affine_coordinates_GFp(group, sig->R, sig->r, NULL, ctx);
  BN_nnmod(sig->r, sig->r, order, ctx); // r = x mod q

  // 4. e = Hash(m)
  HashToBignum(e, msg);

  // 5. s = k - r*d - e*d mod q
  BN_mod_mul(rd, sig->r, priv_key, order, ctx); // r*d
  BN_mod_mul(ed, e, priv_key, order, ctx);      // e*d

  BN_mod_sub(sig->s, k, rd, order, ctx);      // k - r*d
  BN_mod_sub(sig->s, sig->s, ed, order, ctx); // (k - r*d) - e*d

  BN_free(k);
  BN_free(e);
  BN_free(rd);
  BN_free(ed);
}

bool ModifiedECDSA::Verify(const std::string &msg, const Signature *sig,
                           EC_POINT *pub_key) {
  BIGNUM *e = BN_new();
  BIGNUM *r_plus_e = BN_new();
  EC_POINT *V = EC_POINT_new(group);

  // 1. e = Hash(m)
  HashToBignum(e, msg);

  // 2. r + e mod q
  BN_mod_add(r_plus_e, sig->r, e, order, ctx);

  // 3. V = sG + (r+e)Q
  EC_POINT_mul(group, V, sig->s, pub_key, r_plus_e, ctx);

  // 4. Check V == R
  int cmp = EC_POINT_cmp(group, V, sig->R, ctx);

  BN_free(e);
  BN_free(r_plus_e);
  EC_POINT_free(V);

  return (cmp == 0);
}

bool ModifiedECDSA::BatchVerify(const std::vector<std::string> &msgs,
                                const std::vector<Signature *> &sigs,
                                const std::vector<EC_POINT *> &pub_keys) {
  if (msgs.size() != sigs.size() || sigs.size() != pub_keys.size()) {
    return false;
  }

  size_t n = sigs.size();

  BIGNUM *sum_s = BN_new();
  BN_zero(sum_s);

  BIGNUM *e = BN_new();
  BIGNUM *r_plus_e = BN_new();
  EC_POINT *sum_R = EC_POINT_new(group);
  EC_POINT_set_to_infinity(group, sum_R);

  std::vector<const EC_POINT *> points;
  std::vector<const BIGNUM *> scalars;

  for (size_t i = 0; i < n; i++) {
    // sum_s += s_i
    BN_mod_add(sum_s, sum_s, sigs[i]->s, order, ctx);

    // e_i = Hash(m_i)
    HashToBignum(e, msgs[i]);

    // scalar_i = r_i + e_i mod q
    BIGNUM *scalar_i = BN_new();
    BN_mod_add(scalar_i, sigs[i]->r, e, order, ctx);

    points.push_back(pub_keys[i]);
    scalars.push_back(scalar_i);

    // sum_R += R_i
    EC_POINT_add(group, sum_R, sum_R, sigs[i]->R, ctx);
  }

  // Compute LHS = sum_s * G + Sum(scalar_i * Q_i)
  EC_POINT *LHS = EC_POINT_new(group);

  // OpenSSL's EC_POINTs_mul handles the multiple scalar multiplication
  // efficiently LHS = sum_s * G + scalars[0]*points[0] + ... +
  // scalars[n-1]*points[n-1]
  EC_POINTs_mul(group, LHS, sum_s, n, points.data(), scalars.data(), ctx);

  // Compare LHS with sum_R
  int cmp = EC_POINT_cmp(group, LHS, sum_R, ctx);

  // Cleanup
  BN_free(sum_s);
  BN_free(e);
  BN_free(r_plus_e);
  EC_POINT_free(sum_R);
  EC_POINT_free(LHS);
  for (size_t i = 0; i < n; i++) {
    BN_free((BIGNUM *)scalars[i]);
  }

  return (cmp == 0);
}
