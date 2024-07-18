#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <random>
#include <memory>

// Sales record structure
struct SalesRecord {
    int transaction_id;
    std::string date;
    std::string product_id;
    int quantity;
    double price;

    std::string serialize() const {
        std::stringstream ss;
        ss << transaction_id << "|" << date << "|" << product_id << "|" << quantity << "|" << price;
        return ss.str();
    }

    static SalesRecord deserialize(const std::string& data) {
        std::stringstream ss(data);
        SalesRecord record;
        ss >> record.transaction_id;
        ss.ignore(1, '|');
        std::getline(ss, record.date, '|');
        std::getline(ss, record.product_id, '|');
        ss >> record.quantity;
        ss.ignore(1, '|');
        ss >> record.price;
        return record;
    }
};

// Huffman Coding
class Huffman {
public:
    struct HuffmanNode {
        std::string data;
        int freq;
        HuffmanNode* left;
        HuffmanNode* right;

        HuffmanNode(const std::string& data, int freq) : data(data), freq(freq), left(nullptr), right(nullptr) {}
    };

    struct CompareNode {
        bool operator()(HuffmanNode* l, HuffmanNode* r) {
            return l->freq > r->freq;
        }
    };

    std::unordered_map<std::string, std::string> dict;
    std::unordered_map<std::string, std::string> reverse_dict;
    std::vector<unsigned char> encoded_data;

    Huffman(const std::vector<SalesRecord>& table) {
        std::unordered_map<std::string, int> freq_map;
        for (const auto& record : table) {
            freq_map[record.product_id]++;
        }
        buildHuffmanCodes(freq_map);
        encodeProductIDs(table);
    }

    ~Huffman() {
        deleteHuffmanTree(root);
    }

    void encodeProductIDs(const std::vector<SalesRecord>& table) {
        for (const auto& record : table) {
            std::string encoded = dict[record.product_id];
            encoded_data.push_back(static_cast<unsigned char>(encoded.size()));
            for (char c : encoded) {
                encoded_data.push_back(c);
            }
        }
    }

    std::string encode(const std::string& data) {
        return dict[data];
    }

    std::string decode(const std::string& encoded_data) {
        std::string decoded;
        std::string current_code;
        for (char bit : encoded_data) {
            current_code += bit;
            if (reverse_dict.find(current_code) != reverse_dict.end()) {
                decoded += reverse_dict[current_code];
                current_code.clear();
            }
        }
        return decoded;
    }

