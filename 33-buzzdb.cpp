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

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    std::unique_ptr<char[]> data;
    size_t data_length;

public:
    Field(int i) : type(INT) { 
        data_length = sizeof(int);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &i, data_length);
    }

    Field(float f) : type(FLOAT) { 
        data_length = sizeof(float);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &f, data_length);
    }

    Field(const std::string& s) : type(STRING) {
        data_length = s.size() + 1;  // include null-terminator
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), s.c_str(), data_length);
    }

    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(Field&& other){
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    FieldType getType() const { return type; }
    int asInt() const { 
        return *reinterpret_cast<int*>(data.get());
    }
    float asFloat() const { 
        return *reinterpret_cast<float*>(data.get());
    }
    std::string asString() const { 
        return std::string(data.get());
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        if (type == STRING) {
            buffer << data.get() << ' ';
        } else if (type == INT) {
            buffer << *reinterpret_cast<int*>(data.get()) << ' ';
        } else if (type == FLOAT) {
            buffer << *reinterpret_cast<float*>(data.get()) << ' ';
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Field> deserialize(std::istream& in) {
        int type; in >> type;
        size_t length; in >> length;
        if (type == STRING) {
            std::string val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == INT) {
            int val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == FLOAT) {
            float val; in >> val;
            return std::make_unique<Field>(val);
        }
        return nullptr;
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t getSize() const {
        size_t size = 0;
        for (const auto& field : fields) {
            size += field->data_length;
        }
        return size;
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize();
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Tuple> deserialize(std::istream& in) {
        auto tuple = std::make_unique<Tuple>();
        size_t fieldCount; in >> fieldCount;
        for (size_t i = 0; i < fieldCount; ++i) {
            tuple->addField(Field::deserialize(in));
        }
        return tuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {

        // Serialize the tuple into a char array
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();

        //std::cout << "Tuple size: " << tuple_size << " bytes\n";
        assert(tuple_size == 38);

        // Check for first slot with enough space
        size_t slot_itr = 0;
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());        
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == true and 
                slot_array[slot_itr].length >= tuple_size) {
                break;
            }
        }
        if (slot_itr == MAX_SLOTS){
            //std::cout << "Page does not contain an empty slot with sufficient space to store the tuple.";
            return false;
        }

        // Identify the offset where the tuple will be placed in the page
        // Update slot meta-data if needed
        slot_array[slot_itr].empty = false;
        size_t offset = INVALID_VALUE;
        if (slot_array[slot_itr].offset == INVALID_VALUE){
            if(slot_itr != 0){
                auto prev_slot_offset = slot_array[slot_itr - 1].offset;
                auto prev_slot_length = slot_array[slot_itr - 1].length;
                offset = prev_slot_offset + prev_slot_length;
            }
            else{
                offset = metadata_size;
            }

            slot_array[slot_itr].offset = offset;
        }
        else{
            offset = slot_array[slot_itr].offset;
        }

        if(offset + tuple_size >= PAGE_SIZE){
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE){
            slot_array[slot_itr].length = tuple_size;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + offset, 
                    serializedTuple.c_str(), 
                    tuple_size);

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void print() const{
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == false){
                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_data.get() + slot_array[slot_itr].offset;
                std::istringstream iss(tuple_data);
                auto loadedTuple = Tuple::deserialize(iss);
                std::cout << "Slot " << slot_itr << " : [";
                std::cout << (uint16_t)(slot_array[slot_itr].offset) << "] :: ";
                loadedTuple->print();
            }
        }
        std::cout << "\n";
    }
};

const std::string database_filename = "buzzdb.dat";

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;

public:
    StorageManager(){
        fileStream.open(database_filename, std::ios::in | std::ios::out);
        if (!fileStream) {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, std::ios::out);
        }
        fileStream.close(); 
        fileStream.open(database_filename, std::ios::in | std::ios::out); 

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";        
        if(num_pages == 0){
            extend();
        }

    }

    ~StorageManager() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    // Read a page from disk
    std::unique_ptr<SlottedPage> load(uint16_t page_id) {
        fileStream.seekg(page_id * PAGE_SIZE, std::ios::beg);
        auto page = std::make_unique<SlottedPage>();
        // Read the content of the file into the page
        if(fileStream.read(page->page_data.get(), PAGE_SIZE)){
            //std::cout << "Page read successfully from file." << std::endl;
        }
        else{
            std::cerr << "Error: Unable to read data from the file. \n";
            exit(-1);
        }
        return page;
    }

    // Write a page to disk
    void flush(uint16_t page_id, const std::unique_ptr<SlottedPage>& page) {
        size_t page_offset = page_id * PAGE_SIZE;        

        // Move the write pointer
        fileStream.seekp(page_offset, std::ios::beg);
        fileStream.write(page->page_data.get(), PAGE_SIZE);        
        fileStream.flush();
    }

    // Extend database file by one page
    void extend() {
        std::cout << "Extending database file \n";

        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();

        // Update number of pages
        num_pages += 1;
    }

};

