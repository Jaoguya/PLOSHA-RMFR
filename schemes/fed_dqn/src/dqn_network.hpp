#ifndef DQN_NETWORK_HPP
#define DQN_NETWORK_HPP

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <numeric>

// ============================================================================
// Real Multi-Layer Perceptron (MLP) for Deep Q-Network
// Architecture: Input[state_dim] → Dense[128] → ReLU → Dense[64] → ReLU → Dense[num_actions]
//
// This replaces the tabular Q-learning (std::map) with actual matrix
// multiplications and backpropagation, faithfully implementing the DQN
// architecture described in:
//   Choppara & Mangalampalli, "Adaptive Task Scheduling in Fog Computing
//   Using Federated DQN and K-Means Clustering," IEEE Access, 2025.
// ============================================================================

struct DenseLayer {
    int input_dim;
    int output_dim;
    std::vector<std::vector<double>> weights;  // [output_dim x input_dim]
    std::vector<double> biases;                // [output_dim]

    // Cached for backpropagation
    std::vector<double> last_input;
    std::vector<double> last_pre_activation;   // Before ReLU
    std::vector<double> last_output;           // After activation

    DenseLayer() : input_dim(0), output_dim(0) {}

    DenseLayer(int in_dim, int out_dim, std::mt19937& rng)
        : input_dim(in_dim), output_dim(out_dim) {
        // He initialization: w ~ N(0, sqrt(2/fan_in))
        double std_dev = std::sqrt(2.0 / in_dim);
        std::normal_distribution<double> dist(0.0, std_dev);

        weights.resize(out_dim, std::vector<double>(in_dim));
        biases.resize(out_dim, 0.0);

        for (int o = 0; o < out_dim; ++o) {
            for (int i = 0; i < in_dim; ++i) {
                weights[o][i] = dist(rng);
            }
        }
    }

    // Forward pass: output = W * input + b
    std::vector<double> forward(const std::vector<double>& input, bool apply_relu) {
        assert((int)input.size() == input_dim);
        last_input = input;
        last_pre_activation.resize(output_dim);
        last_output.resize(output_dim);

        for (int o = 0; o < output_dim; ++o) {
            double sum = biases[o];
            for (int i = 0; i < input_dim; ++i) {
                sum += weights[o][i] * input[i];
            }
            last_pre_activation[o] = sum;
            last_output[o] = apply_relu ? std::max(0.0, sum) : sum;
        }
        return last_output;
    }

    // Backward pass: compute gradients and return input gradient
    // d_output: gradient of loss w.r.t. this layer's output
    // Returns: gradient of loss w.r.t. this layer's input
    std::vector<double> backward(const std::vector<double>& d_output,
                                  double learning_rate, bool had_relu) {
        assert((int)d_output.size() == output_dim);

        // Apply ReLU derivative if this layer used ReLU
        std::vector<double> d_pre_act(output_dim);
        for (int o = 0; o < output_dim; ++o) {
            if (had_relu) {
                d_pre_act[o] = (last_pre_activation[o] > 0) ? d_output[o] : 0.0;
            } else {
                d_pre_act[o] = d_output[o];
            }
        }

        // Gradient w.r.t. input (for propagating to previous layer)
        std::vector<double> d_input(input_dim, 0.0);
        for (int i = 0; i < input_dim; ++i) {
            for (int o = 0; o < output_dim; ++o) {
                d_input[i] += weights[o][i] * d_pre_act[o];
            }
        }

        // Update weights and biases (SGD)
        for (int o = 0; o < output_dim; ++o) {
            for (int i = 0; i < input_dim; ++i) {
                weights[o][i] -= learning_rate * d_pre_act[o] * last_input[i];
            }
            biases[o] -= learning_rate * d_pre_act[o];
        }

        return d_input;
    }
};

// ============================================================================
// DQN Network: 3-layer MLP
// ============================================================================

struct DQNNetwork {
    DenseLayer layer1;  // Input → 128
    DenseLayer layer2;  // 128 → 64
    DenseLayer layer3;  // 64 → num_actions

    int state_dim;
    int num_actions;

    DQNNetwork() : state_dim(0), num_actions(0) {}

    void init(int state_dim_, int num_actions_, std::mt19937& rng) {
        state_dim = state_dim_;
        num_actions = num_actions_;

        layer1 = DenseLayer(state_dim, 128, rng);
        layer2 = DenseLayer(128, 64, rng);
        layer3 = DenseLayer(64, num_actions, rng);
    }

    // Forward pass: state → Q-values
    std::vector<double> forward(const std::vector<double>& state) {
        auto h1 = layer1.forward(state, true);   // ReLU
        auto h2 = layer2.forward(h1, true);      // ReLU
        auto out = layer3.forward(h2, false);     // Linear output (Q-values)
        return out;
    }

    // Get best action (argmax of Q-values)
    int getBestAction(const std::vector<double>& state) {
        auto q_values = forward(state);
        return static_cast<int>(
            std::max_element(q_values.begin(), q_values.end()) - q_values.begin());
    }

    // Train on a single (state, action, target) tuple using backpropagation
    // Loss = 0.5 * (Q(s,a) - target)^2
    void train(const std::vector<double>& state, int action, double target,
               double learning_rate) {
        // Forward pass (populates cached activations)
        auto q_values = forward(state);

        // Compute gradient of MSE loss w.r.t. output layer
        // dL/dQ = (Q(s,a) - target) for the chosen action, 0 for others
        std::vector<double> d_output(num_actions, 0.0);
        d_output[action] = q_values[action] - target;

        // Backpropagate through all layers
        auto d3 = layer3.backward(d_output, learning_rate, false);
        auto d2 = layer2.backward(d3, learning_rate, true);
        layer1.backward(d2, learning_rate, true);
    }

    // ========================================================================
    // Weight access for Federated Averaging
    // ========================================================================

    // Flatten all weights into a single vector
    std::vector<double> getWeights() const {
        std::vector<double> all_weights;
        auto addLayer = [&](const DenseLayer& layer) {
            for (const auto& row : layer.weights) {
                all_weights.insert(all_weights.end(), row.begin(), row.end());
            }
            all_weights.insert(all_weights.end(),
                               layer.biases.begin(), layer.biases.end());
        };
        addLayer(layer1);
        addLayer(layer2);
        addLayer(layer3);
        return all_weights;
    }

    // Set all weights from a single flattened vector
    void setWeights(const std::vector<double>& all_weights) {
        int idx = 0;
        auto setLayer = [&](DenseLayer& layer) {
            for (auto& row : layer.weights) {
                for (auto& w : row) {
                    w = all_weights[idx++];
                }
            }
            for (auto& b : layer.biases) {
                b = all_weights[idx++];
            }
        };
        setLayer(layer1);
        setLayer(layer2);
        setLayer(layer3);
    }

    // Total number of trainable parameters
    int numParameters() const {
        auto countLayer = [](const DenseLayer& l) {
            return l.input_dim * l.output_dim + l.output_dim;
        };
        return countLayer(layer1) + countLayer(layer2) + countLayer(layer3);
    }
};

#endif // DQN_NETWORK_HPP
