#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>

// Function to split a string by whitespace
std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Convert a string to lowercase
std::string toLower(const std::string &str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

// Inverted Index class
class InvertedIndex {
private:
    // Map from word to a map of document ID to positions in the document
    std::unordered_map<std::string, std::unordered_map<int, std::vector<int>>> index;
    std::vector<std::string> documents;

public:
    // Add document to the index
    void addDocument(int docID, const std::string &content) {
        documents.push_back(content);
        std::vector<std::string> words = split(toLower(content));
        for (size_t i = 0; i < words.size(); ++i) {
            index[words[i]][docID].push_back(i);
        }
    }

    // Get the list of document IDs containing the given word
    std::unordered_set<int> getDocuments(const std::string &word) {
        std::string lowerWord = toLower(word);
        if (index.find(lowerWord) != index.end()) {
            std::unordered_set<int> docIDs;
            for (const auto &entry : index[lowerWord]) {
                docIDs.insert(entry.first);
            }
            return docIDs;
        } else {
            return {};
        }
    }

    // Proximity search: find documents where word1 is within k words of word2
    std::unordered_set<int> proximitySearch(const std::string &word1, const std::string &word2, int k) {
        std::unordered_set<int> result;
        std::string lowerWord1 = toLower(word1);
        std::string lowerWord2 = toLower(word2);

        if (index.find(lowerWord1) != index.end() && index.find(lowerWord2) != index.end()) {
            for (const auto &entry1 : index[lowerWord1]) {
                int docID = entry1.first;
                
                if (index[lowerWord2].find(docID) != index[lowerWord2].end()) {
                    const auto &positions1 = entry1.second;
                    const auto &positions2 = index[lowerWord2].at(docID);

                    // Print the positions for debugging
                    std::cout << "Doc ID: " << docID << "\n";
                    std::cout << "Positions of \"" << word1 << "\": ";
                    for (int pos : positions1) {
                        std::cout << pos << " ";
                    }
                    std::cout << "\nPositions of \"" << word2 << "\": ";
                    for (int pos : positions2) {
                        std::cout << pos << " ";
                    }
                    std::cout << std::endl;

                    for (int pos1 : positions1) {
                        for (int pos2 : positions2) {
                            if (std::abs(pos1 - pos2) <= k) {
                                result.insert(docID);
                                break;  // No need to check further once a match is found
                            }
                        }
                        if (result.find(docID) != result.end()) {
                            break;  // Stop checking other positions if a match is already found
                        }
                    }
                }
            }
        }

        return result;
    }



    // Print the inverted index (for debugging purposes)
    void printIndex() {
        for (const auto &entry : index) {
            std::cout << entry.first << ": ";
            for (const auto &docEntry : entry.second) {
                std::cout << "[" << docEntry.first << ": ";
                for (const auto &pos : docEntry.second) {
                    std::cout << pos << " ";
                }
                std::cout << "] ";
            }
            std::cout << std::endl;
        }
    }

    // Get the document content by its ID
    std::string getDocumentByID(size_t docID) {
        if (docID >= 0 && docID < documents.size()) {
            return documents[docID];
        } else {
            return "";
        }
    }
};

int main() {
    InvertedIndex index;
    std::string line;
    int docID = 0;

    // Read documents from a file
    std::ifstream file("war_and_peace.txt");
    if (file.is_open()) {
        while (std::getline(file, line)) {
            index.addDocument(docID++, line);
        }
        file.close();
    } else {
        std::cerr << "Unable to open file" << std::endl;
        return 1;
    }

    // Example query
    std::string query = "Russia";
    std::unordered_set<int> results = index.getDocuments(query);
    std::cout << "Documents containing \"" << query << "\":" << std::endl;
    for (const auto &lineID : results) {
        std::cout << "Line ID: " << lineID << " Content: " << index.getDocumentByID(lineID) << std::endl;
    }

    // Example proximity query
    std::string word1 = "spoke";
    std::string word2 = "with";
    int k = 10;
    std::unordered_set<int> proximityResults = index.proximitySearch(word1, word2, k);
    std::cout << "Documents where \"" << word1 << "\" is within " << k << " words of \"" << word2 << "\":" << std::endl;
    for (const auto &docID : proximityResults) {
        std::cout << "Line ID: " << docID << " Content: " << index.getDocumentByID(docID) << std::endl;
    }

    // Print the inverted index (for debugging purposes)
    //index.printIndex();

    return 0;
}
