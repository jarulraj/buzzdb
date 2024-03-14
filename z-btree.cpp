#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm> // For std::shuffle
#include <random>    // For std::default_random_engine

#include <list>
#include <unordered_map>
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread>
#include <queue>
#include <optional>

template <typename Key, typename Value>
class BPlusTree {
public:
    struct Node {
        std::vector<Key> keys;
        std::vector<Value> values; // Only used in leaf nodes
        std::vector<std::shared_ptr<Node>> children; // Only used in internal nodes
        std::shared_ptr<Node> next = nullptr; // Next leaf node
        bool isLeaf = false;

        Node(bool leaf) : isLeaf(leaf) {}
    };

     int getValue(const Key& key) const {
        auto node = root;
        while (node != nullptr) {
            if (node->isLeaf) {
                auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
                if (it != node->keys.end() && *it == key) { // Key found in leaf
                    auto index = std::distance(node->keys.begin(), it);
                    return node->values[index];
                }
                break; // Key not found
            } else {
                auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
                node = (it == node->keys.begin()) ? node->children[0] :
                    node->children[std::distance(node->keys.begin(), it)];
            }
        }
        return -1; // Indicate key not found
    }

    std::vector<Value> rangeQuery(const Key& lowerBound, const Key& upperBound) const {
        std::vector<Value> result;
        auto node = root;

        // Traverse to the leaf node containing the lowerBound
        while (node && !node->isLeaf) {
            size_t i = 0;
            while (i < node->keys.size() && lowerBound > node->keys[i]) {
                ++i;
            }
            node = node->children[i];
        }

        // Perform leaf-level traversal using `next`
        while (node) {
            for (size_t i = 0; i < node->keys.size(); ++i) {
                if (node->keys[i] > upperBound) return result;
                if (node->keys[i] >= lowerBound) {
                    std::cout << node->values[i] << " ";
                    result.push_back(node->values[i]);
                }
            }
            node = node->next;
        }

        return result;
    }

    // Function to get a shared_ptr to the root node
    std::shared_ptr<Node> getRoot() const {
        return root;
    }

    void print() const {
        printRecursive(root, 0);
        std::cout << std::endl;
    }

    void printRecursive(std::shared_ptr<Node> node, int level) const {
        if (!node) return;

        // Indentation for readability, based on the level
        std::string indent(level * 2, ' ');

        std::cout << indent << "(L" << level << ") ";

        // Print the node keys
        for (const auto& key : node->keys) {
            std::cout << key << " ";
        }
        std::cout << "\n";

 
        // If it's not a leaf node, recursively print its children and their values
        for (const auto& child : node->children) {
            printRecursive(child, level + 1);
        }

    }


    BPlusTree(size_t order) : maxKeys(order), root(std::make_shared<Node>(true)) {}

    void insertOrUpdate(const Key& key, const Value& value) {
        auto node = root;
        std::vector<std::shared_ptr<Node>> path; // Track the path for backtracking

        // Traverse to find the correct leaf node for the key
        while (!node->isLeaf) {
            path.push_back(node);
            auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
            size_t index = it - node->keys.begin();
            node = node->children[index];
        }

        // Attempt to find the key in the leaf node
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        if (it != node->keys.end() && *it == key) {
            // Key found, update its value
            size_t pos = std::distance(node->keys.begin(), it);
            node->values[pos] += value;
            std::cout << "Updating key " << key << " in node with first key " << node->keys.front() << std::endl;
        } else {
            // Key not found, insert new key-value pair
            size_t pos = it - node->keys.begin();
            std::cout << "Inserting key " << key << " into node with first key ";
            if (node->keys.empty()) {
                std::cout << "N/A (empty node)";
            } else {
                std::cout << node->keys.front();
            }
            std::cout << std::endl;
            node->keys.insert(node->keys.begin() + pos, key);
            node->values.insert(node->values.begin() + pos, value);

            // Check for node overflow and split if necessary
            if (node->keys.size() > maxKeys) {
                splitNode(path, node);
            }
        }
    }

private:
    size_t maxKeys;
    std::shared_ptr<Node> root;

    void printNode(const std::shared_ptr<Node>& node) const {
        if (!node) {
            std::cout << "Empty Node";
            return;
        }

        if (node->isLeaf) {
            std::cout << "Leaf Node: ";
            for (const auto& key : node->keys) std::cout << key << " ";
            std::cout << "\n";
        } else {
            std::cout << "Internal Node: ";
            for (const auto& key : node->keys) std::cout << key << " ";
            std::cout << "\n";
        }
    }

