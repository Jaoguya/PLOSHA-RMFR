#ifndef FTWORKFLOW_SENSOR_HPP
#define FTWORKFLOW_SENSOR_HPP

#include "crypto_wrapper.hpp"
#include <cstdint>
#include <vector>

namespace ftworkflow {

class Sensor {
public:
    Sensor(int sensor_id, int fog_node_id, const std::vector<uint8_t>& aes_key);
    AESCiphertext encryptReading(CryptoWrapper& crypto, uint32_t quantized_value);

    int sensorId() const { return sensor_id_; }
    int fogNodeId() const { return fog_node_id_; }
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

private:
    int sensor_id_;
    int fog_node_id_;
    std::vector<uint8_t> aes_key_;
    bool active_ = true;
};

} // namespace ftworkflow

#endif // FTWORKFLOW_SENSOR_HPP
