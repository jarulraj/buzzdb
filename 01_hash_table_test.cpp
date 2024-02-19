#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>

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
#include <mutex>
#include <shared_mutex>

class HashIndex2 {
private:
    struct HashEntry {
        int key;
        int value;
        int position; // Final position within the array
        bool exists; // Flag to check if entry exists

        // Default constructor
        HashEntry() : key(0), value(0), position(-1), exists(false) {}

        // Constructor for initializing with key, value, and exists flag
        HashEntry(int k, int v, int pos) : key(k), value(v), position(pos), exists(true) {}    
    };

    static const size_t capacity = 10000; // Hard-coded capacity
    HashEntry hashTable[capacity]; // Static-sized array
    // A vector of unique mutexes for fine-grained locking
    std::vector<std::unique_ptr<std::mutex>> mutexes;

    size_t hashFunction(int key) const {
        return key % capacity; // Simple modulo hash function
    }

public:
    HashIndex2() : mutexes(capacity) {
        // Initialize all entries as non-existing by default
        for (size_t i = 0; i < capacity; ++i) {
            hashTable[i] = HashEntry();
            mutexes[i] = std::make_unique<std::mutex>();
        }
    }

    void insertOrUpdate(int key, int value) {
        size_t index = hashFunction(key);
        size_t originalIndex = index;
        bool inserted = false;
        int i = 0; // Attempt counter

        do {
            // Lock only the specific slot's mutex
            std::lock_guard<std::mutex> lock(*mutexes[index]);

            if (!hashTable[index].exists) {
                hashTable[index] = HashEntry(key, value, true);
                hashTable[index].position = index;
                inserted = true;
                break;
            } else if (hashTable[index].key == key) {
                hashTable[index].value += value;
                hashTable[index].position = index;
                inserted = true;
                break;
            }
            i++;
            index = (originalIndex + i*i) % capacity; // Quadratic probing
        } while (index != originalIndex && !inserted);

        if (!inserted) {
            std::cerr << "HashTable is full or cannot insert key: " << key << std::endl;
        }
    }

   int getValue(int key) const {
        size_t index = hashFunction(key);
        size_t originalIndex = index;

        do {
            // Lock only the specific slot's mutex
            std::lock_guard<std::mutex> lock(*mutexes[index]);

            if (hashTable[index].exists && hashTable[index].key == key) {
                return hashTable[index].value;
            }
            if (!hashTable[index].exists) {
                break; // Stop if we find a slot that has never been used
            }
            index = (index + 1) % capacity;
        } while (index != originalIndex);

        return -1; // Key not found
    }

    void print() const {
        for (size_t i = 0; i < capacity; ++i) {
            // Lock only the specific slot's mutex
            std::lock_guard<std::mutex> lock(*mutexes[i]);

            if (hashTable[i].exists) {
                std::cout << "Position: " << hashTable[i].position << 
                ", Key: " << hashTable[i].key << 
                ", Value: " << hashTable[i].value << std::endl;
            }
        }
    }
};

class HashIndex3 {
private:
    struct HashEntry {
        int key;
        int value;
        int position; // Final position within the array
        bool exists; // Flag to check if entry exists

        // Default constructor
        HashEntry() : key(0), value(0), position(-1), exists(false) {}

        // Constructor for initializing with key, value, and exists flag
        HashEntry(int k, int v, int pos) : key(k), value(v), position(pos), exists(true) {}    
    };

    static const size_t capacity = 10000; // Hard-coded capacity
    HashEntry hashTable[capacity]; // Static-sized array
    mutable std::shared_mutex mutexes[capacity]; // Array of shared_mutex for fine-grained locking

    size_t hashFunction(int key) const {
        return key % capacity; // Simple modulo hash function
    }

public:
    HashIndex3() {
        // Initialize all entries as non-existing by default
        for (size_t i = 0; i < capacity; ++i) {
            hashTable[i] = HashEntry();
        }
    }

    void insertOrUpdate(int key, int value) {
        size_t index = hashFunction(key);
        size_t originalIndex = index;
        bool inserted = false;
        int i = 0; // Attempt counter

        do {
            // Exclusive lock for writing
            std::unique_lock<std::shared_mutex> lock(mutexes[index]);

            if (!hashTable[index].exists) {
                hashTable[index] = HashEntry(key, value, true);
                hashTable[index].position = index;
                inserted = true;
                break;
            } else if (hashTable[index].key == key) {
                hashTable[index].value += value;
                hashTable[index].position = index;
                inserted = true;
                break;
            }
            i++;
            index = (originalIndex + i*i) % capacity; // Quadratic probing
        } while (index != originalIndex && !inserted);

        if (!inserted) {
            std::cerr << "HashTable is full or cannot insert key: " << key << std::endl;
        }
    }

   int getValue(int key) const {
        size_t index = hashFunction(key);
        size_t originalIndex = index;

        do {
            // Shared lock for reading
            std::shared_lock<std::shared_mutex> lock(mutexes[index]);

            if (hashTable[index].exists && hashTable[index].key == key) {
                return hashTable[index].value;
            }
            if (!hashTable[index].exists) {
                break; // Stop if we find a slot that has never been used
            }
            index = (index + 1) % capacity;
        } while (index != originalIndex);

        return -1; // Key not found
    }

    void print() const {
        for (size_t i = 0; i < capacity; ++i) {
            // Shared lock for reading
            std::shared_lock<std::shared_mutex> lock(mutexes[i]);

            if (hashTable[i].exists) {
                std::cout << "Position: " << hashTable[i].position << 
                ", Key: " << hashTable[i].key << 
                ", Value: " << hashTable[i].value << std::endl;
            }
        }
    }
};


template<typename HashTableType>
void hashTableOperations(HashTableType& hashTable, int startKey, int endKey, int threadId) {
    for (int key = startKey; key <= endKey; ++key) {
        int value = key + 1000 * threadId;
        hashTable.insertOrUpdate(key, value);
    }
}

template<typename HashTableType>
void benchmarkHashTable(HashTableType& hashTable, const std::string& tableName){
    auto start = std::chrono::high_resolution_clock::now();

    // Assuming a larger range of keys, e.g., 1000 keys per thread
    const int keysPerThread = 1000;
    const int numThreads = 4; // Keeping the number of threads to 4 for this example

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        int startKey = i * keysPerThread + 1;
        int endKey = (i + 1) * keysPerThread;
        threads.emplace_back(hashTableOperations<HashTableType>, std::ref(hashTable), startKey, endKey, i + 1);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << tableName << ": Elapsed time: " << elapsed.count() << " seconds\n";
}

int main() {
    HashIndex2 hashTable2;
    HashIndex3 hashTable3;

    std::cout << "Starting stress test for HashIndex2.\n";
    benchmarkHashTable(hashTable2, "HashTable2");

    std::cout << "Starting stress test for HashIndex3.\n";
    benchmarkHashTable(hashTable3, "HashTable3");


    return 0;
}
