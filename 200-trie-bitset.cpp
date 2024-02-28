#include <iostream>
#include <map>
#include <string>
#include <bitset>

class PatriciaNode {
public:
    bool isEndOfWord;
    std::map<std::string, PatriciaNode*> children;

    PatriciaNode() : isEndOfWord(false) {}
};

class PatriciaTrie {
private:
    PatriciaNode* root;

    void insertHelper(PatriciaNode* node, const std::string& word, size_t index) {
        if (index == word.length()) {
            node->isEndOfWord = true; // Mark the end of a word
            return;
        }

        std::string remaining = word.substr(index); // The remaining substring to insert
        for (auto& child : node->children) {
            const std::string& key = child.first;
            PatriciaNode* childNode = child.second;

            // Find the common prefix between the remaining word and this child's key
            size_t commonLength = 0;
            while (commonLength < key.length() && commonLength < remaining.length()
                   && key[commonLength] == remaining[commonLength]) {
                commonLength++;
            }

            if (commonLength > 0) {
                // If there's a common prefix, split the child if necessary
                if (commonLength < key.length()) {
                    // Split the child's key
                    std::string newKey = key.substr(0, commonLength);
                    std::string oldKey = key.substr(commonLength);
                    PatriciaNode* newChildNode = new PatriciaNode();
                    newChildNode->children[oldKey] = childNode;
                    node->children[newKey] = newChildNode;
                    node->children.erase(key);

                    if (commonLength < remaining.length()) {
                        // Insert the remaining part of the word into the new child
                        insertHelper(newChildNode, word, index + commonLength);
                    } else {
                        newChildNode->isEndOfWord = true;
                    }
                    return;
                } else if (commonLength == remaining.length()) {
                    // The word is already in the trie
                    childNode->isEndOfWord = true;
                    return;
                } else {
                    // Continue inserting into the child node
                    insertHelper(childNode, word, index + commonLength);
                    return;
                }
            }
        }

        // If no common prefix is found, create a new child
        node->children[remaining] = new PatriciaNode();
        insertHelper(node->children[remaining], word, word.length());
    }

    void printHelper(const PatriciaNode* node, const std::string& prefix, int level) const {
        std::string indent(level * 2, ' '); // 2 spaces per level for indentation
        if (!prefix.empty()) { // Check to avoid printing the root node's empty prefix
            std::cout << indent << "-> " << prefix;
            if (node->isEndOfWord) std::cout << " (end)";
            std::cout << std::endl;
        }

        for (const auto& child : node->children) {
            printHelper(child.second, child.first, level + 1);
        }
    }

public:
    PatriciaTrie() {
        root = new PatriciaNode();
    }

    void insert(const std::string& word) {
        insertHelper(root, word, 0);
    }

     void print() const {
        printHelper(root, "", 0);
    }
};
    
int main() {
    PatriciaTrie trie;

    // Convert and insert "hello"
    std::string hello = "hello";
    std::string helloBinary;
    for (char c : hello) {
        helloBinary += std::bitset<8>(c).to_string(); // Convert each character to a binary string
    }
    trie.insert(helloBinary);

    // Convert and insert "helium"
    std::string helium = "helium";
    std::string heliumBinary;
    for (char c : helium) {
        heliumBinary += std::bitset<8>(c).to_string(); // Convert each character to a binary string
    }
    trie.insert(heliumBinary);

    // Convert and insert "help"
    std::string help = "help";
    std::string helpBinary;
    for (char c : help) {
        helpBinary += std::bitset<8>(c).to_string(); // Convert each character to a binary string
    }
    trie.insert(helpBinary);

    std::cout << "ASCII encoded binary words inserted." << std::endl;

    trie.print();

    return 0;
}