    void splitNode(std::vector<std::shared_ptr<Node>> path, std::shared_ptr<Node> node) {
        auto newNode = std::make_shared<Node>(node->isLeaf);
        size_t mid = node->keys.size() / 2;
        Key midKey = node->keys[mid];

        if (node->isLeaf) {
            // Move second half of keys and values to the new node
            std::move(node->keys.begin() + mid, node->keys.end(), std::back_inserter(newNode->keys));
            std::move(node->values.begin() + mid, node->values.end(), std::back_inserter(newNode->values));

            // Update next pointers to maintain the linked list of leaf nodes
            newNode->next = node->next;
            node->next = newNode;

            // Trim original node's keys and values to reflect the split
            node->keys.resize(mid);
            node->values.resize(mid);
        } else {
            // For internal nodes, distribute keys and children to the new node
            std::move(node->keys.begin() + mid + 1, node->keys.end(), std::back_inserter(newNode->keys));
            std::move(node->children.begin() + mid + 1, node->children.end(), std::back_inserter(newNode->children));

            // Trim original node's keys and children
            node->keys.resize(mid);
            node->children.resize(mid + 1);
        }

        // Update parent or create a new root if necessary
        if (path.empty()) {
            // Create a new root if necessary
            auto newRoot = std::make_shared<Node>(false);
            newRoot->keys.push_back(midKey);
            newRoot->children.push_back(node);
            newRoot->children.push_back(newNode);
            root = newRoot;
        } else {
            auto parent = path.back();
            path.pop_back(); // Remove the last element as we're going to handle it

            // Position to insert the new child in the parent node
            size_t pos = std::distance(parent->keys.begin(), std::lower_bound(parent->keys.begin(), parent->keys.end(), midKey));
            parent->keys.insert(parent->keys.begin() + pos, midKey);
            parent->children.insert(parent->children.begin() + pos + 1, newNode);

            // Check if the parent node needs to be split
            if (parent->keys.size() > maxKeys) {
                splitNode(path, parent);
            }
        }

    }



};

class OrderedIndex {
private:
    BPlusTree<int, int> bptree;

public:

    OrderedIndex() : bptree(3) { 
    }

    void insertOrUpdate(int key, int value) {
        bptree.insertOrUpdate(key, value);
    }

    int getValue(int key) const {
        return bptree.getValue(key);
    }

    std::vector<int> rangeQuery(int lowerBound, int upperBound) const {
        return bptree.rangeQuery(lowerBound, upperBound);
    }

    void print() const {
        bptree.print();
    }
};

int main() {
    // Step 1: Initialize B+Tree
    // Assuming the degree (minimum degree 't') of B+Tree is passed to its constructor
    int t = 4; // Just an example, choose an appropriate value for your B+Tree
    BPlusTree<int, int> tree(t);

    int num_keys = 600;

   // Generate a vector of keys
    std::vector<int> keys(num_keys);
    for (int i = 0; i < num_keys; ++i) {
        keys[i] = i + 1; // Keys from 1 to 100
    }

    // Shuffle the keys to insert them in random order
    std::random_device rd; // Non-deterministic random number generator
    std::mt19937 g(rd());  // Standard mersenne_twister_engine seeded with rd()
    std::shuffle(keys.begin(), keys.end(), g);


    // Step 2: Insert elements into the tree
    for (int key : keys) {
        tree.insertOrUpdate(key, key); 
    }

    // (Optional) Step 3: Verify the structure of the tree
    // This step depends on having a method to print or otherwise inspect the structure of the tree
    tree.print(); // Hypothetical method to visualize the tree structure

    // (Optional) Step 4: Check if all inserted elements can be found
    bool correct = true;
    for (int i = 1; i <= num_keys; ++i) {
        int value = tree.getValue(i); // Assuming find returns the value associated with the key, or a special value indicating not found
        if (value != i) {
            correct = false;
            std::cout << "Error: Key " << i << " has incorrect value " << value << std::endl;
            break;
        }
    }

    if (correct) {
        std::cout << "All elements inserted and found correctly." << std::endl;
    } else {
        std::cout << "There was an error in inserting or finding elements." << std::endl;
    }

    // Now, traverse the leaf nodes using the `next` pointer and print the keys
    auto node = tree.getRoot(); // Assuming getRoot() provides access to the root node
    // Traverse down to the first leaf
    while (node && !node->isLeaf) {
        node = node->children.front();
    }

    // Now traverse through the leaf nodes using the next pointer
    std::cout << "Traversing leaf nodes: ";
    while (node != nullptr) {
        for (const auto& key : node->keys) {
            std::cout << key << " ";
        }
        node = node->next;
    }
    std::cout << std::endl;

    return 0;
}
