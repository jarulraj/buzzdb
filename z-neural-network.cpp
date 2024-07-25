#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

class SimpleNeuralNetwork {
private:
    std::vector<double> input_weights;
    std::vector<double> output_weights;
    int hidden_neurons;
    double input_bias;
    double output_bias;

    double sigmoid(double x) const {
        return 1.0 / (1.0 + exp(-x));
    }

    // Normalize data to range [0, 1]
    double normalize(int x, int max) const {
        return static_cast<double>(x) / max;
    }

public:
    SimpleNeuralNetwork(int hidden_size = 100) : hidden_neurons(hidden_size), input_bias(0), output_bias(0) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(-1.0, 1.0);

        input_weights.resize(hidden_size);
        output_weights.resize(hidden_size);
        for (int i = 0; i < hidden_size; ++i) {
            input_weights[i] = dis(gen);
            output_weights[i] = dis(gen);
        }

        input_bias = dis(gen);
        output_bias = dis(gen);
    }

    void train(const std::vector<int>& inputs, const std::vector<double>& targets, int max_input, int max_target) {
        double learning_rate = 0.001;
        int epochs = 10000;
        for (int epoch = 0; epoch < epochs; ++epoch) {
            for (size_t i = 0; i < inputs.size(); ++i) {
                double normalized_input = normalize(inputs[i], max_input);
                double normalized_target = normalize(targets[i], max_target);

                // Forward pass
                std::vector<double> hidden_outputs(hidden_neurons, 0.0);
                double output = output_bias;
                for (int h = 0; h < hidden_neurons; ++h) {
                    hidden_outputs[h] = sigmoid(normalized_input * input_weights[h] + input_bias);
                    output += hidden_outputs[h] * output_weights[h];
                }
                double prediction = sigmoid(output);

                // Backward pass
                double error = normalized_target - prediction;
                double delta_output = error * prediction * (1 - prediction);
                output_bias += learning_rate * delta_output;

                if(epoch % 10000 == 0){
                    std::cout << "Error: " << error << "\n";
                }

                for (int h = 0; h < hidden_neurons; ++h) {
                    double delta_hidden = delta_output * output_weights[h] * hidden_outputs[h] * (1 - hidden_outputs[h]);
                    output_weights[h] += learning_rate * delta_output * hidden_outputs[h];
                    input_weights[h] += learning_rate * delta_hidden * normalized_input;
                    input_bias += learning_rate * delta_hidden;
                }
            }
        }
    }

    double predict(int x, int max_input) const {
        double normalized_input = normalize(x, max_input);
        double output = output_bias;
        for (int h = 0; h < hidden_neurons; ++h) {
            double hidden_output = sigmoid(normalized_input * input_weights[h] + input_bias);
            output += hidden_output * output_weights[h];
        }
        return sigmoid(output);
    }

    void print() const {
        std::cout << "Neural Network Model:\n";
        std::cout << "Input Weights: ";
        for (const auto& w : input_weights) std::cout << w << " ";
        std::cout << "\nOutput Weights: ";
        for (const auto& w : output_weights) std::cout << w << " ";
        std::cout << "\nInput Bias: " << input_bias << "\nOutput Bias: " << output_bias << std::endl;
    }
};

int main() {
    int num_data = 100;
    int max_input = 1000;
    int max_target = 100;
    std::vector<int> inputs(num_data);
    std::vector<double> targets(num_data);

    // Generate linearly spaced inputs and map them to targets
    for (int i = 0; i < num_data; ++i) {
        inputs[i] = i * (max_input / num_data);
        targets[i] = i * (max_target / num_data);
    }

    SimpleNeuralNetwork nn;
    nn.train(inputs, targets, max_input, max_target);

    // Predict and display results
    nn.print();
    for (int i = 0; i < num_data; i += 10) {
        double prediction = nn.predict(inputs[i], max_input) * max_target;  // De-normalize output
        std::cout << "Input: " << inputs[i] << " Predicted: " << prediction << " Actual: " << targets[i] << std::endl;
    }

    return 0;
}
