#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>
#include <memory>
#include <cassert>

struct Model {
    virtual ~Model() {}
    virtual void train(const std::vector<int>& inputs, const std::vector<double>& targets) = 0;
    virtual double predict(int x) const = 0;
    virtual void print() const = 0;
};

struct LinearRegression : public Model {
    double slope = 0.0, intercept = 0.0;

    void train(const std::vector<int>& inputs, const std::vector<double>& targets) override {
        double mean_x = 0, mean_y = 0;
        int n = inputs.size();
        for (int i = 0; i < n; ++i) {
            mean_x += inputs[i];
            mean_y += targets[i];
        }
        mean_x /= n;
        mean_y /= n;

        double num = 0.0, denom = 0.0;
        for (int i = 0; i < n; ++i) {
            num += (inputs[i] - mean_x) * (targets[i] - mean_y);
            denom += (inputs[i] - mean_x) * (inputs[i] - mean_x);
        }

        slope = num / denom;
        intercept = mean_y - slope * mean_x;
    }

    double predict(int x) const override {
        return slope * x + intercept;
    }

    void print() const override {
        std::cout << "Linear Model: y = " << slope << " * x + " << intercept << std::endl;
    }
};


class SimpleNeuralNetwork : public Model  {
private:
    std::vector<double> input_weights;
    std::vector<double> output_weights;
    int hidden_neurons;
    double input_bias;
    double output_bias;
    int max_input;
    double max_target;

    double sigmoid(double x) const {
        return 1.0 / (1.0 + exp(-x));
    }

    // Normalize data to range [0, 1]
    double normalize(int x, int max) const {
        return static_cast<double>(x) / max;
    }

public:
    SimpleNeuralNetwork(int hidden_size = 10) : hidden_neurons(hidden_size), input_bias(0), output_bias(0) {
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

    void train(const std::vector<int>& inputs, const std::vector<double>& targets) {

        // Automatically determine max_input and max_target
        max_input = *std::max_element(inputs.begin(), inputs.end());
        max_target = *std::max_element(targets.begin(), targets.end());

        std::cout << max_input << " " << max_target << "\n";

        double learning_rate = 0.001;
        int epochs = 100000;
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

                for (int h = 0; h < hidden_neurons; ++h) {
                    double delta_hidden = delta_output * output_weights[h] * hidden_outputs[h] * (1 - hidden_outputs[h]);
                    output_weights[h] += learning_rate * delta_output * hidden_outputs[h];
                    input_weights[h] += learning_rate * delta_hidden * normalized_input;
                    input_bias += learning_rate * delta_hidden;
                }
            }
        }
    }

    double predict(int x) const {
        double normalized_input = normalize(x, max_input);
        double output = output_bias;
        for (int h = 0; h < hidden_neurons; ++h) {
            double hidden_output = sigmoid(normalized_input * input_weights[h] + input_bias);
            output += hidden_output * output_weights[h];
        }
        return (sigmoid(output) * max_target);
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


struct RMI {
    std::unique_ptr<Model> root_model;
    std::vector<std::unique_ptr<Model>> sub_models;
    std::vector<std::vector<std::pair<int, std::string>>> leaves;  // Data stored in leaf nodes

    void initModels(bool use_neural_network) {
        if (use_neural_network) {
            root_model = std::make_unique<SimpleNeuralNetwork>();
            for (int i = 0; i < 4; ++i) {
                sub_models.push_back(std::make_unique<SimpleNeuralNetwork>());
            }
        } else {
            root_model = std::make_unique<LinearRegression>();
            for (int i = 0; i < 4; ++i) {
                sub_models.push_back(std::make_unique<LinearRegression>());
            }
        }
    }

    void train(const std::vector<std::pair<int, std::string>>& data, int num_sub_models) {
        std::vector<std::pair<int, std::string>> sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        // Split data into clusters for sub-models
        size_t chunk_size = data.size() / num_sub_models;
        leaves.resize(num_sub_models);
        sub_models.resize(num_sub_models);

        std::vector<int> cluster_keys;
        std::vector<double> cluster_positions;

        for (int i = 0; i < num_sub_models; ++i) {
            cluster_keys.clear();
            cluster_positions.clear();
            size_t start = i * chunk_size;
            size_t end = (i + 1) * chunk_size;

            for (size_t j = start; j < end; ++j) {
                cluster_keys.push_back(sorted_data[j].first);
                cluster_positions.push_back(j);
                leaves[i].push_back(sorted_data[j]);
            }

            sub_models[i]->train(cluster_keys, cluster_positions);
        }

        // Train root model with more comprehensive data
        std::vector<int> root_keys;
        std::vector<double> root_positions;
        for (int i = 0; i < num_sub_models; ++i) {
            for (size_t j = 0; j < leaves[i].size(); ++j) {
                root_keys.push_back(leaves[i][j].first);
                //std::cout << leaves[i][j].first << "\n";
                root_positions.push_back(i);  // Map to sub-model index
            }
        }

        root_model->train(root_keys, root_positions);
    }

    std::string search(int key) {
        int sub_model_idx = root_model->predict(key);
        std::cout << "Submodel: " << sub_model_idx << "\n";
        sub_model_idx = std::min(sub_model_idx, (int)sub_models.size() - 1);  // Bound check
        size_t pos = sub_models[sub_model_idx]->predict(key);
        std::cout << "Position predicted by submodel: " << pos << "\n";
        pos = std::min(pos, leaves[sub_model_idx].size() - 1);  // Bound check within leaf

        // Check a neighborhood around the predicted position
        int search_radius = 5; // Check some positions around the predicted position
        int start = std::max(0, int(pos) - search_radius);
        int end = std::min(int(leaves[sub_model_idx].size() - 1), 
                        int(pos) + search_radius);

        std::cout << "Searching radius \n";
        for (int i = start; i < end; ++i) {
            int key_at_i = leaves[sub_model_idx][i].first;
            std::string value_at_i = leaves[sub_model_idx][i].second;
            std::cout << key_at_i << " " << value_at_i << "\n";
            if (key == key_at_i) {
                return value_at_i;
            }
        }
        return "Key not found";
    }

    void print() {
        root_model->print();
        for (auto& model : sub_models) {
            model->print();
        }
    }
};

int main() {
    std::vector<std::pair<int, std::string>> data;
    std::default_random_engine generator(42);
    std::uniform_int_distribution<int> distribution(0, 1000);

    // Generate synthetic clustered data
    for (int cluster = 0; cluster < 4; ++cluster) {
        int base = cluster * 250;  // Cluster base to create distinct ranges
        for (int i = 0; i < 25; ++i) {
            int key = base + distribution(generator) % 250;
            data.emplace_back(key, "Value_" + std::to_string(key));
        }
    }

    RMI index;
    index.initModels(true);
    index.train(data, 4);
    //index.print();

    // Test search
    std::vector<int> search_keys = {457, 110, 908, 991};
    for(auto search_key: search_keys){
        std::cout << "\nSearch for key " << search_key << " : " << 
        index.search(search_key) << std::endl;
    }

    return 0;
}
