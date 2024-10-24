#include <iostream>
#include <stdexcept>

// Binary Patricia Node structure
class PatriciaNode {
public:
    bool isEndOfWord;
    int value;  // Value associated with the key
    PatriciaNode* left;  // Corresponds to 0
    PatriciaNode* right; // Corresponds to 1
    int skip; // Number of bits skipped in the compressed path
    int keyPart; // Store the key part for comparison purposes

    PatriciaNode(int keyPart = 0, int skip = 0)
        : isEndOfWord(false), value(0), left(nullptr), right(nullptr), skip(skip), keyPart(keyPart) {}
};

// Binary Patricia Trie with Compression
class BinaryPatriciaTrie {
private:
    PatriciaNode* root;

    // Helper to extract a specific bit from a given integer
    bool getBit(int num, int index) const {
        return (num >> index) & 1;
    }

    // Helper function to calculate the length of the common prefix between two integers
    int commonPrefixLength(int key1, int key2, int maxBits) const {
        int i = 0;
        while (i <= maxBits && getBit(key1, i) == getBit(key2, i)) {
            i++;
        }
        return i;
    }

    // Insert helper to recursively insert a key
    PatriciaNode* insertHelper(PatriciaNode* node, int key, int value, int bitIndex) {
        if (node == nullptr) {
            node = new PatriciaNode(key, bitIndex);
            node->isEndOfWord = true;
            node->value = value;
            return node;
        }

        // Calculate the common prefix between the current node's key part and the new key
        int commonLength = commonPrefixLength(node->keyPart, key, bitIndex);

        // If we have a full match (all bits match), proceed with the insertion
        if (commonLength == bitIndex) {
            node->isEndOfWord = true;
            node->value = value;
            return node;
        }

        // If the current node fully matches the existing key (key divergence happens at a lower level)
        if (commonLength == node->skip) {
            bool bit = getBit(key, bitIndex - node->skip);
            if (bit == 0) {
                node->left = insertHelper(node->left, key, value, bitIndex - node->skip - 1);
            } else {
                node->right = insertHelper(node->right, key, value, bitIndex - node->skip - 1);
            }
        } else {
            // Partial match, need to split the current node
            PatriciaNode* newInternalNode = new PatriciaNode(key, commonLength);

            // Set up the new internal node
            bool bit = getBit(node->keyPart, commonLength);
            if (bit == 0) {
                newInternalNode->left = node;
                newInternalNode->right = new PatriciaNode(key, bitIndex - commonLength);
                newInternalNode->right->isEndOfWord = true;
                newInternalNode->right->value = value;
            } else {
                newInternalNode->right = node;
                newInternalNode->left = new PatriciaNode(key, bitIndex - commonLength);
                newInternalNode->left->isEndOfWord = true;
                newInternalNode->left->value = value;
            }
            return newInternalNode;
        }

        return node;
    }

    // Search helper to find a key in the trie
    int searchHelper(PatriciaNode* node, int key, int bitIndex) const {
        if (node == nullptr) {
            throw std::runtime_error("Key not found");
        }

        // Calculate common prefix between node keyPart and the key
        int commonLength = commonPrefixLength(node->keyPart, key, bitIndex);

        if (commonLength == bitIndex) {
            if (node->isEndOfWord) {
                return node->value;
            } else {
                throw std::runtime_error("Key not found");
            }
        }

        bool bit = getBit(key, bitIndex - node->skip);
        if (bit == 0) {
            return searchHelper(node->left, key, bitIndex - node->skip - 1);
        } else {
            return searchHelper(node->right, key, bitIndex - node->skip - 1);
        }
    }

    // Helper function to print binary representation of an integer without leading zeros
    std::string toBinaryWithoutLeadingZeros(int num) const {
        if (num == 0) return "0";  // Special case for 0

        std::string result;
        bool leadingZero = true;

        // Traverse the bits from the most significant to least significant
        for (int i = sizeof(int) * 8 - 1; i >= 0; --i) {
            if ((num >> i) & 1) {
                leadingZero = false;  // Found the first '1', so no more leading zeros
                result += '1';
            } else if (!leadingZero) {
                result += '0';  // Only add '0' once we've passed the leading zeros
            }
        }

        return result;
    }

    // Print the Trie recursively
    void printHelper(PatriciaNode* node, int key, int bitIndex) const {
        if (node == nullptr) return;

        // Print the current node (both internal and leaf)
        std::cout << "Keypart (binary): " << 
                toBinaryWithoutLeadingZeros(node->keyPart)  
                << " (Skip: " << node->skip << ")";

        if (node->isEndOfWord) {
            std::cout << " (Value: " << node->value << ")";  // Leaf node (end of word)
        }

        std::cout << "\n";

        if (node->left) {
            printHelper(node->left, key, bitIndex - node->skip - 1);
        }

        if (node->right) {
            key |= (1 << (bitIndex - node->skip)); // Set the bit to 1 for the right path
            printHelper(node->right, key, bitIndex - node->skip - 1);
        }
    }

public:
    BinaryPatriciaTrie() : root(nullptr) {}

    // Insert a key-value pair into the Trie
    void insert(int key, int value) {
        root = insertHelper(root, key, value, sizeof(int) * 8 - 1);
    }

    // Search for a key in the Trie and return its value
    int search(int key) const {
        return searchHelper(root, key, sizeof(int) * 8 - 1);
    }

    // Print all the keys in the Trie
    void print() const {
        printHelper(root, 0, sizeof(int) * 8 - 1);
    }
};

int main() {
    BinaryPatriciaTrie trie;

    // Example: Insert integers and their corresponding values
    trie.insert(5, 100);   // Binary: 101
    trie.insert(9, 200);   // Binary: 1001
    trie.insert(15, 300);  // Binary: 1111
    trie.insert(7, 400);   // Binary: 111
    trie.insert(3, 500);   // Binary: 11

    std::cout << "Keys inserted into the Binary Patricia Trie:" << std::endl;
    trie.print();

    // Example: Search for keys
    try {
        std::cout << "Value for key 5: " << trie.search(5) << std::endl;
        std::cout << "Value for key 9: " << trie.search(9) << std::endl;
        std::cout << "Value for key 3: " << trie.search(3) << std::endl;
        std::cout << "Value for key 7: " << trie.search(7) << std::endl;
        std::cout << "Value for key 15: " << trie.search(15) << std::endl;

        // Searching for a non-existing key will throw an exception
        std::cout << "Value for key 4: " << trie.search(4) << std::endl;
    } catch (const std::runtime_error& e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
