#ifndef PLOSHA_SENSOR_HPP
#define PLOSHA_SENSOR_HPP

#include "crypto_wrapper.hpp"
#include <cstdint>
#include <vector>

namespace plosha {

class Sensor {
public:
    Sensor(int sensor_id, int fog_node_id, const std::vector<uint8_t>& aes_key);

    // Encrypt a quantized sensor reading using AES-GCM
    AESCiphertext encryptReading(CryptoWrapper& crypto, uint32_t quantized_value);

    int sensorId() const { return sensor_id_; }
    int fogNodeId() const { return fog_node_id_; }
    // Re-associate this sensor with a different fog node. Used only by the
    // heterogeneity experiment, where the capacity-aware scheduling decision is
    // actually applied to routing (the paper defines scheduling latency as
    // running "until a fog node is selected", which implies the selection routes
    // traffic). Other experiments leave the static assignment untouched.
    void setFogNodeId(int fog_node_id) { fog_node_id_ = fog_node_id; }
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    const std::vector<uint8_t>& aesKey() const { return aes_key_; }

private:
    int sensor_id_;
    int fog_node_id_;
    std::vector<uint8_t> aes_key_;  // Fog-scoped AES-256 key k_i
    bool active_ = true;
};

} // namespace plosha

#endif // PLOSHA_SENSOR_HPP
