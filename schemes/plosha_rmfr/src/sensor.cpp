#include "sensor.hpp"
#include <cstring>

namespace plosha {

Sensor::Sensor(int sensor_id, int fog_node_id, const std::vector<uint8_t>& aes_key)
    : sensor_id_(sensor_id), fog_node_id_(fog_node_id), aes_key_(aes_key) {}

AESCiphertext Sensor::encryptReading(CryptoWrapper& crypto, uint32_t quantized_value) {
    uint8_t plaintext[sizeof(uint32_t)];
    std::memcpy(plaintext, &quantized_value, sizeof(uint32_t));
    return crypto.aesEncrypt(aes_key_, plaintext, sizeof(uint32_t));
}

} // namespace plosha
