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
#include <regex>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <cctype>

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    size_t data_length;
    std::unique_ptr<char[]> data;

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

   // Copy constructor
    Field(const Field& other) : type(other.type), data_length(other.data_length), data(new char[data_length]) {
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    // Move constructor - If you already have one, ensure it's correctly implemented
    Field(Field&& other) noexcept : type(other.type), data_length(other.data_length), data(std::move(other.data)) {
        // Optionally reset other's state if needed
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

    // Clone method
    std::unique_ptr<Field> clone() const {
        // Use the copy constructor
        return std::make_unique<Field>(*this);
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

bool operator==(const Field& lhs, const Field& rhs) {
    if (lhs.type != rhs.type) return false; // Different types are never equal

    switch (lhs.type) {
        case INT:
            return *reinterpret_cast<const int*>(lhs.data.get()) == *reinterpret_cast<const int*>(rhs.data.get());
        case FLOAT:
            return *reinterpret_cast<const float*>(lhs.data.get()) == *reinterpret_cast<const float*>(rhs.data.get());
        case STRING:
            return std::string(lhs.data.get(), lhs.data_length - 1) == std::string(rhs.data.get(), rhs.data_length - 1);
        default:
            throw std::runtime_error("Unsupported field type for comparison.");
    }
}

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

    // Clone method
    std::unique_ptr<Tuple> clone() const {
        auto clonedTuple = std::make_unique<Tuple>();
        for (const auto& field : fields) {
            clonedTuple->addField(field->clone());
        }
        return clonedTuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

using TableId = uint16_t;

struct ColumnSchema {
    std::string name;
    FieldType type;
};

struct TableSchema {
    std::vector<ColumnSchema> columns;
};

using PageID = uint16_t;

struct TableMetadata {
    TableId table_id;
    std::string name;
    TableSchema schema;
    std::vector<PageID> page_ids;
};

class Catalog {
private:
    TableId next_table_id = 1;
    std::unordered_map<std::string, TableMetadata> tables_by_name;
    std::unordered_map<TableId, std::string> table_names_by_id;

public:
    TableId createTable(const std::string& name, TableSchema schema) {
        if (tables_by_name.find(name) != tables_by_name.end()) {
            throw std::runtime_error("Table already exists: " + name);
        }

        TableMetadata metadata{next_table_id++, name, std::move(schema), {}};
        TableId table_id = metadata.table_id;

        tables_by_name.emplace(name, std::move(metadata));
        table_names_by_id.emplace(table_id, name);

        return table_id;
    }

    TableMetadata& getTable(const std::string& name) {
        auto it = tables_by_name.find(name);
        if (it == tables_by_name.end()) {
            throw std::runtime_error("Unknown table: " + name);
        }
        return it->second;
    }

    TableMetadata& getTable(TableId table_id) {
        auto it = table_names_by_id.find(table_id);
        if (it == table_names_by_id.end()) {
            throw std::runtime_error("Unknown table id.");
        }
        return getTable(it->second);
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

    PageID allocatePage() {
        storage_manager.extend();
        return static_cast<PageID>(storage_manager.num_pages - 1);
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

};

class TableHeap {
private:
    TableMetadata& metadata;
    BufferManager& buffer_manager;

public:
    TableHeap(TableMetadata& metadata, BufferManager& buffer_manager)
        : metadata(metadata), buffer_manager(buffer_manager) {}

    bool insertTuple(std::unique_ptr<Tuple> tuple) {
        for (PageID page_id : metadata.page_ids) {
            auto& page = buffer_manager.getPage(page_id);
            if (page->addTuple(tuple->clone())) {
                buffer_manager.flushPage(page_id);
                return true;
            }
        }

        PageID page_id = buffer_manager.allocatePage();
        metadata.page_ids.push_back(page_id);

        auto& page = buffer_manager.getPage(page_id);
        if (page->addTuple(std::move(tuple))) {
            buffer_manager.flushPage(page_id);
            return true;
        }

        return false;
    }

    std::unique_ptr<SlottedPage>& getPage(PageID page_id) {
        return buffer_manager.getPage(page_id);
    }

    const std::vector<PageID>& getPageIds() const {
        return metadata.page_ids;
    }

    const TableMetadata& getMetadata() const {
        return metadata;
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

// -----------------------------------------------------------------------------
// Transaction and Operator Context 
// -----------------------------------------------------------------------------

struct TxnContext {
    int id;
    enum State { RUNNING, COMMITTED, ABORTED } state = RUNNING;
};

using TxnPtr = std::shared_ptr<TxnContext>;

class TransactionManager {
    int next_id = 1;
public:
    TxnPtr begin() { 
        std::cout << "Begin Txn ID = " << next_id << std::endl;
        return std::make_shared<TxnContext>(TxnContext{ next_id++ }); 
    }
    void commit(TxnContext& tx) { tx.state = TxnContext::COMMITTED; }
    void abort (TxnContext& tx) { tx.state = TxnContext::ABORTED;  }
};

class Operator {
    public:
    virtual ~Operator() = default;

    /// Initializes the operator.
    virtual void open() = 0;

    /// Tries to generate the next tuple. Return true when a new tuple is
    /// available.
    virtual bool next() = 0;

    /// Destroys the operator.
    virtual void close() = 0;

    /// This returns the pointers to the Fields of the generated tuple. When
    /// `next()` returns true, the Fields will contain the values for the
    /// next tuple. Each `Field` pointer in the vector stands for one attribute of the tuple.
    virtual std::vector<std::unique_ptr<Field>> getOutput() = 0;

    // children can forward txn context to their child operators
    virtual void setTxnContext(std::shared_ptr<TxnContext> txn) {
        txn_ = std::move(txn);
    }

    protected:
    TxnPtr txn_;
};

class UnaryOperator : public Operator {
    protected:
    Operator* input;

    public:
    explicit UnaryOperator(Operator& input) : input(&input) {}

    ~UnaryOperator() override = default;
};

class BinaryOperator : public Operator {
    protected:
    Operator* input_left;
    Operator* input_right;

    public:
    explicit BinaryOperator(Operator& input_left, Operator& input_right)
        : input_left(&input_left), input_right(&input_right) {}

    ~BinaryOperator() override = default;
};

class ScanOperator : public Operator {
private:
    TableHeap& tableHeap;
    size_t currentPageIndex = 0;
    size_t currentSlotIndex = 0;
    std::unique_ptr<Tuple> currentTuple;
    size_t tuple_count = 0;

public:
    ScanOperator(TableHeap& table) : tableHeap(table) {}

    void open() override {
        currentPageIndex = 0;
        currentSlotIndex = 0;
        tuple_count = 0;
        currentTuple.reset();
    }

    bool next() override {
        loadNextTuple();
        return currentTuple != nullptr;
    }

    void close() override {
        std::cout << "Scan Operator tuple_count: " << tuple_count << "\n";
        currentPageIndex = 0;
        currentSlotIndex = 0;
        currentTuple.reset();
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (currentTuple) {
            return std::move(currentTuple->fields);
        }
        return {};
    }

private:
    void loadNextTuple() {
        const auto& page_ids = tableHeap.getPageIds();

        while (currentPageIndex < page_ids.size()) {
            auto& currentPage = tableHeap.getPage(page_ids[currentPageIndex]);

            char* page_buffer = currentPage->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);

            while (currentSlotIndex < MAX_SLOTS) {
                if (!slot_array[currentSlotIndex].empty) {
                    assert(slot_array[currentSlotIndex].offset != INVALID_VALUE);

                    const char* tuple_data =
                        page_buffer + slot_array[currentSlotIndex].offset;

                    std::istringstream iss(
                        std::string(tuple_data, slot_array[currentSlotIndex].length)
                    );

                    currentTuple = Tuple::deserialize(iss);
                    currentSlotIndex++;
                    tuple_count++;
                    return;
                }

                currentSlotIndex++;
            }

            currentPageIndex++;
            currentSlotIndex = 0;
        }

        currentTuple.reset();
    }
};

class IPredicate {
public:
    virtual ~IPredicate() = default;
    virtual bool check(const std::vector<std::unique_ptr<Field>>& tupleFields) const = 0;
};

void printTuple(const std::vector<std::unique_ptr<Field>>& tupleFields) {
    std::cout << "Tuple: [";
    for (const auto& field : tupleFields) {
        field->print(); // Assuming `print()` is a method that prints field content
        std::cout << " ";
    }
    std::cout << "]";
}

class SimplePredicate: public IPredicate {
public:
    enum OperandType { DIRECT, INDIRECT };
    enum ComparisonOperator { EQ, NE, GT, GE, LT, LE }; // Renamed from PredicateType

    struct Operand {
        std::unique_ptr<Field> directValue;
        size_t index;
        OperandType type;

        Operand(std::unique_ptr<Field> value) : directValue(std::move(value)), type(DIRECT) {}
        Operand(size_t idx) : index(idx), type(INDIRECT) {}
    };

    Operand left_operand;
    Operand right_operand;
    ComparisonOperator comparison_operator;

    SimplePredicate(Operand left, Operand right, ComparisonOperator op)
        : left_operand(std::move(left)), right_operand(std::move(right)), comparison_operator(op) {}

    bool check(const std::vector<std::unique_ptr<Field>>& tupleFields) const {
        const Field* leftField = nullptr;
        const Field* rightField = nullptr;

        if (left_operand.type == DIRECT) {
            leftField = left_operand.directValue.get();
        } else if (left_operand.type == INDIRECT) {
            leftField = tupleFields[left_operand.index].get();
        }

        if (right_operand.type == DIRECT) {
            rightField = right_operand.directValue.get();
        } else if (right_operand.type == INDIRECT) {
            rightField = tupleFields[right_operand.index].get();
        }

        if (leftField == nullptr || rightField == nullptr) {
            std::cerr << "Error: Invalid field reference.\n";
            return false;
        }

        if (leftField->getType() != rightField->getType()) {
            std::cerr << "Error: Comparing fields of different types.\n";
            return false;
        }

        // Perform comparison based on field type
        switch (leftField->getType()) {
            case FieldType::INT: {
                int left_val = leftField->asInt();
                int right_val = rightField->asInt();
                return compare(left_val, right_val);
            }
            case FieldType::FLOAT: {
                float left_val = leftField->asFloat();
                float right_val = rightField->asFloat();
                return compare(left_val, right_val);
            }
            case FieldType::STRING: {
                std::string left_val = leftField->asString();
                std::string right_val = rightField->asString();
                return compare(left_val, right_val);
            }
            default:
                std::cerr << "Invalid field type\n";
                return false;
        }
    }


private:

    // Compares two values of the same type
    template<typename T>
    bool compare(const T& left_val, const T& right_val) const {
        switch (comparison_operator) {
            case ComparisonOperator::EQ: return left_val == right_val;
            case ComparisonOperator::NE: return left_val != right_val;
            case ComparisonOperator::GT: return left_val > right_val;
            case ComparisonOperator::GE: return left_val >= right_val;
            case ComparisonOperator::LT: return left_val < right_val;
            case ComparisonOperator::LE: return left_val <= right_val;
            default: std::cerr << "Invalid predicate type\n"; return false;
        }
    }
};

class ComplexPredicate : public IPredicate {
public:
    enum LogicOperator { AND, OR };

private:
    std::vector<std::unique_ptr<IPredicate>> predicates;
    LogicOperator logic_operator;

public:
    ComplexPredicate(LogicOperator op) : logic_operator(op) {}

    void addPredicate(std::unique_ptr<IPredicate> predicate) {
        predicates.push_back(std::move(predicate));
    }

    bool check(const std::vector<std::unique_ptr<Field>>& tupleFields) const {
        
        if (logic_operator == AND) {
            for (const auto& pred : predicates) {
                if (!pred->check(tupleFields)) {
                    return false; // If any predicate fails, the AND condition fails
                }
            }
            return true; // All predicates passed
        } else if (logic_operator == OR) {
            for (const auto& pred : predicates) {
                if (pred->check(tupleFields)) {
                    return true; // If any predicate passes, the OR condition passes
                }
            }
            return false; // No predicates passed
        }
        return false;
    }


};


class SelectOperator : public UnaryOperator {
private:
    std::unique_ptr<IPredicate> predicate;
    bool has_next;
    std::vector<std::unique_ptr<Field>> currentOutput; // Store the current output here

public:
    SelectOperator(Operator& input, std::unique_ptr<IPredicate> predicate)
        : UnaryOperator(input), predicate(std::move(predicate)), has_next(false) {}

    void open() override {
        input->setTxnContext(txn_);
        input->open();
        has_next = false;
        currentOutput.clear(); // Ensure currentOutput is cleared at the beginning
    }

    bool next() override {
        while (input->next()) {
            const auto& output = input->getOutput(); // Temporarily hold the output
            if (predicate->check(output)) {
                // If the predicate is satisfied, store the output in the member variable
                currentOutput.clear(); // Clear previous output
                for (const auto& field : output) {
                    // Assuming Field class has a clone method or copy constructor to duplicate fields
                    currentOutput.push_back(field->clone());
                }
                has_next = true;
                return true;
            }
        }
        has_next = false;
        currentOutput.clear(); // Clear output if no more tuples satisfy the predicate
        return false;
    }

    void close() override {
        input->close();
        currentOutput.clear(); // Ensure currentOutput is cleared at the end
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (has_next) {
            // Since currentOutput already holds the desired output, simply return it
            // Need to create a deep copy to return since we're returning by value
            std::vector<std::unique_ptr<Field>> outputCopy;
            for (const auto& field : currentOutput) {
                outputCopy.push_back(field->clone()); // Clone each field
            }
            return outputCopy;
        } else {
            return {}; // Return an empty vector if no matching tuple is found
        }
    }
};

enum class AggrFuncType { COUNT, MAX, MIN, SUM };

struct AggrFunc {
    AggrFuncType func;
    size_t attr_index; // Index of the attribute to aggregate
};

class HashAggregationOperator : public UnaryOperator {
private:
    std::vector<size_t> group_by_attrs;
    std::vector<AggrFunc> aggr_funcs;
    std::vector<Tuple> output_tuples; // Use your Tuple class for output
    size_t output_tuples_index = 0;

    struct FieldVectorHasher {
        std::size_t operator()(const std::vector<Field>& fields) const {
            std::size_t hash = 0;
            for (const auto& field : fields) {
                std::hash<std::string> hasher;
                std::size_t fieldHash = 0;

                // Depending on the type, hash the corresponding data
                switch (field.type) {
                    case INT: {
                        // Convert integer data to string and hash
                        int value = *reinterpret_cast<const int*>(field.data.get());
                        fieldHash = hasher(std::to_string(value));
                        break;
                    }
                    case FLOAT: {
                        // Convert float data to string and hash
                        float value = *reinterpret_cast<const float*>(field.data.get());
                        fieldHash = hasher(std::to_string(value));
                        break;
                    }
                    case STRING: {
                        // Directly hash the string data
                        std::string value(field.data.get(), field.data_length - 1); // Exclude null-terminator
                        fieldHash = hasher(value);
                        break;
                    }
                    default:
                        throw std::runtime_error("Unsupported field type for hashing.");
                }

                // Combine the hash of the current field with the hash so far
                hash ^= fieldHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };


public:
    HashAggregationOperator(Operator& input, std::vector<size_t> group_by_attrs, std::vector<AggrFunc> aggr_funcs)
        : UnaryOperator(input), group_by_attrs(group_by_attrs), aggr_funcs(aggr_funcs) {}

    void open() override {
        input->open(); // Ensure the input operator is opened
        output_tuples_index = 0;
        output_tuples.clear();

        // Assume a hash map to aggregate tuples based on group_by_attrs
        std::unordered_map<std::vector<Field>, std::vector<Field>, FieldVectorHasher> hash_table;

        while (input->next()) {
            const auto& tuple = input->getOutput(); // Assume getOutput returns a reference to the current tuple

            // Extract group keys and initialize aggregation values
            std::vector<Field> group_keys;
            for (auto& index : group_by_attrs) {
                group_keys.push_back(*tuple[index]); // Deep copy the Field object for group key
            }

            // Process aggregation functions
            if (!hash_table.count(group_keys)) {
                // Initialize aggregate values for a new group
                std::vector<Field> aggr_values(aggr_funcs.size(), Field(0)); // Assuming Field(int) initializes an integer Field
                hash_table[group_keys] = aggr_values;
            }

            // Update aggregate values
            auto& aggr_values = hash_table[group_keys];
            for (size_t i = 0; i < aggr_funcs.size(); ++i) {
                // Simplified update logic for demonstration
                // You'll need to implement actual aggregation logic here
                aggr_values[i] = updateAggregate(aggr_funcs[i], aggr_values[i], *tuple[aggr_funcs[i].attr_index]);
            }
        }

        // Prepare output tuples from the hash table
        for (const auto& entry : hash_table) {
            const auto& group_keys = entry.first;
            const auto& aggr_values = entry.second;
            Tuple output_tuple;
            // Assuming Tuple has a method to add Fields
            for (const auto& key : group_keys) {
                output_tuple.addField(std::make_unique<Field>(key)); // Add group keys to the tuple
            }
            for (const auto& value : aggr_values) {
                output_tuple.addField(std::make_unique<Field>(value)); // Add aggregated values to the tuple
            }
            output_tuples.push_back(std::move(output_tuple));
        }
    }

    bool next() override {
        if (output_tuples_index < output_tuples.size()) {
            output_tuples_index++;
            return true;
        }
        return false;
    }

    void close() override {
        input->close();
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        std::vector<std::unique_ptr<Field>> outputCopy;

        if (output_tuples_index == 0 || output_tuples_index > output_tuples.size()) {
            // If there is no current tuple because next() hasn't been called yet or we're past the last tuple,
            // return an empty vector.
            return outputCopy; // This will be an empty vector
        }

        // Assuming that output_tuples stores Tuple objects and each Tuple has a vector of Field objects or similar
        const auto& currentTuple = output_tuples[output_tuples_index - 1]; // Adjust for 0-based indexing after increment in next()

        // Assuming the Tuple class provides a way to access its fields, e.g., a method or a public member
        for (const auto& field : currentTuple.fields) {
            outputCopy.push_back(field->clone()); // Use the clone method to create a deep copy of each field
        }

        return outputCopy;
    }


private:

    Field updateAggregate(const AggrFunc& aggrFunc, const Field& currentAggr, const Field& newValue) {
        if (currentAggr.getType() != newValue.getType()) {
            throw std::runtime_error("Mismatched Field types in aggregation.");
        }

        switch (aggrFunc.func) {
            case AggrFuncType::COUNT: {
                if (currentAggr.getType() == FieldType::INT) {
                    // For COUNT, simply increment the integer value
                    int count = currentAggr.asInt() + 1;
                    return Field(count);
                }
                break;
            }
            case AggrFuncType::SUM: {
                if (currentAggr.getType() == FieldType::INT) {
                    int sum = currentAggr.asInt() + newValue.asInt();
                    return Field(sum);
                } else if (currentAggr.getType() == FieldType::FLOAT) {
                    float sum = currentAggr.asFloat() + newValue.asFloat();
                    return Field(sum);
                }
                break;
            }
            case AggrFuncType::MAX: {
                if (currentAggr.getType() == FieldType::INT) {
                    int max = std::max(currentAggr.asInt(), newValue.asInt());
                    return Field(max);
                } else if (currentAggr.getType() == FieldType::FLOAT) {
                    float max = std::max(currentAggr.asFloat(), newValue.asFloat());
                    return Field(max);
                }
                break;
            }
            case AggrFuncType::MIN: {
                if (currentAggr.getType() == FieldType::INT) {
                    int min = std::min(currentAggr.asInt(), newValue.asInt());
                    return Field(min);
                } else if (currentAggr.getType() == FieldType::FLOAT) {
                    float min = std::min(currentAggr.asFloat(), newValue.asFloat());
                    return Field(min);
                }
                break;
            }
            default:
                throw std::runtime_error("Unsupported aggregation function.");
        }

        // Default case for unsupported operations or types
        throw std::runtime_error(
            "Invalid operation or unsupported Field type.");
    }

};

struct QueryComponents {
    std::vector<int> selectAttributes;
    bool sumOperation = false;
    int sumAttributeIndex = -1;
    bool groupBy = false;
    int groupByAttributeIndex = -1;
    bool whereCondition = false;
    int whereAttributeIndex = -1;
    int lowerBound = std::numeric_limits<int>::min();
    int upperBound = std::numeric_limits<int>::max();
};

QueryComponents parseQuery(const std::string& query) {
    QueryComponents components;

    // Parse selected attributes
    std::regex selectRegex("\\{(\\d+)\\}(, \\{(\\d+)\\})?");
    std::smatch selectMatches;
    std::string::const_iterator queryStart(query.cbegin());
    while (std::regex_search(queryStart, query.cend(), selectMatches, selectRegex)) {
        for (size_t i = 1; i < selectMatches.size(); i += 2) {
            if (!selectMatches[i].str().empty()) {
                components.selectAttributes.push_back(std::stoi(selectMatches[i]) - 1);
            }
        }
        queryStart = selectMatches.suffix().first;
    }

    // Check for SUM operation
    std::regex sumRegex("SUM\\{(\\d+)\\}");
    std::smatch sumMatches;
    if (std::regex_search(query, sumMatches, sumRegex)) {
        components.sumOperation = true;
        components.sumAttributeIndex = std::stoi(sumMatches[1]) - 1;
    }

    // Check for GROUP BY clause
    std::regex groupByRegex("GROUP BY \\{(\\d+)\\}");
    std::smatch groupByMatches;
    if (std::regex_search(query, groupByMatches, groupByRegex)) {
        components.groupBy = true;
        components.groupByAttributeIndex = std::stoi(groupByMatches[1]) - 1;
    }

    // Extract WHERE conditions more accurately
    std::regex whereRegex("\\{(\\d+)\\} > (\\d+) and \\{(\\d+)\\} < (\\d+)");
    std::smatch whereMatches;
    if (std::regex_search(query, whereMatches, whereRegex)) {
        components.whereCondition = true;
        // Correctly identify the attribute index for the WHERE condition
        components.whereAttributeIndex = std::stoi(whereMatches[1]) - 1;
        components.lowerBound = std::stoi(whereMatches[2]);
        // Ensure the same attribute is used for both conditions
        if (std::stoi(whereMatches[3]) - 1 == components.whereAttributeIndex) {
            components.upperBound = std::stoi(whereMatches[4]);
        } else {
            std::cerr << "Error: WHERE clause conditions apply to different attributes." << std::endl;
            // Handle error or set components.whereCondition = false;
        }
    }

    return components;
}

void prettyPrint(const QueryComponents& components) {
    std::cout << "Query Components:\n";
    std::cout << "  Selected Attributes: ";
    for (auto attr : components.selectAttributes) {
        std::cout << "{" << attr + 1 << "} "; // Convert back to 1-based indexing for display
    }
    std::cout << "\n  SUM Operation: " << (components.sumOperation ? "Yes" : "No");
    if (components.sumOperation) {
        std::cout << " on {" << components.sumAttributeIndex + 1 << "}";
    }
    std::cout << "\n  GROUP BY: " << (components.groupBy ? "Yes" : "No");
    if (components.groupBy) {
        std::cout << " on {" << components.groupByAttributeIndex + 1 << "}";
    }
    std::cout << "\n  WHERE Condition: " << (components.whereCondition ? "Yes" : "No");
    if (components.whereCondition) {
        std::cout << " on {" << components.whereAttributeIndex + 1 << "} > " << components.lowerBound << " and < " << components.upperBound;
    }
    std::cout << std::endl;
}

void executeQuery(const QueryComponents& components,
                  TableHeap& table_heap,
                  const TxnPtr& txn = nullptr){

    // Stack allocation of ScanOperator
    ScanOperator scanOp(table_heap);

    // Using a pointer to Operator to handle polymorphism
    Operator* rootOp = &scanOp;
    rootOp->setTxnContext(txn);   

    // Buffer for optional operators to ensure lifetime
    std::optional<SelectOperator> selectOpBuffer;
    std::optional<HashAggregationOperator> hashAggOpBuffer;

    // Apply WHERE conditions
    if (components.whereAttributeIndex != -1) {
        // Create simple predicates with comparison operators
        auto predicate1 = std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(components.whereAttributeIndex),
            SimplePredicate::Operand(std::make_unique<Field>(components.lowerBound)),
            SimplePredicate::ComparisonOperator::GT
        );

        auto predicate2 = std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(components.whereAttributeIndex),
            SimplePredicate::Operand(std::make_unique<Field>(components.upperBound)),
            SimplePredicate::ComparisonOperator::LT
        );

        // Combine simple predicates into a complex predicate with logical AND operator
        auto complexPredicate = std::make_unique<ComplexPredicate>(ComplexPredicate::LogicOperator::AND);
        complexPredicate->addPredicate(std::move(predicate1));
        complexPredicate->addPredicate(std::move(predicate2));

        // Using std::optional to manage the lifetime of SelectOperator
        selectOpBuffer.emplace(*rootOp, std::move(complexPredicate));
        rootOp = &*selectOpBuffer;
    }

    // Apply SUM or GROUP BY operation
    if (components.sumOperation || components.groupBy) {
        std::vector<size_t> groupByAttrs;
        if (components.groupBy) {
            groupByAttrs.push_back(static_cast<size_t>(components.groupByAttributeIndex));
        }
        std::vector<AggrFunc> aggrFuncs{
            {AggrFuncType::SUM, static_cast<size_t>(components.sumAttributeIndex)}
        };

        // Using std::optional to manage the lifetime of HashAggregationOperator
        hashAggOpBuffer.emplace(*rootOp, groupByAttrs, aggrFuncs);
        rootOp = &*hashAggOpBuffer;
    }

    // Execute the Root Operator
    rootOp->open();
    while (rootOp->next()) {
        // Retrieve and print the current tuple
        const auto& output = rootOp->getOutput();
        for (const auto& field : output) {
            field->print();
            std::cout << " ";
        }
        std::cout << std::endl;
    }
    rootOp->close();
}

class InsertOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<Tuple> tupleToInsert;

public:
    InsertOperator(TableHeap& table) : tableHeap(table) {}

    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {
        // Not used in this context
    }

    bool next() override {
        if (!tupleToInsert) {
            return false;
        }

        return tableHeap.insertTuple(std::move(tupleToInsert));
    }

    void close() override {
        // Not used in this context
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        return {};
    }
};

class DeleteOperator : public Operator {
private:
    BufferManager& bufferManager;
    size_t pageId;
    size_t tupleId;

public:
    DeleteOperator(BufferManager& manager, size_t pageId, size_t tupleId) 
        : bufferManager(manager), pageId(pageId), tupleId(tupleId) {}

    void open() override {
        // Not used in this context
    }

    bool next() override {
        auto& page = bufferManager.getPage(pageId);
        if (!page) {
            std::cerr << "Page not found." << std::endl;
            return false;
        }

        page->deleteTuple(tupleId); // Perform deletion
        bufferManager.flushPage(pageId); // Flush the page to disk after deletion
        return true;
    }

    void close() override {
        // Not used in this context
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        return {}; // Return empty vector
    }
};


class BuzzDB {
public:
    Catalog catalog;
    BufferManager buffer_manager;
    TransactionManager txn_manager;

public:
    BuzzDB() {
        // Storage Manager, Catalog, and Transaction Manager automatically created
    }

    TxnPtr begin() { return txn_manager.begin(); }
    void commit(const TxnPtr& tx) { txn_manager.commit(*tx); }
    void abort(const TxnPtr& tx) { txn_manager.abort(*tx); }

    TableId createTable(const std::string& name,
                        std::initializer_list<ColumnSchema> columns) {
        TableSchema schema;
        for (const auto& column : columns) {
            schema.columns.push_back(column);
        }

        auto table_id = catalog.createTable(name, std::move(schema));
        std::cout << "Created table " << name << " with id " << table_id << "\n";

        return table_id;
    }

    void insert(const std::string& table_name,
                const std::vector<Field>& fields,
                const TxnPtr& txn = nullptr) {
        auto& metadata = catalog.getTable(table_name);

        if (fields.size() != metadata.schema.columns.size()) {
            throw std::runtime_error(
                "Field count does not match schema for table: " + table_name
            );
        }

        auto tuple = std::make_unique<Tuple>();

        for (size_t field_index = 0; field_index < fields.size(); field_index++) {
            const auto expected_type = metadata.schema.columns[field_index].type;

            if (fields[field_index].getType() != expected_type) {
                throw std::runtime_error(
                    "Field type does not match schema for table: " + table_name
                );
            }

            tuple->addField(fields[field_index].clone());
        }

        TableHeap table(metadata, buffer_manager);
        InsertOperator insertOp(table);
        insertOp.setTxnContext(txn);
        insertOp.setTupleToInsert(std::move(tuple));

        bool status = insertOp.next();
        assert(status == true);
    }

    void insert(const std::string& table_name,
                std::initializer_list<Field> fields,
                const TxnPtr& txn = nullptr) {
        std::vector<Field> field_vector;
        for (const auto& field : fields) {
            field_vector.push_back(field);
        }

        insert(table_name, field_vector, txn);
    }

    static std::string trim(const std::string& input) {
        size_t start = 0;
        while (start < input.size() &&
               std::isspace(static_cast<unsigned char>(input[start]))) {
            start++;
        }

        size_t end = input.size();
        while (end > start &&
               std::isspace(static_cast<unsigned char>(input[end - 1]))) {
            end--;
        }

        return input.substr(start, end - start);
    }

    static std::vector<std::string> splitPipeLine(const std::string& line) {
        std::vector<std::string> tokens;
        std::stringstream ss(line);
        std::string token;

        while (std::getline(ss, token, '|')) {
            tokens.push_back(trim(token));
        }

        return tokens;
    }

    static Field parseFieldValue(FieldType type,
                                 const std::string& token,
                                 const std::string& table_name,
                                 size_t line_number) {
        try {
            switch (type) {
                case INT:
                    return Field(std::stoi(token));
                case FLOAT:
                    return Field(std::stof(token));
                case STRING:
                    return Field(token);
            }
        } catch (const std::exception&) {
            throw std::runtime_error(
                "Bad field value '" + token + "' for table " + table_name +
                " at line " + std::to_string(line_number)
            );
        }

        throw std::runtime_error("Unknown field type.");
    }

    void loadImdbFile(const std::string& filename,
                      const TxnPtr& txn = nullptr) {
        std::ifstream inputFile(filename);

        if (!inputFile) {
            throw std::runtime_error("Unable to open file: " + filename);
        }

        std::string line;
        size_t line_number = 0;

        while (std::getline(inputFile, line)) {
            line_number++;

            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto tokens = splitPipeLine(line);
            if (tokens.empty()) {
                continue;
            }

            const std::string& table_name = tokens[0];
            auto& metadata = catalog.getTable(table_name);

            if (tokens.size() - 1 != metadata.schema.columns.size()) {
                throw std::runtime_error(
                    "Wrong field count for table " + table_name +
                    " at line " + std::to_string(line_number)
                );
            }

            std::vector<Field> fields;
            fields.reserve(metadata.schema.columns.size());

            for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
                FieldType expected_type = metadata.schema.columns[i].type;
                fields.push_back(
                    parseFieldValue(expected_type, tokens[i + 1], table_name, line_number)
                );
            }

            insert(table_name, fields, txn);
        }
    }

    void scan(const std::string& table_name,
              const TxnPtr& txn = nullptr) {
        auto& metadata = catalog.getTable(table_name);
        TableHeap table(metadata, buffer_manager);

        ScanOperator scanOp(table);
        scanOp.setTxnContext(txn);

        std::cout << "\nTable " << table_name << "\n";

        scanOp.open();
        while (scanOp.next()) {
            auto output = scanOp.getOutput();

            for (const auto& field : output) {
                field->print();
                std::cout << " ";
            }

            std::cout << "\n";
        }
        scanOp.close();
    }

    void executeQueries(const std::string& table_name,
                        const TxnPtr& txn = nullptr) {
        std::vector<std::string> test_queries = {
            "SUM{1} GROUP BY {1} WHERE {1} > 2 and {1} < 6"
        };

        auto& metadata = catalog.getTable(table_name);
        TableHeap table(metadata, buffer_manager);

        for (const auto& query : test_queries) {
            auto components = parseQuery(query);
            prettyPrint(components);
            executeQuery(components, table, txn);
        }
    }
};

int main() {
    BuzzDB db;

    TxnPtr txn1 = db.begin();

    db.createTable("title", {
        {"id", INT},
        {"title", STRING},
        {"kind_id", INT},
        {"production_year", INT}
    });

    db.createTable("kind_type", {
        {"id", INT},
        {"kind", STRING}
    });

    db.createTable("company_name", {
        {"id", INT},
        {"name", STRING},
        {"country_code", STRING}
    });

    db.createTable("company_type", {
        {"id", INT},
        {"kind", STRING}
    });

    db.createTable("movie_companies", {
        {"id", INT},
        {"movie_id", INT},
        {"company_id", INT},
        {"company_type_id", INT},
        {"note", STRING}
    });

    db.loadImdbFile("imdb.txt", txn1);

    db.commit(txn1);

    TxnPtr txn2 = db.begin();
    auto start = std::chrono::high_resolution_clock::now();

    db.scan("title", txn2);
    db.scan("kind_type", txn2);
    db.scan("company_name", txn2);
    db.scan("company_type", txn2);
    db.scan("movie_companies", txn2);

    auto end = std::chrono::high_resolution_clock::now();

    db.commit(txn2);

    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
              << " microseconds" << std::endl;

    return 0;
}