    std::string performQuery(const std::vector<SalesRecord>& table, const std::string& product_id) {
        int total_quantity = 0;
        double total_sales = 0.0;

        std::string encoded_product_id = encode(product_id);

        size_t i = 0;
        size_t row_index = 0;  // To keep track of the current row in the table

        while (i < encoded_data.size()) {
            unsigned char length = encoded_data[i];
            i++;
            bool match = true;
            for (size_t j = 0; j < length; ++j) {
                if (encoded_data[i + j] != encoded_product_id[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                total_quantity += table[row_index].quantity;
                total_sales += table[row_index].quantity * table[row_index].price;
            }
            i += length;
            row_index++;  // Move to the next row in the table
        }

        std::stringstream result;
        result << "Total quantity sold: " << total_quantity << ", Total sales: $" << total_sales;
        return result.str();
    }

    void print() {
        std::cout << "\n\n*****************\n";
        std::cout << "Huffman Dictionary:" << std::endl;
        for (const auto& pair : dict) {
            std::cout << pair.first << ": " << pair.second << std::endl;
        }

        std::cout << "Encoded Data (first 20):" << std::endl;
        size_t count = 0;
        for (size_t i = 0; i < encoded_data.size() && count < 20; ) {
            unsigned char length = encoded_data[i];
            std::cout << static_cast<int>(length) << " ";
            for (size_t j = 0; j < length; ++j) {
                std::cout << encoded_data[i + 1 + j];
            }
            std::cout << std::endl;
            i += 1 + length;
            count++;
        }
    }

private:
    HuffmanNode* root;

    void buildHuffmanCodes(const std::unordered_map<std::string, int>& freq_map) {
        std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, CompareNode> minHeap;

        for (const auto& pair : freq_map) {
            minHeap.push(new HuffmanNode(pair.first, pair.second));
        }

        while (minHeap.size() > 1) {
            HuffmanNode* left = minHeap.top();
            minHeap.pop();
            HuffmanNode* right = minHeap.top();
            minHeap.pop();

            HuffmanNode* newNode = new HuffmanNode("", left->freq + right->freq);
            newNode->left = left;
            newNode->right = right;
            minHeap.push(newNode);
        }

        root = minHeap.top();
        buildCodes(root, "");
    }

    void buildCodes(HuffmanNode* node, const std::string& str) {
        if (!node) return;
        if (!node->data.empty()) {
            dict[node->data] = str;
            reverse_dict[str] = node->data;
        }
        buildCodes(node->left, str + "0");
        buildCodes(node->right, str + "1");
    }

    void deleteHuffmanTree(HuffmanNode* node) {
        if (!node) return;
        deleteHuffmanTree(node->left);
        deleteHuffmanTree(node->right);
        delete node;
    }
};

// Byte Dictionary Encoding
class ByteDictionary {
public:
    std::unordered_map<std::string, unsigned char> dict;
    std::unordered_map<unsigned char, std::string> reverse_dict;
    std::vector<unsigned char> encoded_data;

    ByteDictionary(const std::vector<SalesRecord>& table) {
        std::unordered_map<std::string, int> freq_map;
        for (const auto& record : table) {
            freq_map[record.product_id]++;
        }

        int dict_size = 0;
        for (const auto& pair : freq_map) {
            if (dict_size < 256) {
                dict[pair.first] = static_cast<unsigned char>(dict_size);
                reverse_dict[static_cast<unsigned char>(dict_size)] = pair.first;
                dict_size++;
            } else {
                break;
            }
        }

        for (const auto& record : table) {
            if (dict.find(record.product_id) != dict.end()) {
                encoded_data.push_back(dict[record.product_id]);
            } else {
                encoded_data.push_back(static_cast<unsigned char>(dict_size));
                reverse_dict[static_cast<unsigned char>(dict_size)] = record.product_id;
                dict_size++;
            }
        }
    }

    std::vector<SalesRecord> decode(const std::vector<SalesRecord>& original_table) const {
        std::vector<SalesRecord> decoded_table;
        for (size_t i = 0; i < encoded_data.size(); i++) {
            SalesRecord record = original_table[i];
            record.product_id = reverse_dict.at(encoded_data[i]);
            decoded_table.push_back(record);
        }
        return decoded_table;
    }

    std::string performQuery(const std::vector<SalesRecord>& table, const std::string& product_id) const {
        int total_quantity = 0;
        double total_sales = 0.0;

        unsigned char encoded_product_id = dict.at(product_id);

        for (size_t i = 0; i < encoded_data.size(); i++) {
            if (encoded_data[i] == encoded_product_id) {
                total_quantity += table[i].quantity;
                total_sales += table[i].quantity * table[i].price;
            }
        }

        std::stringstream result;
        result << "Total quantity sold: " << total_quantity << ", Total sales: $" << total_sales;
        return result.str();
    }

    void print() const {
        std::cout << "\n\n*****************\n";
        std::cout << "Byte Dictionary:" << std::endl;
        for (const auto& pair : dict) {
            std::cout << pair.first << ": " << static_cast<int>(pair.second) << std::endl;
        }

        std::cout << "Encoded Data (first 20):" << std::endl;
        for (size_t i = 0; i < std::min(encoded_data.size(), size_t(20)); ++i) {
            std::cout << static_cast<int>(encoded_data[i]) << " ";
        }
        std::cout << std::endl;
    }
};

// Measure query performance
std::string performUncompressedQuery(const std::vector<SalesRecord>& table, const std::string& product_id) {
    int total_quantity = 0;
    float total_sales = 0.0f;

    for (const auto& record : table) {
        if (record.product_id == product_id) {
            total_quantity += record.quantity;
            total_sales += record.quantity * record.price;
        }
    }

    std::stringstream result;
    result << "Total quantity sold: " << total_quantity << ", Total sales: $" << total_sales;
    return result.str();
}

std::string performCompressedQuery(const std::vector<SalesRecord>& table, const std::unordered_map<std::string, std::string>& huffmanCodes, const std::string& product_id) {
    int total_quantity = 0;
    double total_sales = 0.0;

    std::string encoded_product_id = huffmanCodes.at(product_id);

    for (const auto& record : table) {
        std::string encoded = huffmanCodes.at(record.product_id);
        if (encoded == encoded_product_id) {
            total_quantity += record.quantity;
            total_sales += record.quantity * record.price;
        }
    }

    std::stringstream result;
    result << "Total quantity sold: " << total_quantity << ", Total sales: $" << total_sales;
    return result.str();
}

// Custom skewed distribution for product IDs
int zipf(double alpha, int n) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);