using PageID = uint16_t;

class Policy {
public:
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

void printList(std::string list_name, const std::list<PageID>& myList) {
        std::cout << list_name << " :: ";
        for (const PageID& value : myList) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
}

class LruPolicy : public Policy {
private:
    // List to keep track of the order of use
    std::list<PageID> lruList;

    // Map to find a page's iterator in the list efficiently
    std::unordered_map<PageID, std::list<PageID>::iterator> map;

    size_t cacheSize;

public:

    LruPolicy(size_t cacheSize) : cacheSize(cacheSize) {}

    bool touch(PageID page_id) override {
        //printList("LRU", lruList);

        bool found = false;
        // If page already in the list, remove it
        if (map.find(page_id) != map.end()) {
            found = true;
            lruList.erase(map[page_id]);
            map.erase(page_id);            
        }

        // If cache is full, evict
        if(lruList.size() == cacheSize){
            evict();
        }

        if(lruList.size() < cacheSize){
            // Add the page to the front of the list
            lruList.emplace_front(page_id);
            map[page_id] = lruList.begin();
        }

        return found;
    }

    PageID evict() override {
        // Evict the least recently used page
        PageID evictedPageId = INVALID_VALUE;
        if(lruList.size() != 0){
            evictedPageId = lruList.back();
            map.erase(evictedPageId);
            lruList.pop_back();
        }
        return evictedPageId;
    }

};

constexpr size_t MAX_PAGES_IN_MEMORY = 10;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;

public:
    BufferManager(): 
    policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {}

    std::unique_ptr<SlottedPage>& getPage(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            auto evictedPageId = policy->evict();
            if(evictedPageId != INVALID_VALUE){
                std::cout << "Evicting page " << evictedPageId << "\n";
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
            }
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(page);
        return pageMap[page_id];
    }

    void flushPage(int page_id) {
        //std::cout << "Flush page " << page_id << "\n";
        storage_manager.flush(page_id, pageMap[page_id]);
    }

    void extend(){
        storage_manager.extend();
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

};

class HashIndex {
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

    static const size_t capacity = 100; // Hard-coded capacity
    HashEntry hashTable[capacity]; // Static-sized array

    size_t hashFunction(int key) const {
        return key % capacity; // Simple modulo hash function
    }

public:
    HashIndex() {
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

    // This method is not efficient for range queries 
    // as this is an unordered index
    // but is included for comparison
    std::vector<int> rangeQuery(int lowerBound, int upperBound) const {
        std::vector<int> values;
        for (size_t i = 0; i < capacity; ++i) {
            if (hashTable[i].exists && hashTable[i].key >= lowerBound && hashTable[i].key <= upperBound) {
                std::cout << "Key: " << hashTable[i].key << 
                ", Value: " << hashTable[i].value << std::endl;
                values.push_back(hashTable[i].value);
            }
        }
        return values;
    }

    void print() const {
        for (size_t i = 0; i < capacity; ++i) {
            if (hashTable[i].exists) {
                std::cout << "Position: " << hashTable[i].position << 
                ", Key: " << hashTable[i].key << 
                ", Value: " << hashTable[i].value << std::endl;
            }
        }
    }
};

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


class BuzzDB {
public:
    HashIndex hash_index;
    OrderedIndex ordered_index;

    BufferManager buffer_manager;

public:
    size_t max_number_of_tuples = 5000;
    size_t tuple_insertion_attempt_counter = 0;

    BuzzDB(){
        // Storage Manager automatically created
    }

