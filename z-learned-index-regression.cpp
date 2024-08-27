#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <random>

struct Model {
    double slope = 0.0;
    double intercept = 0.0;

    void train(const std::vector<int>& keys, const std::vector<size_t>& positions) {
        double mean_x = 0, mean_y = 0;
        for (size_t i = 0; i < keys.size(); ++i) {
            mean_x += keys[i];
            mean_y += positions[i];
        }
        mean_x /= keys.size();
        mean_y /= keys.size();

        double num = 0.0, denom = 0.0;
        for (size_t i = 0; i < keys.size(); ++i) {
            double x_diff = keys[i] - mean_x;
            num += x_diff * (positions[i] - mean_y);
            denom += x_diff * x_diff;
        }

        slope = num / denom;
        intercept = mean_y - slope * mean_x;
    }

    size_t predict(int key) const {
        return static_cast<size_t>(std::max(0.0, slope * key + intercept));
    }

    void print() const {
        std::cout << "Model: y = " << slope << " * x + " << intercept << std::endl;
    }
};

struct RMI {
    Model root_model;
    std::vector<Model> sub_models;
    std::vector<std::vector<std::pair<int, std::string>>> leaves;  // Data stored in leaf nodes

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
        std::vector<size_t> cluster_positions;

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

            sub_models[i].train(cluster_keys, cluster_positions);
        }

        // Train root model with more comprehensive data
        std::vector<int> root_keys;
        std::vector<size_t> root_positions;
        for (int i = 0; i < num_sub_models; ++i) {
            for (size_t j = 0; j < leaves[i].size(); ++j) {
                root_keys.push_back(leaves[i][j].first);
                std::cout << leaves[i][j].first << "\n";
                root_positions.push_back(i);  // Map to sub-model index
            }
        }

        root_model.train(root_keys, root_positions);
    }

    std::string search(int key) {
        int sub_model_idx = root_model.predict(key);
        std::cout << "Submodel: " << sub_model_idx << "\n";
        sub_model_idx = std::min(sub_model_idx, (int)sub_models.size() - 1);  // Bound check
        size_t pos = sub_models[sub_model_idx].predict(key);
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
        root_model.print();
        for (auto& model : sub_models) {
            model.print();
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
    index.train(data, 4);
    index.print();

    // Test search
    std::vector<int> search_keys = {457, 110, 908, 991};
    for(auto search_key: search_keys){
        std::cout << "\nSearch for key " << search_key << " : " << 
        index.search(search_key) << std::endl;
    }

    return 0;
}