    static std::vector<double> cdf;
    if (cdf.empty()) {
        double sum = 0.0;
        for (int i = 1; i <= n; i++) {
            sum += 1.0 / std::pow(i, alpha);
        }
        cdf.resize(n + 1);
        cdf[0] = 0;
        for (int i = 1; i <= n; i++) {
            cdf[i] = cdf[i - 1] + (1.0 / std::pow(i, alpha)) / sum;
        }
    }

    double r = dis(gen);
    for (int i = 1; i <= n; i++) {
        if (r < cdf[i]) {
            return i;
        }
    }
    return n;
}

// Helper function to generate a random alphanumeric string of a given length
std::string generateRandomString(int length) {
    static const std::string alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, alphanum.size() - 1);

    std::string result;
    for (int i = 0; i < length; ++i) {
        result += alphanum[dis(gen)];
    }
    return result;
}

// Generate a larger sales table with a skewed distribution for product IDs
std::vector<SalesRecord> generateSalesTable(int num_records, int num_products, double alpha) {
    std::vector<SalesRecord> table;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis_date(1, 28);
    std::uniform_int_distribution<> dis_quantity(1, 10);
    std::uniform_real_distribution<> dis_price(10.0, 100.0);

    // Pre-generate a list of product IDs based on a skewed distribution
    std::vector<std::string> product_ids(num_products);
    for (int i = 0; i < num_products; ++i) {
        // Generate 30-character alphanumeric strings
        product_ids[i] = generateRandomString(30);  
    }

    for (int i = 1; i <= num_records; ++i) {
        std::stringstream date;
        date << "2021-01-" << (dis_date(gen) < 10 ? "0" : "") << dis_date(gen);
        int product_index = zipf(alpha, num_products);
        std::string product_id = product_ids[product_index - 1];  // Adjust index to be 0-based

        SalesRecord record = {i, date.str(), product_id, dis_quantity(gen), dis_price(gen)};
        if (i % 1000 == 0) {
            std::cout << product_id << "\n";
        }
        table.push_back(record);
    }

    return table;
}


int main() {

    // Generate a larger sales table
    int num_records = 300000; // Number of sales records
    int num_products = 30; // Number of unique products
    double alpha = 1.0; // Skewness parameter (higher values mean more skew)    
    std::vector<SalesRecord> sales_table = generateSalesTable(num_records, num_products, alpha);


    // Serialize the sales table
    std::stringstream ss;
    for (const auto& record : sales_table) {
        ss << record.serialize() << "\n";
    }
    std::string serialized_data = ss.str();

    // Collect all product IDs for Huffman encoding
    std::vector<std::string> all_product_ids;
    for (const auto& record : sales_table) {
        all_product_ids.push_back(record.product_id);
    }

    // Compress product IDs using Huffman Coding
    Huffman huffman(sales_table);

    // Compress data using Byte Dictionary Encoding
    ByteDictionary byteDict(sales_table);

    std::string product_id = sales_table[2].product_id;

    // Perform query without compression
    auto start = std::chrono::high_resolution_clock::now();
    std::string query_result = performUncompressedQuery(sales_table, product_id);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> uncompressed_duration = end - start;

    // Perform query with Huffman compressed data
    huffman.print();
    start = std::chrono::high_resolution_clock::now();
    std::string huffman_query_result = huffman.performQuery(sales_table, product_id);
    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> huffman_duration = end - start;

    // Perform query with Byte Dictionary encoded data
    byteDict.print();
    start = std::chrono::high_resolution_clock::now();
    std::string byte_dict_query_result = byteDict.performQuery(sales_table, product_id);
    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> byte_dict_duration = end - start;

    // Print results
    std::cout << "Uncompressed query result: " << query_result << std::endl;
    std::cout << "Uncompressed query time: " << uncompressed_duration.count() << " seconds" << std::endl;
    std::cout << "Huffman compressed query result: " << huffman_query_result << std::endl;
    std::cout << "Huffman compressed query time: " << huffman_duration.count() << " seconds" << std::endl;
    std::cout << "Byte Dictionary compressed query result: " << byte_dict_query_result << std::endl;
    std::cout << "Byte Dictionary compressed query time: " << byte_dict_duration.count() << " seconds" << std::endl;

    return 0;
}
