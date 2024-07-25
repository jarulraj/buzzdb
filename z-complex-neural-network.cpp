#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <numeric>
#include <algorithm>

class SimpleNeuralNetwork {
private:
    std::vector<int> layer_sizes;
    double learning_rate;
    double decay_rate;
    std::vector<std::vector<double>> weights;
    std::vector<std::vector<double>> biases;
    double input_mean = 0.0;
    double input_std = 1.0;
    double target_mean = 0.0;
    double target_std = 1.0;

    double relu(double x) const {
        return std::max(0.0, x);
    }

    double sigmoid(double x) const {
        return 1.0 / (1.0 + exp(-x));
    }

    double normalize(double x, double mean, double std) const {
        return (x - mean) / std;
    }

    void initializeWeights() {
        std::random_device rd;
        std::mt19937 gen(rd());
        weights.clear();
        biases.clear();
        for (size_t i = 0; i < layer_sizes.size() - 1; ++i) {
            std::normal_distribution<> dis(0, std::sqrt(2.0 / layer_sizes[i]));

            // Initialize weights for layer i
            weights.push_back(std::vector<double>(layer_sizes[i] * layer_sizes[i + 1]));
            for (auto &weight : weights.back()) {
                weight = dis(gen);
            }

            // Initialize biases for layer i
            biases.push_back(std::vector<double>(layer_sizes[i + 1], 0.0));
        }
    }

public:
    SimpleNeuralNetwork(const std::vector<int>& sizes, double init_lr = 0.01, double lr_decay = 0.001)
        : layer_sizes(sizes), learning_rate(init_lr), decay_rate(lr_decay), weights(sizes.size()-1), biases(sizes.size()-1) {
        initializeWeights();
    }

    void train(const std::vector<int>& inputs, const std::vector<double>& targets, int epochs, int batch_size) {
        // Precompute normalization parameters
        input_mean = std::accumulate(inputs.begin(), inputs.end(), 0.0) / inputs.size();
        input_std = std::sqrt(std::accumulate(inputs.begin(), inputs.end(), 0.0, 
            [this](double acc, int x) { return acc + (x - input_mean) * (x - input_mean); }) / inputs.size());
        target_mean = std::accumulate(targets.begin(), targets.end(), 0.0) / targets.size();
        target_std = std::sqrt(std::accumulate(targets.begin(), targets.end(), 0.0, 
            [this](double acc, double x) { return acc + (x - target_mean) * (x - target_mean); }) / targets.size());

        std::vector<double> errors;

        for (int epoch = 0; epoch < epochs; ++epoch) {
            std::vector<int> indices(inputs.size());
            std::iota(indices.begin(), indices.end(), 0);
            std::shuffle(indices.begin(), indices.end(), std::mt19937(std::random_device()()));

            for (size_t start_index = 0; start_index < inputs.size(); start_index += batch_size) {
                size_t end_index = std::min(inputs.size(), start_index + batch_size);
                std::vector<std::vector<double>> weight_gradients(weights.size());
                std::vector<std::vector<double>> bias_gradients(biases.size());

                for (size_t l = 0; l < weights.size(); ++l) {
                    weight_gradients[l].resize(weights[l].size(), 0.0);
                    bias_gradients[l].resize(biases[l].size(), 0.0);
                }

                double batch_error = 0.0;

                for (size_t i = start_index; i < end_index; ++i) {
                    int idx = indices[i];
                    double normalized_input = normalize(inputs[idx], input_mean, input_std);
                    double normalized_target = normalize(targets[idx], target_mean, target_std);

                    // Forward pass
                    std::vector<double> activations{normalized_input};
                    std::vector<std::vector<double>> activations_per_layer;

                    for (size_t l = 0; l < weights.size(); ++l) {
                        std::vector<double> next_activations(layer_sizes[l + 1], biases[l][0]); // Start with bias values
                        for (int j = 0; j < layer_sizes[l + 1]; ++j) {
                            for (int k = 0; k < layer_sizes[l]; ++k) {
                                next_activations[j] += activations[k] * weights[l][k * layer_sizes[l + 1] + j];
                            }
                            next_activations[j] = l == weights.size() - 1 ? sigmoid(next_activations[j]) : relu(next_activations[j]);
                        }
                        activations_per_layer.push_back(activations); // Save activations for backpropagation
                        activations = next_activations; // Move to the next layer
                    }

                    double prediction = activations.back(); // Last layer's output is the network's prediction
                    double error = normalized_target - prediction;
                    double delta_output = error * prediction * (1 - prediction);
                    batch_error += error * error;

                    std::vector<double> deltas{delta_output};

                    // Backpropagation
                    for (int l = weights.size() - 1; l >= 0; --l) {
                        std::vector<double> new_deltas(layer_sizes[l], 0.0);
                        for (int j = 0; j < layer_sizes[l+1]; ++j) {
                            bias_gradients[l][j] += deltas[j];
                            for (int k = 0; k < layer_sizes[l]; ++k) {
                                weight_gradients[l][k * layer_sizes[l+1] + j] += deltas[j] * activations_per_layer[l][k];
                                new_deltas[k] += weights[l][k * layer_sizes[l+1] + j] * deltas[j];
                            }
                        }
                        deltas = new_deltas;
                    }
                }

                // Apply accumulated gradients
                for (size_t l = 0; l < weights.size(); ++l) {
                    for (size_t j = 0; j < weights[l].size(); ++j) {
                        weights[l][j] -= learning_rate * weight_gradients[l][j] / (end_index - start_index);
                    }
                    for (size_t j = 0; j < biases[l].size(); ++j) {
                        biases[l][j] -= learning_rate * bias_gradients[l][j] / (end_index - start_index);
                    }
                }

                errors.push_back(batch_error / (end_index - start_index));
            }

            learning_rate *= 1 / (1 + decay_rate * epoch);
            if(epoch % 100 == 0){
                std::cout << "Epoch " << epoch + 1 << " complete. Avg Error: " << errors.back() << std::endl;
            }
        }
    }


