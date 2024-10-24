#include <iostream>
#include <map>
#include <string>

class PatriciaNode {
public:
    bool isEndOfWord;
    int value;  // Store the value for the key
    std::map<std::string, PatriciaNode*> children;

    PatriciaNode() : isEndOfWord(false), value(0) {}
};

class PatriciaTrie {
private:
    PatriciaNode* root;

    void insertHelper(PatriciaNode* node, const std::string& word, size_t index, int value) {

        std::cout << "[insertHelper] Word: '" << word << "', Index: " << index << "\n";

        if (index == word.length()) {
            node->isEndOfWord = true;
            node->value = value;  // Store the value when the word ends
            return;
        }

        std::string remaining = word.substr(index);
        for (auto& child : node->children) {
            const std::string& key = child.first;
            PatriciaNode* childNode = child.second;

            size_t commonLength = 0;
            while (commonLength < key.length() && commonLength < remaining.length() &&
                   key[commonLength] == remaining[commonLength]) {
                commonLength++;
            }

            if (commonLength > 0) {
                if (commonLength < key.length()) {
                    // Split the child's key
                    std::string newKey = key.substr(0, commonLength);
                    std::string oldKey = key.substr(commonLength);

                    std::cout << "[insertHelper] Splitting Key: '" << key << "' into '" << newKey << "' and '" << oldKey << "'\n";

                    PatriciaNode* newChildNode = new PatriciaNode();
                    newChildNode->children[oldKey] = childNode;
                    node->children[newKey] = newChildNode;
                    node->children.erase(key);

                    childNode = newChildNode;                    
                }

                insertHelper(childNode, word, index + commonLength, value);
                return;
            }
        }

        std::cout << "REMAINING: " << remaining << "\n";

        node->children[remaining] = new PatriciaNode();
        insertHelper(node->children[remaining], word, word.length(), value);
    }

    int searchHelper(PatriciaNode* node, const std::string& word, size_t index) const {
        if (index == word.length()) {
            if (node->isEndOfWord) {
                return node->value;  // Return the value if the word ends here
            }
            throw std::runtime_error("Key not found");
        }

        std::string remaining = word.substr(index);
        for (const auto& child : node->children) {
            const std::string& key = child.first;
            PatriciaNode* childNode = child.second;

            // If `key` is a prefix of `remaining`
            if (remaining.find(key) == 0) {  
                return searchHelper(childNode, word, index + key.length());
            }
        }

        throw std::runtime_error("Key not found");
    }

    void printHelper(const PatriciaNode* node, const std::string& prefix, int level) const {
        std::string indent(level * 2, ' '); // 2 spaces per level for indentation
        if (!prefix.empty()) { // Check to avoid printing the root node's empty prefix
            std::cout << indent << "-> " << prefix;
            if (node->isEndOfWord) {
                std::cout << " (end, value: " << node->value << ")";
            }
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

    void insert(const std::string& word, int value) {
        insertHelper(root, word, 0, value);
    }

    int search(const std::string& word) const {
        return searchHelper(root, word, 0);
    }

    void print() const {
        printHelper(root, "", 0);
    }
};

int main() {
    PatriciaTrie trie;
    
    trie.insert("hello", 1);
    trie.insert("helium", 2);
    trie.insert("help", 3);
    trie.insert("helicopter", 4);
    trie.insert("heap", 5);
    trie.insert("hero", 6);
    trie.insert("heron", 7);

    std::cout << "Words inserted." << std::endl;

    trie.print();

    trie.insert("halibut", 8);
    trie.print();

    trie.insert("france", 9);
    trie.print();

    try {
        std::cout << "Value for 'helium': " << trie.search("helium") << std::endl;
        std::cout << "Value for 'halibut': " << trie.search("halibut") << std::endl;
        std::cout << "Value for 'germany': " << trie.search("germany") << std::endl;  // This will throw an exception
    } catch (const std::runtime_error& e) {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