    bool try_to_insert(int key, int value){
        bool status = false;
        auto num_pages = buffer_manager.getNumPages();
        for (size_t page_itr = 0; page_itr < num_pages; page_itr++) {

            auto newTuple = std::make_unique<Tuple>();

            auto key_field = std::make_unique<Field>(key);
            auto value_field = std::make_unique<Field>(value);
            float float_val = 132.04;
            auto float_field = std::make_unique<Field>(float_val);
            auto string_field = std::make_unique<Field>("buzzdb");

            newTuple->addField(std::move(key_field));
            newTuple->addField(std::move(value_field));
            newTuple->addField(std::move(float_field));
            newTuple->addField(std::move(string_field));

            auto& page = buffer_manager.getPage(page_itr);

            status = page->addTuple(std::move(newTuple));
            if (status == true){
                //std::cout << "Inserted into page: " << page_itr << "\n";
                buffer_manager.flushPage(page_itr);
                break;
            }
        }

        return status;
    }

    // insert function
    void insert(int key, int value) {
        tuple_insertion_attempt_counter += 1;

        if(tuple_insertion_attempt_counter >= max_number_of_tuples){
            return;
        }

        bool status = try_to_insert(key, value);

        // Try again after extending the database file
        if(status == false){
            buffer_manager.extend();
            bool status2 = try_to_insert(key, value);
            assert(status2 == true);
        }

        //newTuple->print();

        // Skip deleting tuples only once every hundred tuples
        if (tuple_insertion_attempt_counter % 100 != 0){
            auto& page = buffer_manager.getPage(0);
            page->deleteTuple(0);
            buffer_manager.flushPage(0);
        }
    }

    void scanTableToBuildIndex(){

        std::cout << "Scanning table to build index \n";

        auto num_pages = buffer_manager.getNumPages();

        for (size_t page_itr = 0; page_itr < num_pages; page_itr++) {
            auto& page = buffer_manager.getPage(page_itr);
            char* page_buffer = page->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);
            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_array[slot_itr].empty == false){
                    assert(slot_array[slot_itr].offset != INVALID_VALUE);
                    const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                    std::istringstream iss(tuple_data);
                    auto loadedTuple = Tuple::deserialize(iss);
                    int key = loadedTuple->fields[0]->asInt();
                    int value = loadedTuple->fields[1]->asInt();

                    // Build indexes
                    hash_index.insertOrUpdate(key, value);

                    ordered_index.insertOrUpdate(key, value);
                    //ordered_index.print();
                }
            }
        }

    }

    // perform a SELECT ... GROUP BY ... SUM query
    void selectGroupBySum() {
       hash_index.print();
    }

    // perform a SELECT ... query with a range
    void performRangeQueryWithHashIndex(int lowerBound, int upperBound) {
        auto start = std::chrono::high_resolution_clock::now();
        auto results = hash_index.rangeQuery(lowerBound, upperBound);
        std::cout << "Results: " << results.size() << "\n";
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "HashIndex Range Query Elapsed time: " << elapsed.count() << " ms\n";
    }

    void performRangeQueryWithOrderedIndex(int lowerBound, int upperBound) {
        auto start = std::chrono::high_resolution_clock::now();
        auto results = ordered_index.rangeQuery(lowerBound, upperBound);
        std::cout << "Results: " << results.size() << "\n";
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        std::cout << "OrderedIndex Range Query Elapsed time: " << elapsed.count() << " ms\n";
    }

};

int main() {
    // Get the start time
    auto start = std::chrono::high_resolution_clock::now();

    BuzzDB db;

    std::ifstream inputFile("output.txt");

    if (!inputFile) {
        std::cerr << "Unable to open file" << std::endl;
        return 1;
    }

    int field1, field2;
    while (inputFile >> field1 >> field2) {
        db.insert(field1, field2);
    }

    db.scanTableToBuildIndex();
    
    db.selectGroupBySum();

    int lowerBound = 2;
    int upperBound = 7;
    db.performRangeQueryWithHashIndex(lowerBound, upperBound);

    db.performRangeQueryWithOrderedIndex(lowerBound, upperBound);

    std::cout << "Num Pages: " << db.buffer_manager.getNumPages() << "\n";

    // Get the end time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

    db.ordered_index.print();

    return 0;
}