    double predict(int x) const {
        double normalized_input = normalize(x, input_mean, input_std);
        std::vector<double> activations{normalized_input};

        for (size_t l = 0; l < weights.size(); ++l) {
            std::vector<double> next_activations = biases[l];  // Corrected initialization
            for (int j = 0; j < layer_sizes[l+1]; ++j) {
                for (int k = 0; k < layer_sizes[l]; ++k) {
                    next_activations[j] += activations[k] * weights[l][k * layer_sizes[l+1] + j];
                }
                next_activations[j] = l == weights.size() - 1 ? sigmoid(next_activations[j]) : relu(next_activations[j]);
            }
            activations = next_activations;
        }

        double raw_output = sigmoid(activations.back());
        return raw_output * target_std + target_mean;
    }

    void print() const {
        std::cout << "Neural Network Model:\n";
        for (size_t l = 0; l < weights.size(); ++l) {
            std::cout << "Layer " << l << " Weights and Biases:\n";
            for (const auto& weight : weights[l]) {
                std::cout << weight << " ";
            }
            std::cout << "\nBiases: ";
            for (const auto& bias : biases[l]) {
                std::cout << bias << " ";
            }
            std::cout << std::endl;
        }
    }
};

int main() {
    // Increase the range and number of data points
    int num_data = 1000; // Increase the number of data points for better training coverage
    int max_input = 10000; // Increase the range of input values
    int max_target = 1000; // Increase the range of target values

    std::vector<int> inputs(num_data);
    std::vector<double> targets(num_data);

    // Generate linearly spaced values across the new ranges
    for (int i = 0; i < num_data; ++i) {
        inputs[i] = i * (max_input / num_data);
        targets[i] = (i * (max_target / num_data)); // Use modulo to create non-linear targets
    }

    // Initialize the neural network with more neurons in each layer to handle more complex patterns
    SimpleNeuralNetwork nn({1, 10, 10, 1}, 0.001, 0.0001);

    // Train the network
    nn.train(inputs, targets, 1000, 10); // Adjust epochs and batch size as needed

    // Print the network parameters
    //nn.print();

    // Predict and display results
    for (int i = 0; i < num_data; i += 100) { // Sample predictions to print
        double prediction = nn.predict(inputs[i]);
        std::cout << "Input: " << inputs[i] << " Predicted: " << prediction << " Actual: " << targets[i] << std::endl;
    }

    return 0;
}
