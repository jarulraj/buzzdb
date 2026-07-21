#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <list>
#include <sstream>
#include <optional>
#include <regex>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <cassert>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <functional>
#include <utility>
#include <limits>
#include <unordered_map>
#include <unistd.h>

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

    // Clone method
    std::unique_ptr<Field> clone() const {
        // Use the copy constructor
        return std::make_unique<Field>(*this);
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

int compareFields(const Field& lhs, const Field& rhs) {
    if (lhs.type != rhs.type) {
        throw std::runtime_error("Cannot compare fields with different types.");
    }

    switch (lhs.type) {
        case INT:
            if (lhs.asInt() < rhs.asInt()) {
                return -1;
            }
            if (lhs.asInt() > rhs.asInt()) {
                return 1;
            }
            return 0;
        case FLOAT:
            if (lhs.asFloat() < rhs.asFloat()) {
                return -1;
            }
            if (lhs.asFloat() > rhs.asFloat()) {
                return 1;
            }
            return 0;
        case STRING:
            if (lhs.asString() < rhs.asString()) {
                return -1;
            }
            if (lhs.asString() > rhs.asString()) {
                return 1;
            }
            return 0;
    }
    throw std::runtime_error("Unsupported field type for comparison.");
}

bool fieldLess(const Field& lhs, const Field& rhs) {
    return compareFields(lhs, rhs) < 0;
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

    std::unique_ptr<Tuple> clone() const {
        auto tuple = std::make_unique<Tuple>();
        for (const auto& field : fields) {
            tuple->addField(field->clone());
        }
        return tuple;
    }
};

std::string fieldToString(const Field& field) {
    std::ostringstream output;
    switch (field.getType()) {
        case INT:
            output << field.asInt();
            break;
        case FLOAT:
            output << field.asFloat();
            break;
        case STRING:
            output << field.asString();
            break;
    }
    return output.str();
}

std::string tupleToString(const Tuple& tuple) {
    std::ostringstream output;
    output << "[";
    for (size_t i = 0; i < tuple.fields.size(); i++) {
        if (i != 0) {
            output << ", ";
        }
        output << fieldToString(*tuple.fields[i]);
    }
    output << "]";
    return output.str();
}

static constexpr size_t PAGE_SIZE = 4096;
static constexpr size_t MAX_SLOTS = 512;
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

using PageID = uint16_t;
using TableId = uint16_t;
using LSN = uint64_t;

constexpr PageID CATALOG_PAGE_ID = 0;
constexpr PageID INVALID_PAGE_ID = std::numeric_limits<PageID>::max();
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId SYS_TABLES_ID = 1;
constexpr TableId SYS_COLUMNS_ID = 2;
constexpr TableId FIRST_USER_TABLE_ID = 100;

struct RecordId {
    TableId table_id = INVALID_TABLE_ID;
    PageID page_id = INVALID_PAGE_ID;
    size_t slot_id = 0;
};

struct IndexDescriptor {
    TableId table_id = INVALID_TABLE_ID;
    size_t column_index = 0;

    bool operator<(const IndexDescriptor& other) const {
        if (table_id != other.table_id) {
            return table_id < other.table_id;
        }
        return column_index < other.column_index;
    }

    bool operator==(const IndexDescriptor& other) const {
        return table_id == other.table_id &&
               column_index == other.column_index;
    }
};

struct IndexKey {
    FieldType type = INT;
    int int_value = 0;
    float float_value = 0.0f;
    std::string string_value;

    explicit IndexKey(const Field& field) : type(field.getType()) {
        switch (type) {
            case INT:
                int_value = field.asInt();
                break;
            case FLOAT:
                float_value = field.asFloat();
                break;
            case STRING:
                string_value = field.asString();
                break;
        }
    }

    bool operator==(const IndexKey& other) const {
        if (type != other.type) {
            return false;
        }

        switch (type) {
            case INT:
                return int_value == other.int_value;
            case FLOAT:
                return float_value == other.float_value;
            case STRING:
                return string_value == other.string_value;
        }
        return false;
    }
};

struct IndexKeyHasher {
    std::size_t operator()(const IndexKey& key) const {
        switch (key.type) {
            case INT:
                return std::hash<int>{}(key.int_value);
            case FLOAT:
                return std::hash<float>{}(key.float_value);
            case STRING:
                return std::hash<std::string>{}(key.string_value);
        }
        return 0;
    }
};

const std::string BOOTSTRAP_MAGIC = "BUZZDB_BOOTSTRAP";

enum class CrashPoint {
    NONE,
    AFTER_STEAL_PAGE_FLUSH
};

struct PageHeader {
    TableId table_id = INVALID_TABLE_ID;
    PageID next_page = INVALID_PAGE_ID;
    LSN page_lsn = 0;
};

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

static_assert(sizeof(Slot) * MAX_SLOTS + sizeof(PageHeader) < PAGE_SIZE,
              "Slot directory and page header must leave tuple space.");

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);

    SlottedPage(){
        reset();
    }

    void reset() {
        std::memset(page_data.get(), 0, PAGE_SIZE);

        Slot* slot_array = slots();
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }

        header()->table_id = INVALID_TABLE_ID;
        header()->next_page = INVALID_PAGE_ID;
        header()->page_lsn = 0;
    }

    TableId getTableId() const {
        return header()->table_id;
    }

    void setTableId(TableId table_id) {
        header()->table_id = table_id;
    }

    PageID getNextPage() const {
        return header()->next_page;
    }

    void setNextPage(PageID page_id) {
        header()->next_page = page_id;
    }

    LSN getPageLSN() const {
        return header()->page_lsn;
    }

    void setPageLSN(LSN page_lsn) {
        header()->page_lsn = page_lsn;
    }

    bool addTuple(std::unique_ptr<Tuple> tuple) {
        return addTupleAndReturnSlot(std::move(tuple)).has_value();
    }

    std::optional<size_t> addTupleAndReturnSlot(std::unique_ptr<Tuple> tuple) {
        auto bytes = tuple->serialize();
        auto slot_id = findAvailableSlot(bytes.size());
        if (!slot_id || !putSerializedTupleAtSlot(*slot_id, bytes)) {
            return std::nullopt;
        }
        return slot_id;
    }

    bool putTupleAtSlot(size_t slot_id, std::unique_ptr<Tuple> tuple) {
        return putSerializedTupleAtSlot(slot_id, tuple->serialize());
    }

    std::unique_ptr<Tuple> getTuple(size_t slot_id) const {
        if (slot_id >= MAX_SLOTS || slots()[slot_id].empty) {
            return nullptr;
        }

        const auto& slot = slots()[slot_id];
        assert(slot.offset != INVALID_VALUE);
        assert(slot.length != INVALID_VALUE);
        const char* tuple_data = page_data.get() + slot.offset;
        std::istringstream iss(std::string(tuple_data, slot.length));
        return Tuple::deserialize(iss);
    }

    bool updateTuple(size_t slot_id, std::unique_ptr<Tuple> tuple) {
        if (slot_id >= MAX_SLOTS || slots()[slot_id].empty) {
            return false;
        }

        return putSerializedTupleAtSlot(slot_id, tuple->serialize());
    }

    void deleteTuple(size_t slot_id) {
        if (slot_id < MAX_SLOTS) {
            slots()[slot_id].empty = true;
        }
    }

private:
    static constexpr size_t SLOT_ARRAY_SIZE = sizeof(Slot) * MAX_SLOTS;
    static constexpr size_t HEADER_OFFSET = SLOT_ARRAY_SIZE;
    static constexpr size_t DATA_START = HEADER_OFFSET + sizeof(PageHeader);

    Slot* slots() {
        return reinterpret_cast<Slot*>(page_data.get());
    }

    const Slot* slots() const {
        return reinterpret_cast<const Slot*>(page_data.get());
    }

    PageHeader* header() {
        return reinterpret_cast<PageHeader*>(page_data.get() + HEADER_OFFSET);
    }

    const PageHeader* header() const {
        return reinterpret_cast<const PageHeader*>(page_data.get() + HEADER_OFFSET);
    }

    std::optional<size_t> findAvailableSlot(size_t tuple_size) const {
        const Slot* slot_array = slots();
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (!slot_array[slot_itr].empty) {
                continue;
            }
            if (slot_array[slot_itr].length == INVALID_VALUE ||
                slot_array[slot_itr].length >= tuple_size) {
                return slot_itr;
            }
        }
        return std::nullopt;
    }

    size_t nextFreeOffset() const {
        size_t offset = DATA_START;
        const Slot* slot_array = slots();
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].offset == INVALID_VALUE ||
                slot_array[slot_itr].length == INVALID_VALUE) {
                continue;
            }

            size_t slot_end = static_cast<size_t>(slot_array[slot_itr].offset) +
                              slot_array[slot_itr].length;
            if (slot_end > offset) {
                offset = slot_end;
            }
        }
        return offset;
    }

    bool putSerializedTupleAtSlot(size_t slot_itr,
                                  const std::string& bytes) {
        if (slot_itr >= MAX_SLOTS) {
            return false;
        }

        size_t tuple_size = bytes.size();
        auto& slot = slots()[slot_itr];
        if (slot.length != INVALID_VALUE && tuple_size > slot.length) {
            return false;
        }

        size_t offset = slot.offset;
        if (offset == INVALID_VALUE) {
            offset = nextFreeOffset();
        }

        if (offset < DATA_START || offset + tuple_size > PAGE_SIZE) {
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= DATA_START);
        assert(offset + tuple_size <= PAGE_SIZE);

        slot.empty = false;
        slot.offset = static_cast<uint16_t>(offset);
        if (slot.length == INVALID_VALUE) {
            slot.length = static_cast<uint16_t>(tuple_size);
        }

        std::memcpy(page_data.get() + offset, bytes.c_str(), tuple_size);
        return true;
    }
};

const std::string database_filename = "buzzdb.dat";
const std::string log_filename = "buzzdb.log";
const std::string master_record_filename = "buzzdb.master";

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

        //std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";        
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
        if(!fileStream.read(page->page_data.get(), PAGE_SIZE)){
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
        //std::cout << "Extending database file \n";

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

class Policy {
public:
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

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

class LogManager;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    LogManager& log_manager;
    std::unique_ptr<Policy> policy;
    std::set<PageID> pinned_pages;
    std::function<void(PageID, LSN)> page_flush_callback;

public:
    explicit BufferManager(LogManager& log_manager)
        : log_manager(log_manager),
          policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {}

    void setPageFlushCallback(std::function<void(PageID, LSN)> callback) {
        page_flush_callback = std::move(callback);
    }

    std::unique_ptr<SlottedPage>& getPage(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            evictUnpinnedPage();
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        //std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(page);
        return pageMap[page_id];
    }

    void flushPage(int page_id, const std::string& reason = "page flush") {
        auto& page = getPage(page_id);
        forceLogBeforePageFlush(page_id, reason);
        LSN page_lsn = page->getPageLSN();
        storage_manager.flush(page_id, page);
        if (page_flush_callback) {
            page_flush_callback(page_id, page_lsn);
        }
    }

    void clearBufferPool() {
        if (!pinned_pages.empty()) {
            throw std::runtime_error("Cannot clear buffer pool with pinned pages.");
        }

        std::vector<PageID> cached_pages;
        for (const auto& entry : pageMap) {
            cached_pages.push_back(entry.first);
        }
        for (PageID page_id : cached_pages) {
            forceLogBeforePageFlush(page_id, "buffer pool clear");
            LSN page_lsn = pageMap.at(page_id)->getPageLSN();
            storage_manager.flush(page_id, pageMap.at(page_id));
            if (page_flush_callback) {
                page_flush_callback(page_id, page_lsn);
            }
        }
        pageMap.clear();
        policy = std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY);
    }

    void pinPage(PageID page_id) {
        pinned_pages.insert(page_id);
    }

    void unpinPage(PageID page_id) {
        pinned_pages.erase(page_id);
    }

    void extend(){
        storage_manager.extend();
    }

    PageID appendPage(TableId table_id) {
        storage_manager.extend();
        auto page_id = static_cast<PageID>(storage_manager.num_pages - 1);
        resetPage(page_id, table_id);
        return page_id;
    }

    void resetPage(PageID page_id, TableId table_id) {
        auto& page = getPage(page_id);
        page->reset();
        page->setTableId(table_id);
        page->setNextPage(INVALID_PAGE_ID);
        flushPage(page_id);
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

private:
    void forceLogBeforePageFlush(PageID page_id, const std::string&);

    void evictUnpinnedPage() {
        size_t attempts = pageMap.size();
        while (attempts-- > 0) {
            auto evictedPageId = policy->evict();
            if(evictedPageId == INVALID_VALUE){
                break;
            }
            if (pinned_pages.find(evictedPageId) != pinned_pages.end()) {
                policy->touch(evictedPageId);
                continue;
            }

            forceLogBeforePageFlush(evictedPageId, "eviction");
            LSN page_lsn = pageMap[evictedPageId]->getPageLSN();
            storage_manager.flush(evictedPageId, pageMap[evictedPageId]);
            if (page_flush_callback) {
                page_flush_callback(evictedPageId, page_lsn);
            }
            pageMap.erase(evictedPageId);
            return;
        }

        throw std::runtime_error("All buffer pages are pinned.");
    }
};

enum class LogRecordType {
    BEGIN,
    UPDATE,
    INSERT,
    DELETE,
    CLR,
    COMMIT,
    ABORT,
    END,
    BEGIN_CHECKPOINT,
    END_CHECKPOINT
};

bool isTupleChangeRecord(LogRecordType type);

struct RecoveryAnalysis {
    enum class TxnStatus {
        RUNNING,
        COMMITTING,
        ABORTING
    };

    struct ActiveTransactionEntry {
        TxnStatus status = TxnStatus::RUNNING;
        LSN last_lsn = 0;
    };

    using ActiveTransactionTable = std::map<int, ActiveTransactionEntry>;
    using DirtyPageTable = std::map<PageID, LSN>;

    ActiveTransactionTable active_transaction_table;
    DirtyPageTable dirty_page_table;
    int next_txn_id = 1;
};

struct LogRecord {
    LSN lsn = 0;
    LSN prev_lsn = 0;
    LogRecordType type;
    int txn_id = 0;
    TableId table_id = INVALID_TABLE_ID;
    PageID page_id = INVALID_PAGE_ID;
    size_t slot_id = 0;
    LogRecordType undo_type = LogRecordType::UPDATE;
    LSN undo_next_lsn = 0;
    std::unique_ptr<Tuple> before_tuple;
    std::unique_ptr<Tuple> after_tuple;
    RecoveryAnalysis::ActiveTransactionTable checkpoint_att;
    RecoveryAnalysis::DirtyPageTable checkpoint_dpt;

    LogRecord(LogRecordType type, int txn_id)
        : type(type), txn_id(txn_id) {}

    LogRecord(LogRecordType type,
              int txn_id,
              TableId table_id,
              PageID page_id,
              size_t slot_id,
              std::unique_ptr<Tuple> before_tuple,
              std::unique_ptr<Tuple> after_tuple)
        : type(type),
          txn_id(txn_id),
          table_id(table_id),
          page_id(page_id),
          slot_id(slot_id),
          before_tuple(std::move(before_tuple)),
          after_tuple(std::move(after_tuple)) {}
};

bool isTupleChangeRecord(LogRecordType type) {
    return type == LogRecordType::UPDATE ||
           type == LogRecordType::INSERT ||
           type == LogRecordType::DELETE;
}

bool isRedoableRecord(LogRecordType type) {
    return isTupleChangeRecord(type) || type == LogRecordType::CLR;
}

std::string logRecordName(LogRecordType type) {
    switch (type) {
        case LogRecordType::BEGIN:
            return "BEGIN";
        case LogRecordType::UPDATE:
            return "UPDATE";
        case LogRecordType::INSERT:
            return "INSERT";
        case LogRecordType::DELETE:
            return "DELETE";
        case LogRecordType::CLR:
            return "CLR";
        case LogRecordType::COMMIT:
            return "COMMIT";
        case LogRecordType::ABORT:
            return "ABORT";
        case LogRecordType::END:
            return "END";
        case LogRecordType::BEGIN_CHECKPOINT:
            return "BEGIN_CHECKPOINT";
        case LogRecordType::END_CHECKPOINT:
            return "END_CHECKPOINT";
    }
    throw std::runtime_error("Unknown log record.");
}

std::string recoveryTxnStatusName(RecoveryAnalysis::TxnStatus status) {
    switch (status) {
        case RecoveryAnalysis::TxnStatus::RUNNING:
            return "RUNNING";
        case RecoveryAnalysis::TxnStatus::COMMITTING:
            return "COMMITTING";
        case RecoveryAnalysis::TxnStatus::ABORTING:
            return "ABORTING";
    }
    throw std::runtime_error("Unknown transaction status.");
}

RecoveryAnalysis::TxnStatus parseRecoveryTxnStatus(
    const std::string& status) {
    if (status == "RUNNING") {
        return RecoveryAnalysis::TxnStatus::RUNNING;
    }
    if (status == "COMMITTING") {
        return RecoveryAnalysis::TxnStatus::COMMITTING;
    }
    if (status == "ABORTING") {
        return RecoveryAnalysis::TxnStatus::ABORTING;
    }
    throw std::runtime_error("Unknown transaction status in checkpoint: " + status);
}

bool parseLogRecordType(const std::string& type,
                        LogRecordType& record_type) {
    if (type == "BEGIN") {
        record_type = LogRecordType::BEGIN;
        return true;
    }
    if (type == "UPDATE") {
        record_type = LogRecordType::UPDATE;
        return true;
    }
    if (type == "INSERT") {
        record_type = LogRecordType::INSERT;
        return true;
    }
    if (type == "DELETE") {
        record_type = LogRecordType::DELETE;
        return true;
    }
    if (type == "CLR") {
        record_type = LogRecordType::CLR;
        return true;
    }
    if (type == "COMMIT") {
        record_type = LogRecordType::COMMIT;
        return true;
    }
    if (type == "ABORT") {
        record_type = LogRecordType::ABORT;
        return true;
    }
    if (type == "END") {
        record_type = LogRecordType::END;
        return true;
    }
    if (type == "BEGIN_CHECKPOINT") {
        record_type = LogRecordType::BEGIN_CHECKPOINT;
        return true;
    }
    if (type == "END_CHECKPOINT") {
        record_type = LogRecordType::END_CHECKPOINT;
        return true;
    }
    return false;
}

struct MasterRecord {
    LSN checkpoint_begin_lsn = 0;
    size_t checkpoint_begin_offset = 0;
};

MasterRecord readMasterRecordFile() {
    std::ifstream input(master_record_filename);
    MasterRecord master_record;
    if (!input) {
        return master_record;
    }

    std::string key;
    input >> key >> master_record.checkpoint_begin_lsn;
    if (key != "checkpoint_lsn") {
        throw std::runtime_error("Malformed ARIES master record.");
    }
    input >> key >> master_record.checkpoint_begin_offset;
    if (key != "checkpoint_offset") {
        throw std::runtime_error("Malformed ARIES master record.");
    }
    return master_record;
}

class LogManager {
private:
    struct PendingRecord {
        LSN lsn;
        std::string text;
    };

    std::vector<PendingRecord> pending_records;
    std::map<LSN, size_t> log_offsets;
    int log_fd = -1;
    bool log_existed_at_open = false;
    LSN next_lsn = 1;
    LSN flushed_lsn = 0;
    size_t next_log_offset = 0;
    size_t records_written = 0;
    size_t bytes_written = 0;

public:
    LogManager() {
        openLogFile();
    }

    ~LogManager() {
        if (log_fd != -1) {
            ::close(log_fd);
        }
    }

    void reset() {
        if (::ftruncate(log_fd, 0) == -1) {
            throw std::runtime_error(
                "Unable to reset recovery log: " +
                std::string(std::strerror(errno))
            );
        }
        syncLogFile();
        std::remove(master_record_filename.c_str());
        pending_records.clear();
        log_offsets.clear();
        next_lsn = 1;
        flushed_lsn = 0;
        next_log_offset = 0;
        records_written = 0;
        bytes_written = 0;
    }

    LSN append(LogRecord& record) {
        record.lsn = next_lsn++;
        auto text = serialize(record);
        pending_records.push_back({record.lsn, text});
        log_offsets[record.lsn] = next_log_offset;
        records_written++;
        bytes_written += text.size() + 1;
        next_log_offset += text.size() + 1;
        return record.lsn;
    }

    bool forceUpTo(LSN lsn) {
        if (lsn <= flushed_lsn) {
            return false;
        }

        size_t force_count = 0;
        LSN last_forced_lsn = flushed_lsn;
        std::string log_bytes;
        while (force_count < pending_records.size() &&
               pending_records[force_count].lsn <= lsn) {
            log_bytes += pending_records[force_count].text;
            log_bytes += "\n";
            last_forced_lsn = pending_records[force_count].lsn;
            force_count++;
        }
        if (force_count == 0) {
            return false;
        }

        writeAll(log_bytes);
        syncLogFile();

        flushed_lsn = last_forced_lsn;
        pending_records.erase(pending_records.begin(),
                              pending_records.begin() + force_count);
        next_log_offset = durableLogSize();
        return true;
    }

    LSN getFlushedLSN() const {
        return flushed_lsn;
    }

    bool existedAtOpen() const {
        return log_existed_at_open;
    }

    size_t getLogOffset(LSN lsn) const {
        auto offset = log_offsets.find(lsn);
        if (offset == log_offsets.end()) {
            throw std::runtime_error("No log offset found for LSN.");
        }
        return offset->second;
    }

    std::vector<LogRecord> readAll() {
        return readFromOffset(0);
    }

    std::vector<LogRecord> readFromOffset(size_t start_offset) {
        std::vector<LogRecord> records;
        pending_records.clear();
        next_log_offset = durableLogSize();
        std::istringstream input(readLogContentsFromOffset(start_offset));

        std::string line;
        LSN durable_lsn = flushed_lsn;
        size_t line_offset = start_offset;
        while (std::getline(input, line)) {
            if (!line.empty()) {
                auto record = parse(line);
                log_offsets[record.lsn] = line_offset;
                durable_lsn = std::max(durable_lsn, record.lsn);
                records.push_back(std::move(record));
            }
            line_offset += line.size() + 1;
        }
        flushed_lsn = durable_lsn;
        next_lsn = std::max(next_lsn, durable_lsn + 1);
        return records;
    }

    void writeMasterRecord(LSN checkpoint_begin_lsn) const {
        std::ofstream output(master_record_filename, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to write ARIES master record.");
        }
        output << "checkpoint_lsn " << checkpoint_begin_lsn << "\n"
               << "checkpoint_offset " << getLogOffset(checkpoint_begin_lsn)
               << "\n";
        output.flush();
    }

    MasterRecord readMasterRecord() const {
        return readMasterRecordFile();
    }

    size_t truncateBefore(LSN keep_lsn) {
        if (!pending_records.empty()) {
            throw std::runtime_error("Cannot truncate log with unforced records.");
        }

        auto records = readAll();
        std::vector<std::string> kept_records;
        size_t removed = 0;
        for (auto& record : records) {
            if (record.lsn < keep_lsn) {
                removed++;
                continue;
            }
            kept_records.push_back(serialize(record));
        }

        log_offsets.clear();
        next_log_offset = 0;
        flushed_lsn = 0;
        std::string log_bytes;
        for (const auto& text : kept_records) {
            auto record = parse(text);
            log_offsets[record.lsn] = next_log_offset;
            log_bytes += text;
            log_bytes += "\n";
            next_log_offset += text.size() + 1;
            flushed_lsn = record.lsn;
        }
        if (::ftruncate(log_fd, 0) == -1) {
            throw std::runtime_error(
                "Unable to truncate recovery log: " +
                std::string(std::strerror(errno))
            );
        }
        if (::lseek(log_fd, 0, SEEK_SET) == -1) {
            throw std::runtime_error(
                "Unable to seek recovery log after truncation: " +
                std::string(std::strerror(errno))
            );
        }
        writeAll(log_bytes);
        syncLogFile();
        next_lsn = std::max(next_lsn, flushed_lsn + 1);
        return removed;
    }

private:
    size_t durableLogSize() const {
        off_t end = ::lseek(log_fd, 0, SEEK_END);
        if (end == -1) {
            throw std::runtime_error(
                "Unable to seek recovery log end: " +
                std::string(std::strerror(errno))
            );
        }
        return static_cast<size_t>(end);
    }

    void openLogFile() {
        if (log_fd != -1) {
            return;
        }
        log_existed_at_open = (::access(log_filename.c_str(), F_OK) == 0);
        log_fd = ::open(log_filename.c_str(),
                        O_RDWR | O_CREAT | O_APPEND,
                        0644);
        if (log_fd == -1) {
            throw std::runtime_error(
                "Unable to open recovery log: " +
                std::string(std::strerror(errno))
            );
        }
    }

    void writeAll(const std::string& bytes) {
        size_t written = 0;
        while (written < bytes.size()) {
            ssize_t result = ::write(log_fd,
                                     bytes.data() + written,
                                     bytes.size() - written);
            if (result == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(
                    "Unable to write recovery log: " +
                    std::string(std::strerror(errno))
                );
            }
            written += static_cast<size_t>(result);
        }
    }

    void syncLogFile() {
        if (::fsync(log_fd) == -1) {
            throw std::runtime_error(
                "Unable to sync recovery log: " +
                std::string(std::strerror(errno))
            );
        }
    }

    std::string readLogContentsFromOffset(size_t start_offset) {
        if (::lseek(log_fd, static_cast<off_t>(start_offset), SEEK_SET) == -1) {
            throw std::runtime_error(
                "Unable to seek recovery log: " +
                std::string(std::strerror(errno))
            );
        }

        std::string contents;
        char buffer[4096];
        while (true) {
            ssize_t result = ::read(log_fd, buffer, sizeof(buffer));
            if (result == -1) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(
                    "Unable to read recovery log: " +
                    std::string(std::strerror(errno))
                );
            }
            if (result == 0) {
                break;
            }
            contents.append(buffer, static_cast<size_t>(result));
        }
        if (::lseek(log_fd, 0, SEEK_END) == -1) {
            throw std::runtime_error(
                "Unable to seek recovery log end: " +
                std::string(std::strerror(errno))
            );
        }
        return contents;
    }

    LogRecord parse(const std::string& line) const {
        std::istringstream input(line);
        LSN lsn = 0;
        std::string type;
        int txn_id = 0;
        LSN prev_lsn = 0;
        input >> lsn >> type >> txn_id >> prev_lsn;
        if (!input) {
            throw std::runtime_error("Bad log record: " + line);
        }

        LogRecordType record_type = LogRecordType::BEGIN;
        if (!parseLogRecordType(type, record_type)) {
            throw std::runtime_error("Unknown log record: " + line);
        }

        LogRecord record{record_type, txn_id};
        record.lsn = lsn;
        record.prev_lsn = prev_lsn;
        if (record_type == LogRecordType::CLR) {
            std::string undo_type;
            int table_id = 0;
            int page_id = 0;
            size_t slot_id = 0;
            input >> record.undo_next_lsn
                  >> undo_type
                  >> table_id
                  >> page_id
                  >> slot_id;
            if (!input || !parseLogRecordType(undo_type, record.undo_type) ||
                !isTupleChangeRecord(record.undo_type)) {
                throw std::runtime_error("Bad CLR log record: " + line);
            }

            record.table_id = static_cast<TableId>(table_id);
            record.page_id = static_cast<PageID>(page_id);
            record.slot_id = slot_id;
            if (record.undo_type != LogRecordType::INSERT) {
                record.after_tuple = Tuple::deserialize(input);
            }
        } else if (isTupleChangeRecord(record_type)) {
            int table_id = 0;
            int page_id = 0;
            size_t slot_id = 0;
            input >> table_id >> page_id >> slot_id;
            if (!input) {
                throw std::runtime_error("Bad log operation record: " + line);
            }

            record.table_id = static_cast<TableId>(table_id);
            record.page_id = static_cast<PageID>(page_id);
            record.slot_id = slot_id;
            if (record_type == LogRecordType::UPDATE ||
                record_type == LogRecordType::DELETE) {
                record.before_tuple = Tuple::deserialize(input);
            }
            if (record_type == LogRecordType::UPDATE ||
                record_type == LogRecordType::INSERT) {
                record.after_tuple = Tuple::deserialize(input);
            }
        } else if (record_type == LogRecordType::END_CHECKPOINT) {
            std::string marker;
            size_t entry_count = 0;
            input >> marker >> entry_count;
            if (!input || marker != "ATT") {
                throw std::runtime_error("Bad checkpoint ATT record: " + line);
            }
            for (size_t i = 0; i < entry_count; i++) {
                int txn_id = 0;
                std::string status;
                LSN last_lsn = 0;
                input >> txn_id >> status >> last_lsn;
                if (!input) {
                    throw std::runtime_error("Bad checkpoint ATT entry: " + line);
                }
                record.checkpoint_att[txn_id] = {
                    parseRecoveryTxnStatus(status),
                    last_lsn
                };
            }

            input >> marker >> entry_count;
            if (!input || marker != "DPT") {
                throw std::runtime_error("Bad checkpoint DPT record: " + line);
            }
            for (size_t i = 0; i < entry_count; i++) {
                int page_id = 0;
                LSN rec_lsn = 0;
                input >> page_id >> rec_lsn;
                if (!input) {
                    throw std::runtime_error("Bad checkpoint DPT entry: " + line);
                }
                record.checkpoint_dpt[static_cast<PageID>(page_id)] = rec_lsn;
            }
        }
        return record;
    }

    std::string serialize(const LogRecord& record) const {
        std::ostringstream output;
        if (record.lsn == 0) {
            throw std::runtime_error("Log record is missing an LSN.");
        }
        output << record.lsn << " "
               << logRecordName(record.type) << " "
               << record.txn_id << " "
               << record.prev_lsn;
        if (record.type == LogRecordType::CLR) {
            output << " " << record.undo_next_lsn
                   << " " << logRecordName(record.undo_type)
                   << " " << record.table_id
                   << " " << record.page_id
                   << " " << record.slot_id;
            if (record.undo_type != LogRecordType::INSERT) {
                if (!record.after_tuple) {
                    throw std::runtime_error("CLR is missing after-undo image.");
                }
                output << " " << record.after_tuple->serialize();
            }
        } else if (isTupleChangeRecord(record.type)) {
            output << " " << record.table_id
                   << " " << record.page_id
                   << " " << record.slot_id;
            if (record.type == LogRecordType::UPDATE ||
                record.type == LogRecordType::DELETE) {
                if (!record.before_tuple) {
                    throw std::runtime_error("Log record is missing before image.");
                }
                output << " " << record.before_tuple->serialize();
            }
            if (record.type == LogRecordType::UPDATE ||
                record.type == LogRecordType::INSERT) {
                if (!record.after_tuple) {
                    throw std::runtime_error("Log record is missing after image.");
                }
                output << " " << record.after_tuple->serialize();
            }
        } else if (record.type == LogRecordType::END_CHECKPOINT) {
            output << " ATT " << record.checkpoint_att.size();
            for (const auto& txn : record.checkpoint_att) {
                output << " " << txn.first
                       << " " << recoveryTxnStatusName(txn.second.status)
                       << " " << txn.second.last_lsn;
            }
            output << " DPT " << record.checkpoint_dpt.size();
            for (const auto& page : record.checkpoint_dpt) {
                output << " " << page.first
                       << " " << page.second;
            }
        }
        return output.str();
    }
};

void BufferManager::forceLogBeforePageFlush(PageID page_id,
                                        const std::string&) {
    auto it = pageMap.find(page_id);
    if (it == pageMap.end()) {
        return;
    }

    LSN page_lsn = it->second->getPageLSN();
    if (page_lsn == 0) {
        return;
    }

    log_manager.forceUpTo(page_lsn);
    if (log_manager.getFlushedLSN() < page_lsn) {
        throw std::runtime_error("WAL rule violated: flushedLSN < pageLSN.");
    }
}

class RecoveryManager {
private:
    struct RuntimeTxnState {
        LSN last_lsn = 0;
        std::vector<LogRecord> staged_records;
        std::vector<PageID> dirty_pages;
    };

    BufferManager& buffer_manager;
    LogManager& log_manager;
    CrashPoint crash_point = CrashPoint::NONE;
    int next_txn_id = 1;
    int current_txn_id = 0;
    std::map<int, RuntimeTxnState> runtime_txns;
    RecoveryAnalysis::DirtyPageTable dirty_page_table;

public:
    RecoveryManager(BufferManager& buffer_manager,
                    LogManager& log_manager)
        : buffer_manager(buffer_manager),
          log_manager(log_manager) {
        buffer_manager.setPageFlushCallback([this](PageID page_id, LSN page_lsn) {
            notePageFlushed(page_id, page_lsn);
        });
    }

    bool isActive() const {
        return current_txn_id != 0;
    }

    void recover() {
        if (!logExists()) {
            if (databaseHasExistingPages()) {
                throwInconsistentDatabase("missing WAL file");
            }
            initializeEmpty();
            return;
        }

        MasterRecord master_record = log_manager.readMasterRecord();
        auto records = readRecordsForAnalysis(master_record);
        auto analysis = analysisPass(
            records,
            master_record.checkpoint_begin_lsn != 0
        );
        next_txn_id = std::max(next_txn_id, analysis.next_txn_id);
        const auto* recovery_records = &records;
        std::vector<LogRecord> full_log_records;
        if (needsRetainedLogForRecovery(master_record, records, analysis)) {
            full_log_records = log_manager.readAll();
            recovery_records = &full_log_records;
        }
        redoPass(*recovery_records, analysis);
        undoPass(*recovery_records, analysis);
    }

    void resetLog() {
        log_manager.reset();
        dirty_page_table.clear();
    }

    PageID allocatePhysicalPage(TableId table_id) {
        return buffer_manager.appendPage(table_id);
    }

    int begin() {
        int txn_id = next_txn_id++;
        runtime_txns[txn_id] = RuntimeTxnState{};
        current_txn_id = txn_id;
        LogRecord begin_record{LogRecordType::BEGIN, txn_id};
        appendTxnRecord(txn_id, begin_record);
        return txn_id;
    }

    void commit() {
        if (current_txn_id == 0) {
            throw std::runtime_error("COMMIT without BEGIN.");
        }
        commit(current_txn_id);
    }

    void commit(int txn_id) {
        LogRecord commit_record{LogRecordType::COMMIT, txn_id};
        LSN commit_lsn = appendTxnRecord(txn_id, commit_record);
        forceLogUpTo(commit_lsn);

        LogRecord end_record{LogRecordType::END, txn_id};
        LSN end_lsn = appendTxnRecord(txn_id, end_record);
        forceLogUpTo(end_lsn);

        runtime_txns.erase(txn_id);
        if (current_txn_id == txn_id) {
            current_txn_id = 0;
        }
        crash_point = CrashPoint::NONE;
    }

    void abort() {
        if (current_txn_id == 0) {
            throw std::runtime_error("ABORT without BEGIN.");
        }
        abort(current_txn_id);
    }

    void abort(int txn_id) {
        auto& txn = runtimeTxn(txn_id);

        LogRecord abort_record{LogRecordType::ABORT, txn_id};
        LSN abort_lsn = appendTxnRecord(txn_id, abort_record);
        forceLogUpTo(abort_lsn);

        for (auto it = txn.staged_records.rbegin(); it != txn.staged_records.rend(); ++it) {
            LogRecord clr = makeClrRecord(txn_id,
                                          txn.last_lsn,
                                          *it);
            LSN clr_lsn = log_manager.append(clr);
            txn.last_lsn = clr_lsn;
            rememberDirtyPage(clr.page_id, clr_lsn);
            applyRecordToPage(clr);
        }

        forceDirtyPages(txn);
        LogRecord end_record{LogRecordType::END, txn_id};
        LSN end_lsn = appendTxnRecord(txn_id, end_record);
        forceLogUpTo(end_lsn);

        runtime_txns.erase(txn_id);
        if (current_txn_id == txn_id) {
            current_txn_id = 0;
        }
        crash_point = CrashPoint::NONE;
    }

    void setCurrentTransaction(int txn_id) {
        runtimeTxn(txn_id);
        current_txn_id = txn_id;
    }

    void clearCurrentTransaction() {
        current_txn_id = 0;
    }

    void stageUpdate(TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple,
                     std::unique_ptr<Tuple> after_tuple) {
        if (current_txn_id == 0) {
            return;
        }

        stageRecord(LogRecordType::UPDATE,
                    table_id,
                    page_id,
                    slot_id,
                    std::move(before_tuple),
                    std::move(after_tuple));
    }

    void stageInsert(TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> after_tuple) {
        if (current_txn_id == 0) {
            return;
        }

        stageRecord(LogRecordType::INSERT,
                    table_id,
                    page_id,
                    slot_id,
                    nullptr,
                    std::move(after_tuple));
    }

    void stageDelete(TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple) {
        if (current_txn_id == 0) {
            return;
        }

        stageRecord(LogRecordType::DELETE,
                    table_id,
                    page_id,
                    slot_id,
                    std::move(before_tuple),
                    nullptr);
    }

    void checkpoint() {
        LogRecord begin_record{LogRecordType::BEGIN_CHECKPOINT, 0};
        LSN begin_lsn = log_manager.append(begin_record);
        forceLogUpTo(begin_lsn);

        LogRecord end_record{LogRecordType::END_CHECKPOINT, 0};
        for (const auto& [txn_id, txn] : runtime_txns) {
            end_record.checkpoint_att[txn_id] = {
                RecoveryAnalysis::TxnStatus::RUNNING,
                txn.last_lsn
            };
        }
        end_record.checkpoint_dpt = dirty_page_table;
        LSN end_lsn = log_manager.append(end_record);
        forceLogUpTo(end_lsn);

        bool can_truncate = end_record.checkpoint_att.empty() &&
                            end_record.checkpoint_dpt.empty();
        if (can_truncate) {
            log_manager.truncateBefore(begin_lsn);
        }

        log_manager.writeMasterRecord(begin_lsn);
    }

    void crashAt(CrashPoint point) {
        crash_point = point;
    }

    bool shouldCrashAt(CrashPoint point) const {
        return crash_point == point;
    }

    void crashIfRequested(CrashPoint point, const std::string& message) {
        if (crash_point != point) {
            return;
        }
        crash_point = CrashPoint::NONE;
        throw std::runtime_error(message);
    }

private:
    void initializeEmpty() {
        log_manager.reset();
    }

    bool databaseHasExistingPages() const {
        return buffer_manager.getNumPages() > 1;
    }

    bool logExists() const {
        return log_manager.existedAtOpen();
    }

    [[noreturn]] void throwInconsistentDatabase(const std::string& reason) const {
        throw std::runtime_error(
            "Inconsistent BuzzDB files: " + reason + ". " +
            "buzzdb.dat and buzzdb.log must be kept together. "
            "Remove all of them to start a fresh database."
        );
    }

    void notePageFlushed(PageID page_id, LSN page_lsn) {
        auto dirty_page = dirty_page_table.find(page_id);
        if (dirty_page != dirty_page_table.end() &&
            dirty_page->second <= page_lsn) {
            dirty_page_table.erase(dirty_page);
        }
    }

    void rememberDirtyPage(PageID page_id, LSN rec_lsn) {
        if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
            dirty_page_table[page_id] = rec_lsn;
        }
    }

    std::vector<LogRecord> readRecordsForAnalysis(
        const MasterRecord& master_record) {
        if (master_record.checkpoint_begin_lsn == 0) {
            return log_manager.readAll();
        }

        auto records = log_manager.readFromOffset(
            master_record.checkpoint_begin_offset);
        if (records.empty()) {
            throw std::runtime_error(
                "ARIES master record points past the end of buzzdb.log."
            );
        }
        if (records.front().lsn != master_record.checkpoint_begin_lsn ||
            records.front().type != LogRecordType::BEGIN_CHECKPOINT) {
            throw std::runtime_error(
                "ARIES master record does not point at BEGIN_CHECKPOINT."
            );
        }
        return records;
    }

    LSN minRecLSN(const RecoveryAnalysis& analysis) const {
        LSN min_rec_lsn = 0;
        for (const auto& [page_id, rec_lsn] : analysis.dirty_page_table) {
            (void)page_id;
            if (min_rec_lsn == 0 || rec_lsn < min_rec_lsn) {
                min_rec_lsn = rec_lsn;
            }
        }
        return min_rec_lsn;
    }

    bool needsRetainedLogForRecovery(
        const MasterRecord& master_record,
        const std::vector<LogRecord>& analysis_records,
        const RecoveryAnalysis& analysis) {
        if (master_record.checkpoint_begin_lsn == 0 ||
            analysis_records.empty()) {
            return false;
        }

        LSN min_rec_lsn = minRecLSN(analysis);
        if (min_rec_lsn != 0 && min_rec_lsn < analysis_records.front().lsn) {
            return true;
        }
        return false;
    }

    RuntimeTxnState& runtimeTxn(int txn_id) {
        auto it = runtime_txns.find(txn_id);
        if (it == runtime_txns.end()) {
            throw std::runtime_error("Unknown active transaction.");
        }
        return it->second;
    }

    bool isDirtyPage(const RuntimeTxnState& txn, PageID page_id) const {
        for (PageID dirty_page_id : txn.dirty_pages) {
            if (dirty_page_id == page_id) {
                return true;
            }
        }
        return false;
    }

    LSN appendTxnRecord(int txn_id, LogRecord& record) {
        auto& txn = runtimeTxn(txn_id);
        record.prev_lsn = txn.last_lsn;
        LSN record_lsn = log_manager.append(record);
        txn.last_lsn = record_lsn;
        return record_lsn;
    }

    std::pair<LSN, LSN> forceLogUpTo(LSN lsn) {
        LSN before = log_manager.getFlushedLSN();
        log_manager.forceUpTo(lsn);
        return {before, log_manager.getFlushedLSN()};
    }

    size_t forceDirtyPages(const RuntimeTxnState& txn) {
        size_t forced = 0;
        for (PageID page_id : txn.dirty_pages) {
            buffer_manager.flushPage(page_id);
            forced++;
        }
        return forced;
    }

    void stageRecord(LogRecordType type,
                     TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple,
                     std::unique_ptr<Tuple> after_tuple) {
        auto& txn = runtimeTxn(current_txn_id);
        if (!isDirtyPage(txn, page_id)) {
            txn.dirty_pages.push_back(page_id);
        }

        LogRecord record{
            type,
            current_txn_id,
            table_id,
            page_id,
            slot_id,
            std::move(before_tuple),
            std::move(after_tuple)
        };
        LSN record_lsn = appendTxnRecord(current_txn_id, record);
        rememberDirtyPage(page_id, record_lsn);
        auto& page = buffer_manager.getPage(page_id);
        page->setPageLSN(record_lsn);
        txn.staged_records.push_back(std::move(record));
    }

    RecoveryAnalysis analysisPass(const std::vector<LogRecord>& records,
                                  bool load_checkpoint_snapshot) const {
        RecoveryAnalysis analysis;
        bool checkpoint_snapshot_loaded = !load_checkpoint_snapshot;
        for (const auto& record : records) {
            analysis.next_txn_id = std::max(analysis.next_txn_id,
                                            record.txn_id + 1);
            if (record.type == LogRecordType::BEGIN) {
                analysis.active_transaction_table[record.txn_id] = {
                    RecoveryAnalysis::TxnStatus::RUNNING,
                    record.lsn
                };
            } else if (isTupleChangeRecord(record.type)) {
                auto status = RecoveryAnalysis::TxnStatus::RUNNING;
                auto it = analysis.active_transaction_table.find(record.txn_id);
                if (it != analysis.active_transaction_table.end()) {
                    status = it->second.status;
                }
                analysis.active_transaction_table[record.txn_id] = {status, record.lsn};
            } else if (record.type == LogRecordType::CLR) {
                auto status = RecoveryAnalysis::TxnStatus::RUNNING;
                auto it = analysis.active_transaction_table.find(record.txn_id);
                if (it != analysis.active_transaction_table.end()) {
                    status = it->second.status;
                }
                analysis.active_transaction_table[record.txn_id] = {status, record.lsn};
            } else if (record.type == LogRecordType::COMMIT) {
                analysis.active_transaction_table[record.txn_id] = {
                    RecoveryAnalysis::TxnStatus::COMMITTING,
                    record.lsn
                };
            } else if (record.type == LogRecordType::ABORT) {
                analysis.active_transaction_table[record.txn_id] = {
                    RecoveryAnalysis::TxnStatus::ABORTING,
                    record.lsn
                };
            } else if (record.type == LogRecordType::END) {
                analysis.active_transaction_table.erase(record.txn_id);
            } else if (record.type == LogRecordType::END_CHECKPOINT &&
                       !checkpoint_snapshot_loaded) {
                for (const auto& txn : record.checkpoint_att) {
                    auto current = analysis.active_transaction_table.find(txn.first);
                    if (current == analysis.active_transaction_table.end() ||
                        current->second.last_lsn < txn.second.last_lsn) {
                        analysis.active_transaction_table[txn.first] = txn.second;
                    }
                    analysis.next_txn_id = std::max(analysis.next_txn_id,
                                                    txn.first + 1);
                }
                for (const auto& page : record.checkpoint_dpt) {
                    auto current = analysis.dirty_page_table.find(page.first);
                    if (current == analysis.dirty_page_table.end() ||
                        page.second < current->second) {
                        analysis.dirty_page_table[page.first] = page.second;
                    }
                }
                checkpoint_snapshot_loaded = true;
            }

            if (shouldRedoRecord(record)) {
                PageID page_id = record.page_id;
                LSN rec_lsn = record.lsn;
                if (analysis.dirty_page_table.find(page_id) ==
                    analysis.dirty_page_table.end()) {
                    analysis.dirty_page_table[page_id] = rec_lsn;
                }
            }
        }
        return analysis;
    }

    bool isLoserTxn(const RecoveryAnalysis::ActiveTransactionEntry& entry) const {
        return entry.status == RecoveryAnalysis::TxnStatus::RUNNING ||
               entry.status == RecoveryAnalysis::TxnStatus::ABORTING;
    }

    bool shouldRedoRecord(const LogRecord& record) const {
        return isRedoableRecord(record.type);
    }

    std::map<LSN, const LogRecord*> recordsByLSN(
        const std::vector<LogRecord>& records) const {
        std::map<LSN, const LogRecord*> by_lsn;
        for (const auto& record : records) {
            by_lsn[record.lsn] = &record;
        }
        return by_lsn;
    }

    void redoPass(const std::vector<LogRecord>& records,
                  const RecoveryAnalysis& analysis) {
        LSN min_rec_lsn = 0;
        for (const auto& [page_id, rec_lsn] : analysis.dirty_page_table) {
            if (min_rec_lsn == 0 || rec_lsn < min_rec_lsn) {
                min_rec_lsn = rec_lsn;
            }
        }

        if (min_rec_lsn == 0) {
            return;
        }

        for (const auto& record : records) {
            if (!shouldRedoRecord(record)) {
                continue;
            }
            if (record.lsn < min_rec_lsn) {
                continue;
            }

            auto dirty_page = analysis.dirty_page_table.find(record.page_id);
            if (dirty_page == analysis.dirty_page_table.end() ||
                record.lsn < dirty_page->second) {
                continue;
            }

            auto& page = buffer_manager.getPage(record.page_id);
            if (page->getPageLSN() >= record.lsn) {
                continue;
            }

            applyRedo(record);
        }
    }

    void undoPass(const std::vector<LogRecord>& records,
                  const RecoveryAnalysis& analysis) {
        auto by_lsn = recordsByLSN(records);
        std::map<int, LSN> last_lsn_by_txn;
        std::map<LSN, int> to_undo;

        for (const auto& txn : analysis.active_transaction_table) {
            int txn_id = txn.first;
            const auto& entry = txn.second;

            if (entry.status == RecoveryAnalysis::TxnStatus::COMMITTING) {
                appendRecoveryRecord(txn_id,
                                     LogRecordType::END,
                                     entry.last_lsn);
                continue;
            }

            if (!isLoserTxn(entry)) {
                continue;
            }

            LSN txn_last_lsn = entry.last_lsn;
            if (entry.status != RecoveryAnalysis::TxnStatus::ABORTING) {
                LSN abort_lsn = appendRecoveryRecord(txn_id,
                                                     LogRecordType::ABORT,
                                                     txn_last_lsn);
                txn_last_lsn = abort_lsn;
            }
            last_lsn_by_txn[txn_id] = txn_last_lsn;
            if (entry.last_lsn != 0) {
                to_undo[entry.last_lsn] = txn_id;
            }
        }

        while (!to_undo.empty()) {
            auto next = std::prev(to_undo.end());
            LSN lsn = next->first;
            int txn_id = next->second;
            to_undo.erase(next);

            auto record_it = by_lsn.find(lsn);
            if (record_it == by_lsn.end()) {
                throw std::runtime_error("Missing log record in toUndo set.");
            }

            const auto& record = *record_it->second;
            if (record.type == LogRecordType::CLR) {
                if (record.undo_next_lsn != 0) {
                    to_undo[record.undo_next_lsn] = txn_id;
                }
                continue;
            }

            if (isTupleChangeRecord(record.type)) {
                LogRecord clr = makeClrRecord(txn_id,
                                              last_lsn_by_txn[txn_id],
                                              record);
                LSN clr_lsn = log_manager.append(clr);
                last_lsn_by_txn[txn_id] = clr_lsn;
                rememberDirtyPage(clr.page_id, clr_lsn);
                applyRedo(clr);
            }

            if (record.prev_lsn != 0) {
                to_undo[record.prev_lsn] = txn_id;
            }
        }

        for (const auto& loser : last_lsn_by_txn) {
            appendRecoveryRecord(loser.first,
                                 LogRecordType::END,
                                 loser.second);
        }
    }

    LSN appendRecoveryRecord(int txn_id,
                             LogRecordType type,
                             LSN prev_lsn) {
        LogRecord record{type, txn_id};
        record.prev_lsn = prev_lsn;
        LSN record_lsn = log_manager.append(record);
        forceLogUpTo(record_lsn);
        return record_lsn;
    }

    LogRecord makeClrRecord(int txn_id,
                            LSN prev_lsn,
                            const LogRecord& undone_record) {
        if (!isTupleChangeRecord(undone_record.type)) {
            throw std::runtime_error("CLR requested for non-update log record.");
        }

        LogRecord clr{LogRecordType::CLR, txn_id};
        clr.prev_lsn = prev_lsn;
        clr.undo_next_lsn = undone_record.prev_lsn;
        clr.undo_type = undone_record.type;
        clr.table_id = undone_record.table_id;
        clr.page_id = undone_record.page_id;
        clr.slot_id = undone_record.slot_id;
        if (undone_record.type != LogRecordType::INSERT) {
            if (!undone_record.before_tuple) {
                throw std::runtime_error("Cannot build CLR without before image.");
            }
            clr.after_tuple = undone_record.before_tuple->clone();
        }
        return clr;
    }

    void applyRedo(const LogRecord& record) {
        applyRecordToPage(record);
        buffer_manager.flushPage(record.page_id);
    }

    void applyRecordToPage(const LogRecord& record) {
        auto& page = buffer_manager.getPage(record.page_id);
        if (page->getTableId() != record.table_id) {
            throw std::runtime_error("WAL redo page ownership mismatch.");
        }

        if (record.type == LogRecordType::CLR) {
            if (record.undo_type == LogRecordType::INSERT) {
                page->deleteTuple(record.slot_id);
            } else if (!record.after_tuple ||
                       !page->putTupleAtSlot(record.slot_id,
                                             record.after_tuple->clone())) {
                throw std::runtime_error("Unable to redo CLR operation.");
            }
            page->setPageLSN(std::max(page->getPageLSN(), record.lsn));
            return;
        }

        if (record.type == LogRecordType::DELETE) {
            page->deleteTuple(record.slot_id);
        } else if (!record.after_tuple ||
                   !page->putTupleAtSlot(record.slot_id,
                                         record.after_tuple->clone())) {
            throw std::runtime_error("Unable to redo WAL operation.");
        }
        page->setPageLSN(std::max(page->getPageLSN(), record.lsn));
    }

};

class PageManager {
private:
    BufferManager& buffer_manager;
    RecoveryManager& recovery_manager;

public:
    PageManager(BufferManager& buffer_manager,
                RecoveryManager& recovery_manager)
        : buffer_manager(buffer_manager),
          recovery_manager(recovery_manager) {}

    void resetRecoveryLog() {
        recovery_manager.resetLog();
    }

    SlottedPage& readPage(TableId table_id,
                          const std::string& table_name,
                          PageID page_id) {
        return checkedPage(table_id, table_name, page_id);
    }

    SlottedPage& writePage(TableId table_id,
                           const std::string& table_name,
                           PageID page_id) {
        return checkedPage(table_id, table_name, page_id);
    }

    bool recoveryActive() const {
        return recovery_manager.isActive();
    }

    void stageUpdate(TableId table_id,
                     const std::string& table_name,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple,
                     std::unique_ptr<Tuple> after_tuple) {
        checkedPage(table_id, table_name, page_id);
        recovery_manager.stageUpdate(table_id,
                                     page_id,
                                     slot_id,
                                     std::move(before_tuple),
                                     std::move(after_tuple));
    }

    void stageInsert(TableId table_id,
                     const std::string& table_name,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> after_tuple) {
        checkedPage(table_id, table_name, page_id);
        recovery_manager.stageInsert(table_id,
                                     page_id,
                                     slot_id,
                                     std::move(after_tuple));
    }

    void stageDelete(TableId table_id,
                     const std::string& table_name,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple) {
        checkedPage(table_id, table_name, page_id);
        recovery_manager.stageDelete(table_id,
                                     page_id,
                                     slot_id,
                                     std::move(before_tuple));
    }

    PageID nextPage(TableId table_id,
                    const std::string& table_name,
                    PageID page_id) {
        SlottedPage& page = checkedPage(table_id, table_name, page_id);
        PageID next_page_id = page.getNextPage();
        return next_page_id;
    }

    void flushWritePage(TableId, PageID page_id) {
        if (recovery_manager.isActive()) {
            if (!recovery_manager.shouldCrashAt(CrashPoint::AFTER_STEAL_PAGE_FLUSH)) {
                return;
            }
            buffer_manager.flushPage(page_id, "uncommitted STEAL");
            recovery_manager.crashIfRequested(
                CrashPoint::AFTER_STEAL_PAGE_FLUSH,
                "Simulated crash after uncommitted page " +
                std::to_string(page_id) +
                " reached disk via STEAL, before COMMIT"
            );
            return;
        }
        buffer_manager.flushPage(page_id);
    }

    PageID allocatePage(TableId table_id) {
        auto page_id = recovery_manager.allocatePhysicalPage(table_id);
        auto& page = buffer_manager.getPage(page_id);
        page->setTableId(table_id);
        page->setNextPage(INVALID_PAGE_ID);
        buffer_manager.flushPage(page_id);
        return page_id;
    }

private:
    SlottedPage& checkedPage(TableId table_id,
                             const std::string& table_name,
                             PageID physical_page_id) {
        auto& page = buffer_manager.getPage(physical_page_id);
        if (page->getTableId() != table_id) {
            throw std::runtime_error("Page ownership mismatch for table: " + table_name);
        }
        return *page;
    }
};

// One column in a table schema.
struct ColumnSchema {
    std::string name;
    FieldType type;
};

// Ordered list of columns for a table.
struct TableSchema {
    std::vector<ColumnSchema> columns;

    std::optional<size_t> findColumnIndex(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    int getColumnIndex(const std::string& name) const {
        auto index = findColumnIndex(name);
        if (index) {
            return static_cast<int>(*index);
        }
        throw std::runtime_error("Unknown column: " + name);
    }
};

struct TextUtil {
    static std::string trim(const std::string& input);
    static std::vector<std::string> split(const std::string& input, char delimiter);
};


// Catalog-owned table description.
struct TableMetadata {
    TableId table_id;
    std::string name;
    TableSchema schema;
    PageID first_page = INVALID_PAGE_ID;
};

// Runtime handle for one table's heap pages; owns no catalog metadata.
class TableHeap {
private:
    TableMetadata& metadata;
    PageManager& page_manager;

public:
    TableHeap(TableMetadata& metadata, PageManager& page_manager)
        : metadata(metadata),
          page_manager(page_manager) {}

    PageID firstPage() const {
        return metadata.first_page;
    }

    PageID nextPage(PageID page_id) {
        return page_manager.nextPage(metadata.table_id, metadata.name, page_id);
    }

    SlottedPage& readPage(PageID page_id) {
        return page_manager.readPage(metadata.table_id, metadata.name, page_id);
    }

    SlottedPage& writePage(PageID page_id) {
        return page_manager.writePage(metadata.table_id, metadata.name, page_id);
    }

    std::unique_ptr<Tuple> getTuple(PageID page_id, size_t slot) {
        return readPage(page_id).getTuple(slot);
    }

    void updateTuple(PageID page_id, size_t slot, std::unique_ptr<Tuple> tuple) {
        if (page_manager.recoveryActive()) {
            auto before_tuple = getTuple(page_id, slot);
            if (!before_tuple) {
                throw std::runtime_error("UPDATE target tuple is missing.");
            }
            page_manager.stageUpdate(metadata.table_id,
                                     metadata.name,
                                     page_id,
                                     slot,
                                     before_tuple->clone(),
                                     tuple->clone());
        }

        if (!writePage(page_id).updateTuple(slot, std::move(tuple))) {
            throw std::runtime_error("Updated tuple no longer fits in its slot.");
        }
        flushWritePage(page_id);
    }

    void deleteTuple(PageID page_id, size_t slot) {
        if (page_manager.recoveryActive()) {
            auto before_tuple = getTuple(page_id, slot);
            if (!before_tuple) {
                throw std::runtime_error("DELETE target tuple is missing.");
            }
            page_manager.stageDelete(metadata.table_id,
                                     metadata.name,
                                     page_id,
                                     slot,
                                     before_tuple->clone());
        }
        writePage(page_id).deleteTuple(slot);
        flushWritePage(page_id);
    }

    bool addTuple(std::unique_ptr<Tuple> tuple) {
        PageID previous_page_id = INVALID_PAGE_ID;
        for (PageID page_id = firstPage();
             page_id != INVALID_PAGE_ID;
             page_id = nextPage(page_id)) {
            if (page_manager.recoveryActive()) {
                auto& page = writePage(page_id);
                auto slot = page.addTupleAndReturnSlot(tuple->clone());
                if (slot) {
                    page_manager.stageInsert(metadata.table_id,
                                             metadata.name,
                                             page_id,
                                             *slot,
                                             tuple->clone());
                    flushWritePage(page_id);
                    return true;
                }
                previous_page_id = page_id;
                continue;
            }

            if (writePage(page_id).addTuple(tuple->clone())) {
                flushWritePage(page_id);
                return true;
            }
            previous_page_id = page_id;
        }

        if (page_manager.recoveryActive()) {
            throw std::runtime_error(
                "INSERT requiring a new page inside a WAL transaction is not supported in v61."
            );
        }

        PageID page_id = allocatePageAfter(previous_page_id);
        auto& page = writePage(page_id);
        if (!page.addTuple(std::move(tuple))) {
            return false;
        }

        page_manager.flushWritePage(metadata.table_id, page_id);
        return true;
    }

    bool appendTuple(std::unique_ptr<Tuple> tuple, PageID& append_page_id) {
        if (page_manager.recoveryActive()) {
            return addTuple(std::move(tuple));
        }

        if (append_page_id == INVALID_PAGE_ID) {
            append_page_id = firstPage();
        }

        PageID previous_page_id = INVALID_PAGE_ID;
        for (PageID page_id = append_page_id;
             page_id != INVALID_PAGE_ID;
             page_id = nextPage(page_id)) {
            if (writePage(page_id).addTuple(tuple->clone())) {
                flushWritePage(page_id);
                append_page_id = page_id;
                return true;
            }
            previous_page_id = page_id;
        }

        PageID page_id = allocatePageAfter(previous_page_id);
        append_page_id = page_id;
        auto& page = writePage(page_id);
        if (!page.addTuple(std::move(tuple))) {
            return false;
        }

        page_manager.flushWritePage(metadata.table_id, page_id);
        return true;
    }

    PageID allocatePageAfter(PageID previous_page_id) {
        PageID page_id = page_manager.allocatePage(metadata.table_id);

        if (previous_page_id != INVALID_PAGE_ID) {
            writePage(previous_page_id).setNextPage(page_id);
            flushWritePage(previous_page_id);
        } else {
            metadata.first_page = page_id;
        }
        return page_id;
    }

    std::vector<std::unique_ptr<Tuple>> readAllTuples() {
        std::vector<std::unique_ptr<Tuple>> tuples;

        for (PageID page_id = firstPage();
             page_id != INVALID_PAGE_ID;
             page_id = nextPage(page_id)) {
            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                auto tuple = getTuple(page_id, slot_itr);
                if (tuple) {
                    tuples.push_back(std::move(tuple));
                }
            }
        }

        return tuples;
    }

private:
    void flushWritePage(PageID page_id) {
        page_manager.flushWritePage(metadata.table_id, page_id);
    }
};

// Page 0 bootstraps catalog tables; the catalog tables are ordinary heaps.
class Catalog {
private:
    BufferManager& buffer_manager;
    PageManager& page_manager;
    TableId next_table_id = FIRST_USER_TABLE_ID;
    std::unordered_map<std::string, TableMetadata> tables_by_name;

public:
    Catalog(BufferManager& manager, PageManager& page_manager)
        : buffer_manager(manager),
          page_manager(page_manager) {}

    void load() {
        auto& page = buffer_manager.getPage(CATALOG_PAGE_ID);
        std::string bootstrap_text = readPageText(page);
        auto first_line_end = bootstrap_text.find('\n');
        std::string first_line = first_line_end == std::string::npos
            ? bootstrap_text
            : bootstrap_text.substr(0, first_line_end);
        if (first_line != BOOTSTRAP_MAGIC) {
            if (buffer_manager.getNumPages() > 1) {
                throw std::runtime_error(database_filename + " is not a valid BuzzDB file.");
            }
            page_manager.resetRecoveryLog();
            initializeNewDatabase();
            return;
        }

        auto roots = loadBootstrap(bootstrap_text);
        installSystemTables(roots.first, roots.second);
        loadUserTables();
    }

    bool createTable(const std::string& name, TableSchema schema) {
        auto it = tables_by_name.find(name);
        if (it != tables_by_name.end()) {
            return false;
        }

        TableId table_id = next_table_id++;
        PageID first_page = allocateFirstPage(table_id);
        TableMetadata metadata{
            table_id, name, std::move(schema), first_page
        };
        auto& cached_metadata = cacheTable(std::move(metadata));
        persistTableRecord(cached_metadata);
        persistColumns(cached_metadata);
        return true;
    }

    bool hasTable(const std::string& name) const {
        return tables_by_name.find(name) != tables_by_name.end();
    }

    bool empty() const {
        for (const auto& entry : tables_by_name) {
            if (entry.second.table_id >= FIRST_USER_TABLE_ID) {
                return false;
            }
        }
        return true;
    }

    TableMetadata& getTable(const std::string& name) {
        auto it = tables_by_name.find(name);
        if (it == tables_by_name.end()) {
            throw std::runtime_error("Unknown table: " + name);
        }
        return it->second;
    }

    TableMetadata& getTable(TableId table_id) {
        for (auto& entry : tables_by_name) {
            if (entry.second.table_id == table_id) {
                return entry.second;
            }
        }
        throw std::runtime_error(
            "Unknown table id: " + std::to_string(table_id)
        );
    }

private:
    static std::string readPageText(const std::unique_ptr<SlottedPage>& page) {
        size_t length = 0;
        while (length < PAGE_SIZE && page->page_data[length] != '\0') {
            length++;
        }
        return std::string(page->page_data.get(), length);
    }

    PageID allocateFirstPage(TableId table_id) {
        return page_manager.allocatePage(table_id);
    }

    TableMetadata& cacheTable(TableMetadata metadata) {
        std::string table_name = metadata.name;
        auto result = tables_by_name.insert_or_assign(table_name, std::move(metadata));
        return result.first->second;
    }

    static TableSchema tablesTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"table_name", STRING},
            {"first_page", INT}
        }};
    }

    static TableSchema columnsTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"column_id", INT},
            {"column_name", STRING},
            {"column_type", INT}
        }};
    }

    void initializeNewDatabase() {
        PageID tables_first_page = allocateFirstPage(SYS_TABLES_ID);
        PageID columns_first_page = allocateFirstPage(SYS_COLUMNS_ID);
        writeBootstrap(tables_first_page, columns_first_page);
        installSystemTables(tables_first_page, columns_first_page);

        auto& tables_metadata = getTable("__tables");
        auto& columns_metadata = getTable("__columns");
        persistTableRecord(tables_metadata);
        persistColumns(tables_metadata);
        persistTableRecord(columns_metadata);
        persistColumns(columns_metadata);
    }

    void writeBootstrap(PageID tables_first_page, PageID columns_first_page) {
        std::ostringstream output;
        output << BOOTSTRAP_MAGIC << "\n"
               << "TABLES|" << tables_first_page << "\n"
               << "COLUMNS|" << columns_first_page << "\n";

        std::string payload = output.str();
        if (payload.size() >= PAGE_SIZE) {
            throw std::runtime_error("Bootstrap page is full.");
        }

        auto& page = buffer_manager.getPage(CATALOG_PAGE_ID);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), payload.data(), payload.size());
        buffer_manager.flushPage(CATALOG_PAGE_ID);
    }

    std::pair<PageID, PageID> loadBootstrap(const std::string& bootstrap_text) {
        std::istringstream input(bootstrap_text);
        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("Bootstrap page is empty.");
        }
        if (line != BOOTSTRAP_MAGIC) {
            throw std::runtime_error(database_filename + " is not a valid BuzzDB file.");
        }

        PageID tables_first_page = INVALID_PAGE_ID;
        PageID columns_first_page = INVALID_PAGE_ID;
        while (std::getline(input, line)) {
            auto tokens = TextUtil::split(line, '|');
            if (tokens.size() != 2) {
                continue;
            }
            if (tokens[0] == "TABLES") {
                tables_first_page = static_cast<PageID>(std::stoi(tokens[1]));
            } else if (tokens[0] == "COLUMNS") {
                columns_first_page = static_cast<PageID>(std::stoi(tokens[1]));
            }
        }

        if (tables_first_page == INVALID_PAGE_ID ||
            columns_first_page == INVALID_PAGE_ID) {
            throw std::runtime_error("Bootstrap page is missing catalog roots.");
        }
        return {tables_first_page, columns_first_page};
    }

    void installSystemTables(PageID tables_first_page, PageID columns_first_page) {
        tables_by_name.clear();
        cacheTable({
            SYS_TABLES_ID, "__tables", tablesTableSchema(), tables_first_page
        });
        cacheTable({
            SYS_COLUMNS_ID, "__columns", columnsTableSchema(), columns_first_page
        });
    }

    void insertSystemTuple(const std::string& table_name, std::unique_ptr<Tuple> tuple) {
        auto& metadata = getTable(table_name);
        TableHeap table(metadata, page_manager);
        if (!table.addTuple(std::move(tuple))) {
            throw std::runtime_error("Unable to insert catalog tuple.");
        }
    }

    void persistTableRecord(const TableMetadata& metadata) {
        auto tuple = std::make_unique<Tuple>();
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.table_id)));
        tuple->addField(std::make_unique<Field>(metadata.name));
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.first_page)));
        insertSystemTuple("__tables", std::move(tuple));
    }

    void persistColumns(const TableMetadata& metadata) {
        for (size_t column_id = 0; column_id < metadata.schema.columns.size(); column_id++) {
            const auto& column = metadata.schema.columns[column_id];
            auto tuple = std::make_unique<Tuple>();
            tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.table_id)));
            tuple->addField(std::make_unique<Field>(static_cast<int>(column_id)));
            tuple->addField(std::make_unique<Field>(column.name));
            tuple->addField(std::make_unique<Field>(static_cast<int>(column.type)));
            insertSystemTuple("__columns", std::move(tuple));
        }
    }

    void loadUserTables() {
        auto& tables_metadata = getTable("__tables");
        TableHeap tables_heap(tables_metadata, page_manager);
        std::vector<TableMetadata> user_tables;

        for (auto& tuple : tables_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            next_table_id = std::max(
                next_table_id,
                static_cast<TableId>(table_id + 1)
            );
            if (table_id < FIRST_USER_TABLE_ID) {
                continue;
            }

            user_tables.push_back({
                table_id,
                tuple->fields[1]->asString(),
                TableSchema{},
                static_cast<PageID>(tuple->fields[2]->asInt())
            });
        }

        for (auto& metadata : user_tables) {
            loadColumns(metadata);
            cacheTable(std::move(metadata));
        }
    }

    void loadColumns(TableMetadata& metadata) {
        auto& columns_metadata = getTable("__columns");
        TableHeap columns_heap(columns_metadata, page_manager);
        std::map<int, ColumnSchema> columns_by_id;

        for (auto& tuple : columns_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            if (table_id != metadata.table_id) {
                continue;
            }

            int column_id = tuple->fields[1]->asInt();
            auto column_name = tuple->fields[2]->asString();
            auto column_type = static_cast<FieldType>(tuple->fields[3]->asInt());
            columns_by_id[column_id] = {column_name, column_type};
        }

        for (const auto& entry : columns_by_id) {
            metadata.schema.columns.push_back(entry.second);
        }
    }
};

struct IndexSpec {
    std::string table_name;
    std::string column_name;
};

class HashIndex {
private:
    std::unordered_map<IndexKey, std::vector<RecordId>, IndexKeyHasher> entries;

public:
    void clear() {
        entries.clear();
    }

    void insert(const Field& key, const RecordId& record_id) {
        auto& records = entries[IndexKey(key)];
        records.push_back(record_id);
    }

    const std::vector<RecordId>* lookup(const Field& key) const {
        auto it = entries.find(IndexKey(key));
        if (it == entries.end()) {
            return nullptr;
        }
        return &it->second;
    }

};

class IndexManager {
private:
    struct BuildTarget {
        IndexDescriptor descriptor;
    };

    Catalog& catalog;
    PageManager& page_manager;
    std::map<IndexDescriptor, HashIndex> indexes;

public:
    IndexManager(Catalog& catalog, PageManager& page_manager)
        : catalog(catalog), page_manager(page_manager) {}

    void buildIndexes(const std::vector<IndexSpec>& specs) {
        std::map<std::string, std::vector<BuildTarget>> targets_by_table;

        for (const auto& spec : specs) {
            if (!catalog.hasTable(spec.table_name)) {
                throw std::runtime_error(
                    "Cannot build index on missing table: " + spec.table_name
                );
            }

            auto& metadata = catalog.getTable(spec.table_name);
            auto column_index = metadata.schema.findColumnIndex(spec.column_name);
            if (!column_index) {
                throw std::runtime_error(
                    "Cannot build index on missing column: " +
                    spec.table_name + "." + spec.column_name
                );
            }

            targets_by_table[spec.table_name].push_back({
                {metadata.table_id, *column_index}
            });
        }

        for (auto& entry : targets_by_table) {
            auto& metadata = catalog.getTable(entry.first);
            for (const auto& target : entry.second) {
                indexes[target.descriptor].clear();
            }

            TableHeap table_heap(metadata, page_manager);
            for (PageID page_id = table_heap.firstPage();
                 page_id != INVALID_PAGE_ID;
                 page_id = table_heap.nextPage(page_id)) {
                for (size_t slot = 0; slot < MAX_SLOTS; slot++) {
                    auto tuple = table_heap.getTuple(page_id, slot);
                    if (!tuple) {
                        continue;
                    }

                    for (const auto& target : entry.second) {
                        size_t column_index = target.descriptor.column_index;
                        if (column_index >= tuple->fields.size()) {
                            continue;
                        }
                        indexes[target.descriptor].insert(
                            *tuple->fields[column_index],
                            {metadata.table_id, page_id, slot}
                        );
                    }
                }
            }

        }
    }

    bool hasIndex(const IndexDescriptor& descriptor) const {
        return indexes.find(descriptor) != indexes.end();
    }

    const HashIndex& index(const IndexDescriptor& descriptor) const {
        auto it = indexes.find(descriptor);
        if (it == indexes.end()) {
            throw std::runtime_error("Index is not built.");
        }
        return it->second;
    }

    const std::vector<RecordId>* lookup(const IndexDescriptor& descriptor,
                                        const Field& value) const {
        return index(descriptor).lookup(value);
    }
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
    PageID currentPageId = INVALID_PAGE_ID;
    size_t currentSlotIndex = 0;
    std::unique_ptr<Tuple> currentTuple;
    size_t tuple_count = 0;

public:
    ScanOperator(TableHeap& tableHeap) : tableHeap(tableHeap) {}

    void open() override {
        currentPageId = tableHeap.firstPage();
        currentSlotIndex = 0;
        currentTuple.reset();
    }

    bool next() override {
        loadNextTuple();
        return currentTuple != nullptr;
    }

    void close() override {
        currentPageId = INVALID_PAGE_ID;
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
        while (currentPageId != INVALID_PAGE_ID) {
            if (currentSlotIndex >= MAX_SLOTS) {
                currentSlotIndex = 0;
            }

            while (currentSlotIndex < MAX_SLOTS) {
                auto tuple = tableHeap.getTuple(currentPageId, currentSlotIndex);
                currentSlotIndex++;
                if (tuple) {
                    currentTuple = std::move(tuple);
                    tuple_count++;
                    return;
                }
            }

            currentPageId = tableHeap.nextPage(currentPageId);
        }

        currentTuple.reset();
    }
};

class IPredicate {
public:
    virtual ~IPredicate() = default;
    virtual bool check(const std::vector<std::unique_ptr<Field>>& tupleFields) const = 0;
};

std::vector<std::unique_ptr<Field>> cloneFields(
        const std::vector<std::unique_ptr<Field>>& fields) {
    std::vector<std::unique_ptr<Field>> cloned;
    for (const auto& field : fields) {
        cloned.push_back(field->clone());
    }
    return cloned;
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
    std::vector<std::unique_ptr<Field>> currentOutput;

public:
    SelectOperator(Operator& input, std::unique_ptr<IPredicate> predicate)
        : UnaryOperator(input), predicate(std::move(predicate)), has_next(false) {}

    void open() override {
        input->open();
        has_next = false;
        currentOutput.clear();
    }

    bool next() override {
        while (input->next()) {
            const auto& output = input->getOutput();
            if (predicate->check(output)) {
                currentOutput.clear();
                for (const auto& field : output) {
                    currentOutput.push_back(field->clone());
                }
                has_next = true;
                return true;
            }
        }
        has_next = false;
        currentOutput.clear();
        return false;
    }

    void close() override {
        input->close();
        currentOutput.clear();
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (has_next) {
            std::vector<std::unique_ptr<Field>> outputCopy;
            for (const auto& field : currentOutput) {
                outputCopy.push_back(field->clone());
            }
            return outputCopy;
        } else {
            return {};
        }
    }
};

class ProjectionOperator : public UnaryOperator {
private:
    std::vector<size_t> projected_attrs;
    std::vector<std::unique_ptr<Field>> currentOutput;
    bool has_next = false;

public:
    ProjectionOperator(Operator& input, std::vector<size_t> projected_attrs)
        : UnaryOperator(input), projected_attrs(std::move(projected_attrs)) {}

    void open() override {
        input->open();
        currentOutput.clear();
        has_next = false;
    }

    bool next() override {
        if (!input->next()) {
            currentOutput.clear();
            has_next = false;
            return false;
        }

        auto input_tuple = input->getOutput();
        currentOutput.clear();
        for (auto attr_index : projected_attrs) {
            if (attr_index >= input_tuple.size()) {
                throw std::runtime_error("Projection attribute index out of range.");
            }
            currentOutput.push_back(input_tuple[attr_index]->clone());
        }
        has_next = true;
        return true;
    }

    void close() override {
        input->close();
        currentOutput.clear();
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(currentOutput);
    }
};

class SortOperator : public UnaryOperator {
private:
    size_t sort_attr_index;
    std::vector<std::vector<std::unique_ptr<Field>>> sorted_tuples;
    size_t tuple_index = 0;

public:
    SortOperator(Operator& input, size_t sort_attr_index)
        : UnaryOperator(input), sort_attr_index(sort_attr_index) {}

    void open() override {
        input->open();
        sorted_tuples.clear();
        tuple_index = 0;
        while (input->next()) {
            auto tuple = input->getOutput();
            if (sort_attr_index >= tuple.size()) {
                throw std::runtime_error("Sort attribute index out of range.");
            }
            sorted_tuples.push_back(std::move(tuple));
        }
        input->close();

        std::stable_sort(
            sorted_tuples.begin(),
            sorted_tuples.end(),
            [&](const auto& left, const auto& right) {
                return fieldLess(*left[sort_attr_index],
                                 *right[sort_attr_index]);
            }
        );
    }

    bool next() override {
        if (tuple_index >= sorted_tuples.size()) {
            return false;
        }
        tuple_index++;
        return true;
    }

    void close() override {
        sorted_tuples.clear();
        tuple_index = 0;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (tuple_index == 0 || tuple_index > sorted_tuples.size()) {
            return {};
        }
        return cloneFields(sorted_tuples[tuple_index - 1]);
    }
};

class NestedLoopJoinOperator : public BinaryOperator {
private:
    size_t left_attr_index;
    size_t right_attr_index;
    std::vector<std::vector<std::unique_ptr<Field>>> rightTuples;
    std::vector<std::unique_ptr<Field>> currentLeftTuple;
    std::vector<std::unique_ptr<Field>> currentOutput;
    size_t rightTupleIndex = 0;
    bool has_left_tuple = false;
    bool has_next = false;

public:
    NestedLoopJoinOperator(Operator& left,
                           Operator& right,
                           size_t left_attr_index,
                           size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->open();
        input_right->open();
        rightTuples.clear();
        while (input_right->next()) {
            rightTuples.push_back(input_right->getOutput());
        }
        input_right->close();

        currentLeftTuple.clear();
        currentOutput.clear();
        rightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    bool next() override {
        currentOutput.clear();
        has_next = false;

        while (true) {
            if (!has_left_tuple) {
                if (!input_left->next()) {
                    return false;
                }
                currentLeftTuple = input_left->getOutput();
                rightTupleIndex = 0;
                has_left_tuple = true;
            }

            while (rightTupleIndex < rightTuples.size()) {
                const auto& right_tuple = rightTuples[rightTupleIndex++];
                if (left_attr_index >= currentLeftTuple.size() ||
                    right_attr_index >= right_tuple.size()) {
                    throw std::runtime_error(
                        "Nested-loop join attribute index out of range."
                    );
                }
                if (!(*currentLeftTuple[left_attr_index] ==
                      *right_tuple[right_attr_index])) {
                    continue;
                }

                currentOutput = cloneFields(currentLeftTuple);
                for (const auto& field : right_tuple) {
                    currentOutput.push_back(field->clone());
                }
                has_next = true;
                return true;
            }

            has_left_tuple = false;
        }
    }

    void close() override {
        input_left->close();
        rightTuples.clear();
        currentLeftTuple.clear();
        currentOutput.clear();
        rightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(currentOutput);
    }
};

class SortMergeJoinOperator : public BinaryOperator {
private:
    size_t left_attr_index;
    size_t right_attr_index;
    std::vector<std::vector<std::unique_ptr<Field>>> left_tuples;
    std::vector<std::vector<std::unique_ptr<Field>>> right_tuples;
    std::vector<std::vector<std::unique_ptr<Field>>> right_group;
    std::vector<std::unique_ptr<Field>> current_left_tuple;
    std::vector<std::unique_ptr<Field>> current_output;
    std::unique_ptr<Field> current_group_key;
    size_t left_index = 0;
    size_t right_index = 0;
    size_t right_group_output_index = 0;
    bool has_next = false;

    static void requireAttr(const std::vector<std::unique_ptr<Field>>& tuple,
                            size_t attr_index,
                            const std::string& side) {
        if (attr_index >= tuple.size()) {
            throw std::runtime_error(
                "Sort-merge join " + side + " attribute index out of range."
            );
        }
    }

    void loadRightGroupForKey(const Field& key) {
        if (current_group_key && *current_group_key == key) {
            return;
        }

        current_group_key = key.clone();
        right_group.clear();
        size_t scan = right_index;
        while (scan < right_tuples.size()) {
            requireAttr(right_tuples[scan], right_attr_index, "right");
            int cmp = compareFields(*right_tuples[scan][right_attr_index], key);
            if (cmp != 0) {
                break;
            }
            right_group.push_back(cloneFields(right_tuples[scan]));
            scan++;
        }
    }

public:
    SortMergeJoinOperator(Operator& left,
                          Operator& right,
                          size_t left_attr_index,
                          size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->open();
        input_right->open();
        left_tuples.clear();
        right_tuples.clear();
        while (input_left->next()) {
            auto tuple = input_left->getOutput();
            requireAttr(tuple, left_attr_index, "left");
            left_tuples.push_back(std::move(tuple));
        }
        while (input_right->next()) {
            auto tuple = input_right->getOutput();
            requireAttr(tuple, right_attr_index, "right");
            right_tuples.push_back(std::move(tuple));
        }
        input_left->close();
        input_right->close();

        left_index = 0;
        right_index = 0;
        right_group_output_index = 0;
        right_group.clear();
        current_left_tuple.clear();
        current_output.clear();
        current_group_key.reset();
        has_next = false;
    }

    bool next() override {
        current_output.clear();
        has_next = false;

        while (true) {
            if (!current_left_tuple.empty() &&
                right_group_output_index < right_group.size()) {
                current_output = cloneFields(current_left_tuple);
                for (const auto& field : right_group[right_group_output_index++]) {
                    current_output.push_back(field->clone());
                }
                has_next = true;
                return true;
            }

            current_left_tuple.clear();
            right_group_output_index = 0;
            if (left_index >= left_tuples.size()) {
                return false;
            }

            const auto& left_tuple = left_tuples[left_index++];
            requireAttr(left_tuple, left_attr_index, "left");
            const auto& left_key = *left_tuple[left_attr_index];

            while (right_index < right_tuples.size()) {
                requireAttr(right_tuples[right_index], right_attr_index, "right");
                if (compareFields(
                        *right_tuples[right_index][right_attr_index],
                        left_key) >= 0) {
                    break;
                }
                right_index++;
            }
            if (right_index >= right_tuples.size()) {
                return false;
            }

            int cmp = compareFields(
                left_key,
                *right_tuples[right_index][right_attr_index]
            );
            if (cmp < 0) {
                continue;
            }

            loadRightGroupForKey(left_key);
            if (right_group.empty()) {
                continue;
            }
            current_left_tuple = cloneFields(left_tuple);
        }
    }

    void close() override {
        left_tuples.clear();
        right_tuples.clear();
        right_group.clear();
        current_left_tuple.clear();
        current_output.clear();
        current_group_key.reset();
        left_index = 0;
        right_index = 0;
        right_group_output_index = 0;
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(current_output);
    }
};

class IndexNestedLoopJoinOperator : public Operator {
private:
    Operator& outer;
    TableHeap& inner_heap;
    const HashIndex& index;
    size_t outer_attr_index;
    size_t inner_attr_index;
    bool inner_output_first;
    std::unique_ptr<IPredicate> inner_predicate;
    std::vector<std::unique_ptr<Field>> current_outer_tuple;
    std::vector<std::unique_ptr<Field>> current_output;
    const std::vector<RecordId>* matching_records = nullptr;
    size_t matching_record_index = 0;
    bool has_outer_tuple = false;
    bool has_next = false;

public:
    IndexNestedLoopJoinOperator(
            Operator& outer,
            TableHeap& inner_heap,
            const HashIndex& index,
            size_t outer_attr_index,
            size_t inner_attr_index,
            bool inner_output_first,
            std::unique_ptr<IPredicate> inner_predicate)
        : outer(outer),
          inner_heap(inner_heap),
          index(index),
          outer_attr_index(outer_attr_index),
          inner_attr_index(inner_attr_index),
          inner_output_first(inner_output_first),
          inner_predicate(std::move(inner_predicate)) {}

    void open() override {
        outer.open();
        current_outer_tuple.clear();
        current_output.clear();
        matching_records = nullptr;
        matching_record_index = 0;
        has_outer_tuple = false;
        has_next = false;
    }

    bool next() override {
        current_output.clear();
        has_next = false;

        while (true) {
            if (!has_outer_tuple) {
                if (!outer.next()) {
                    return false;
                }
                current_outer_tuple = outer.getOutput();
                if (outer_attr_index >= current_outer_tuple.size()) {
                    throw std::runtime_error(
                        "Index nested-loop outer attribute index out of range."
                    );
                }
                matching_records = index.lookup(
                    *current_outer_tuple[outer_attr_index]
                );
                matching_record_index = 0;
                has_outer_tuple = true;
            }

            while (matching_records &&
                   matching_record_index < matching_records->size()) {
                const auto& record_id =
                    (*matching_records)[matching_record_index++];
                auto inner_tuple = inner_heap.getTuple(
                    record_id.page_id,
                    record_id.slot_id
                );
                if (!inner_tuple) {
                    continue;
                }
                if (inner_attr_index >= inner_tuple->fields.size()) {
                    throw std::runtime_error(
                        "Index nested-loop inner attribute index out of range."
                    );
                }
                if (!(*current_outer_tuple[outer_attr_index] ==
                      *inner_tuple->fields[inner_attr_index])) {
                    continue;
                }
                if (inner_predicate &&
                    !inner_predicate->check(inner_tuple->fields)) {
                    continue;
                }

                if (inner_output_first) {
                    current_output = cloneFields(inner_tuple->fields);
                    for (const auto& field : current_outer_tuple) {
                        current_output.push_back(field->clone());
                    }
                } else {
                    current_output = cloneFields(current_outer_tuple);
                    for (const auto& field : inner_tuple->fields) {
                        current_output.push_back(field->clone());
                    }
                }
                has_next = true;
                return true;
            }

            has_outer_tuple = false;
            matching_records = nullptr;
            matching_record_index = 0;
        }
    }

    void close() override {
        outer.close();
        current_outer_tuple.clear();
        current_output.clear();
        matching_records = nullptr;
        matching_record_index = 0;
        has_outer_tuple = false;
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(current_output);
    }
};

class HashJoinOperator : public BinaryOperator {
private:
    struct HashJoinKey {
        FieldType type;
        int int_value = 0;
        float float_value = 0.0f;
        std::string string_value;

        explicit HashJoinKey(const Field& field) : type(field.getType()) {
            switch (type) {
                case INT:
                    int_value = field.asInt();
                    break;
                case FLOAT:
                    float_value = field.asFloat();
                    break;
                case STRING:
                    string_value = field.asString();
                    break;
            }
        }

        bool operator==(const HashJoinKey& other) const {
            if (type != other.type) {
                return false;
            }

            switch (type) {
                case INT:
                    return int_value == other.int_value;
                case FLOAT:
                    return float_value == other.float_value;
                case STRING:
                    return string_value == other.string_value;
            }
            return false;
        }
    };

    struct HashJoinKeyHasher {
        std::size_t operator()(const HashJoinKey& key) const {
            switch (key.type) {
                case INT:
                    return std::hash<int>{}(key.int_value);
                case FLOAT:
                    return std::hash<float>{}(key.float_value);
                case STRING:
                    return std::hash<std::string>{}(key.string_value);
            }
            return 0;
        }
    };

    size_t left_attr_index;
    size_t right_attr_index;
    std::unordered_map<
        HashJoinKey,
        std::vector<std::vector<std::unique_ptr<Field>>>,
        HashJoinKeyHasher
    > hash_table;
    std::vector<std::unique_ptr<Field>> current_left_tuple;
    std::vector<std::unique_ptr<Field>> current_output;
    const std::vector<std::vector<std::unique_ptr<Field>>>* matching_right_tuples = nullptr;
    size_t matching_right_tuple_index = 0;
    bool has_next = false;

public:
    HashJoinOperator(Operator& left,
                     Operator& right,
                     size_t left_attr_index,
                     size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->open();
        input_right->open();
        hash_table.clear();

        while (input_right->next()) {
            auto right_tuple = input_right->getOutput();
            if (right_attr_index >= right_tuple.size()) {
                throw std::runtime_error("Hash join right attribute index out of range.");
            }
            hash_table[HashJoinKey(*right_tuple[right_attr_index])].push_back(
                std::move(right_tuple)
            );
        }
        input_right->close();

        current_left_tuple.clear();
        current_output.clear();
        matching_right_tuples = nullptr;
        matching_right_tuple_index = 0;
        has_next = false;
    }

    bool next() override {
        current_output.clear();
        has_next = false;

        while (true) {
            while (matching_right_tuples == nullptr ||
                   matching_right_tuple_index >= matching_right_tuples->size()) {
                if (!input_left->next()) {
                    return false;
                }

                current_left_tuple = input_left->getOutput();
                if (left_attr_index >= current_left_tuple.size()) {
                    throw std::runtime_error("Hash join left attribute index out of range.");
                }

                auto matches = hash_table.find(
                    HashJoinKey(*current_left_tuple[left_attr_index])
                );
                if (matches == hash_table.end()) {
                    matching_right_tuples = nullptr;
                    matching_right_tuple_index = 0;
                    continue;
                }

                matching_right_tuples = &matches->second;
                matching_right_tuple_index = 0;
            }

            const auto& right_tuple =
                (*matching_right_tuples)[matching_right_tuple_index++];
            current_output = cloneFields(current_left_tuple);
            for (const auto& field : right_tuple) {
                current_output.push_back(field->clone());
            }
            has_next = true;
            return true;
        }
    }

    void close() override {
        input_left->close();
        hash_table.clear();
        current_left_tuple.clear();
        current_output.clear();
        matching_right_tuples = nullptr;
        matching_right_tuple_index = 0;
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(current_output);
    }
};

enum class AggrFuncType { COUNT, MAX, MIN, SUM };

struct AggrFunc {
    AggrFuncType func;
    size_t attr_index;
};

class HashAggregationOperator : public UnaryOperator {
private:
    std::vector<size_t> group_by_attrs;
    std::vector<AggrFunc> aggr_funcs;
    std::vector<Tuple> output_tuples;
    size_t output_tuples_index = 0;

    struct FieldVectorHasher {
        std::size_t operator()(const std::vector<Field>& fields) const {
            std::size_t hash = 0;
            for (const auto& field : fields) {
                std::hash<std::string> hasher;
                std::size_t fieldHash = 0;

                switch (field.type) {
                    case INT: {
                        int value = *reinterpret_cast<const int*>(field.data.get());
                        fieldHash = hasher(std::to_string(value));
                        break;
                    }
                    case FLOAT: {
                        float value = *reinterpret_cast<const float*>(field.data.get());
                        fieldHash = hasher(std::to_string(value));
                        break;
                    }
                    case STRING: {
                        std::string value(field.data.get(), field.data_length - 1);
                        fieldHash = hasher(value);
                        break;
                    }
                    default:
                        throw std::runtime_error("Unsupported field type for hashing.");
                }

                hash ^= fieldHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };


public:
    HashAggregationOperator(Operator& input, std::vector<size_t> group_by_attrs, std::vector<AggrFunc> aggr_funcs)
        : UnaryOperator(input), group_by_attrs(group_by_attrs), aggr_funcs(aggr_funcs) {}

    void open() override {
        input->open();
        output_tuples_index = 0;
        output_tuples.clear();

        std::unordered_map<std::vector<Field>, std::vector<Field>, FieldVectorHasher> hash_table;

        while (input->next()) {
            const auto& tuple = input->getOutput();

            std::vector<Field> group_keys;
            for (auto& index : group_by_attrs) {
                group_keys.push_back(*tuple[index]);
            }

            if (!hash_table.count(group_keys)) {
                std::vector<Field> aggr_values(aggr_funcs.size(), Field(0));
                hash_table[group_keys] = aggr_values;
            }

            auto& aggr_values = hash_table[group_keys];
            for (size_t i = 0; i < aggr_funcs.size(); ++i) {
                aggr_values[i] = updateAggregate(aggr_funcs[i], aggr_values[i], *tuple[aggr_funcs[i].attr_index]);
            }
        }

        for (const auto& entry : hash_table) {
            const auto& group_keys = entry.first;
            const auto& aggr_values = entry.second;
            Tuple output_tuple;
            for (const auto& key : group_keys) {
                output_tuple.addField(std::make_unique<Field>(key));
            }
            for (const auto& value : aggr_values) {
                output_tuple.addField(std::make_unique<Field>(value));
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
            return outputCopy;
        }

        const auto& currentTuple = output_tuples[output_tuples_index - 1];

        for (const auto& field : currentTuple.fields) {
            outputCopy.push_back(field->clone());
        }

        return outputCopy;
    }


private:

    Field updateAggregate(const AggrFunc& aggrFunc, const Field& currentAggr, const Field& newValue) {
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
                if (currentAggr.getType() != newValue.getType()) {
                    throw std::runtime_error("Mismatched Field types in aggregation.");
                }
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
                if (currentAggr.getType() != newValue.getType()) {
                    throw std::runtime_error("Mismatched Field types in aggregation.");
                }
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
                if (currentAggr.getType() != newValue.getType()) {
                    throw std::runtime_error("Mismatched Field types in aggregation.");
                }
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

struct FieldParser {
    static std::unique_ptr<Field> parseValue(FieldType type,
                                             const std::string& value) {
        switch (type) {
            case INT:
                return std::make_unique<Field>(std::stoi(value));
            case FLOAT:
                return std::make_unique<Field>(std::stof(value));
            case STRING:
                return std::make_unique<Field>(value);
        }
        throw std::runtime_error("Unsupported field type.");
    }
};

using TableRefId = uint32_t;
constexpr TableRefId INVALID_TABLE_REF_ID = 0;

struct TableRef {
    TableRefId id = INVALID_TABLE_REF_ID;
    TableId table_id = INVALID_TABLE_ID;
    std::string table_name;
    std::string alias;
};

struct ColumnRef {
    TableRefId table_ref_id = INVALID_TABLE_REF_ID;
    size_t column_index = 0;
};

struct JoinClause {
    TableRefId input_table_ref_id = INVALID_TABLE_REF_ID;
    ColumnRef left;
    ColumnRef right;
};

struct FilterClause {
    ColumnRef column;
    std::unique_ptr<Field> value;
};

struct ColumnEqualityClause {
    ColumnRef left;
    ColumnRef right;
};

struct QueryComponents {
    std::string tableName;
    std::vector<int> selectAttributes;
    bool aggregateOperation = false;
    AggrFuncType aggregateFunction = AggrFuncType::SUM;
    int aggregateAttributeIndex = -1;
    bool groupBy = false;
    int groupByAttributeIndex = -1;
    bool whereCondition = false;
    int whereAttributeIndex = -1;
    int lowerBound = std::numeric_limits<int>::min();
    int upperBound = std::numeric_limits<int>::max();
    bool equalityCondition = false;
    int equalityAttributeIndex = -1;
    std::string equalityColumnName;
    std::string equalityValueText;
    std::unique_ptr<Field> equalityValue;

    TableRefId base_table_ref_id = INVALID_TABLE_REF_ID;
    std::vector<TableRef> table_refs;
    std::vector<ColumnRef> select_columns;
    std::vector<JoinClause> joins;
    std::vector<FilterClause> filters;
    std::vector<ColumnEqualityClause> column_filters;
    std::vector<std::string> output_names;
    std::optional<ColumnRef> order_by_column;

    bool isJoinQuery() const {
        return !joins.empty();
    }
};

bool sameColumnRef(const ColumnRef& left, const ColumnRef& right) {
    return left.table_ref_id == right.table_ref_id &&
           left.column_index == right.column_index;
}

std::string stripOptionalBrackets(const std::string& value) {
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string joinOutputName(const std::string& projection) {
    std::regex minProjectionRegex(
        "^\\s*MIN\\s*\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*$",
        std::regex_constants::icase
    );
    std::regex columnProjectionRegex(
        "^\\s*\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*$"
    );
    std::smatch matches;
    if (std::regex_match(projection, matches, minProjectionRegex)) {
        return "MIN(" + static_cast<std::string>(matches[1]) + "." +
               static_cast<std::string>(matches[2]) + ")";
    }
    if (std::regex_match(projection, matches, columnProjectionRegex)) {
        return static_cast<std::string>(matches[1]) + "." +
               static_cast<std::string>(matches[2]);
    }
    throw std::runtime_error("Unsupported join projection: " + projection);
}

std::vector<std::string> splitOnAnd(const std::string& predicates) {
    std::vector<std::string> parts;
    std::regex andRegex("\\s+AND\\s+", std::regex_constants::icase);
    for (std::sregex_token_iterator it(predicates.begin(),
                                       predicates.end(),
                                       andRegex,
                                       -1);
         it != std::sregex_token_iterator();
         ++it) {
        auto part = TextUtil::trim(*it);
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

QueryComponents parseJoinQuery(const std::string& query, Catalog& catalog) {
    QueryComponents components;

    std::regex fromRegex(
        "^\\s*(?:(?:SELECT|PROJECT)\\s+)?(.+?)\\s+FROM\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)(?:\\s+(?:AS\\s+)?((?!JOIN\\b|WHERE\\b)[A-Za-z_][A-Za-z0-9_]*))?"
        "\\s+(.*)\\s*;?\\s*$",
        std::regex_constants::icase
    );
    std::smatch matches;
    if (!std::regex_match(query, matches, fromRegex)) {
        throw std::runtime_error("Unsupported join query: " + query);
    }

    std::map<std::string, TableRefId> alias_to_table_ref_id;
    auto tableIdForRef = [&](TableRefId table_ref_id) -> TableId {
        for (const auto& table_ref : components.table_refs) {
            if (table_ref.id == table_ref_id) {
                return table_ref.table_id;
            }
        }
        throw std::runtime_error(
            "Unknown table reference id: " + std::to_string(table_ref_id)
        );
    };
    auto addTableAlias = [&](const std::string& table_name,
                             const std::string& alias) -> TableRefId {
        auto& metadata = catalog.getTable(table_name);
        TableRefId table_ref_id =
            static_cast<TableRefId>(components.table_refs.size() + 1);
        if (!alias_to_table_ref_id.insert({alias, table_ref_id}).second) {
            throw std::runtime_error("Duplicate table alias: " + alias);
        }
        components.table_refs.push_back({
            table_ref_id,
            metadata.table_id,
            table_name,
            alias
        });
        return table_ref_id;
    };

    auto resolveColumn = [&](const std::string& alias,
                             const std::string& column_name) -> ColumnRef {
        auto alias_it = alias_to_table_ref_id.find(alias);
        if (alias_it == alias_to_table_ref_id.end()) {
            throw std::runtime_error("Unknown table alias: " + alias);
        }

        auto& metadata = catalog.getTable(tableIdForRef(alias_it->second));
        return {
            alias_it->second,
            static_cast<size_t>(metadata.schema.getColumnIndex(column_name))
        };
    };

    const std::string left_table = matches[2];
    const std::string left_alias = matches[3].matched
        ? static_cast<std::string>(matches[3])
        : left_table;
    std::string rest = TextUtil::trim(matches[4]);

    components.base_table_ref_id = addTableAlias(left_table, left_alias);

    const std::string projection_list = matches[1];

    std::regex joinClauseRegex(
        "^\\s*JOIN\\s+([A-Za-z_][A-Za-z0-9_]*)"
        "(?:\\s+(?:AS\\s+)?((?!ON\\b)[A-Za-z_][A-Za-z0-9_]*))?"
        "\\s+ON\\s+\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}"
        "\\s*=\\s*\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}"
        "(.*)$",
        std::regex_constants::icase
    );
    while (std::regex_match(rest, matches, joinClauseRegex)) {
        const std::string table_name = matches[1];
        const std::string alias = matches[2].matched
            ? static_cast<std::string>(matches[2])
            : table_name;
        TableRefId input_table_ref_id = addTableAlias(table_name, alias);
        components.joins.push_back({
            input_table_ref_id,
            resolveColumn(matches[3], matches[4]),
            resolveColumn(matches[5], matches[6])
        });
        rest = TextUtil::trim(matches[7]);
    }
    if (components.joins.empty()) {
        throw std::runtime_error("Join query must include at least one JOIN.");
    }

    std::regex orderByRegex(
        "\\s+ORDER\\s+BY\\s+\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*$",
        std::regex_constants::icase
    );
    std::smatch order_matches;
    if (std::regex_search(rest, order_matches, orderByRegex)) {
        components.order_by_column =
            resolveColumn(order_matches[1], order_matches[2]);
        rest = TextUtil::trim(rest.substr(
            0,
            static_cast<size_t>(order_matches.position())
        ));
    }

    std::regex whereRegex("^WHERE\\s+(.+)$", std::regex_constants::icase);
    if (std::regex_match(rest, matches, whereRegex)) {
        for (const auto& predicate : splitOnAnd(matches[1])) {
            std::regex columnPredicateRegex(
                "^\\s*\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*=\\s*"
                "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*$"
            );
            std::regex valuePredicateRegex(
                "^\\s*\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}\\s*=\\s*"
                "([^\\s;]+)\\s*$"
            );
            if (std::regex_match(predicate, matches, columnPredicateRegex)) {
                components.column_filters.push_back({
                    resolveColumn(matches[1], matches[2]),
                    resolveColumn(matches[3], matches[4])
                });
                continue;
            }
            if (std::regex_match(predicate, matches, valuePredicateRegex)) {
                auto column = resolveColumn(matches[1], matches[2]);
                auto& metadata = catalog.getTable(tableIdForRef(column.table_ref_id));
                const auto& column_schema =
                    metadata.schema.columns[column.column_index];
                components.filters.push_back({
                    column,
                    FieldParser::parseValue(
                        column_schema.type,
                        matches[3]
                    )
                });
                continue;
            }
            throw std::runtime_error("Unsupported WHERE predicate: " + predicate);
        }
        rest.clear();
    }
    if (!TextUtil::trim(rest).empty()) {
        throw std::runtime_error("Unexpected join query text: " + rest);
    }

    std::regex projectionRegex(
        "(?:MIN\\s*)?\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)\\}",
        std::regex_constants::icase
    );
    for (std::sregex_iterator it(projection_list.begin(),
                                 projection_list.end(),
                                 projectionRegex);
         it != std::sregex_iterator();
         ++it) {
        const std::string alias = (*it)[1];
        const std::string column_name = (*it)[2];
        components.select_columns.push_back(resolveColumn(alias, column_name));
        components.output_names.push_back(joinOutputName((*it)[0]));
    }
    if (components.select_columns.empty()) {
        throw std::runtime_error("Join query must project at least one column.");
    }

    return components;
}

QueryComponents parseQuery(const std::string& query, Catalog& catalog) {
    QueryComponents components;

    if (std::regex_search(query, std::regex("\\bJOIN\\b", std::regex_constants::icase))) {
        return parseJoinQuery(query, catalog);
    }

    std::regex selectAllRegex(
        "^\\s*\\{\\*\\}\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+))?\\s*$",
        std::regex_constants::icase);
    std::smatch selectAllMatches;
    if (std::regex_match(query, selectAllMatches, selectAllRegex)) {
        const std::string tableName = selectAllMatches[1];
        const auto& metadata = catalog.getTable(tableName);

        components.tableName = tableName;
        for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
            components.selectAttributes.push_back(static_cast<int>(i));
        }
        if (selectAllMatches[2].matched) {
            const std::string columnName = selectAllMatches[2];
            const std::string value = selectAllMatches[3];
            int column = metadata.schema.getColumnIndex(columnName);
            components.equalityCondition = true;
            components.equalityAttributeIndex = column;
            components.equalityColumnName = columnName;
            components.equalityValueText = value;
            components.equalityValue = FieldParser::parseValue(
                metadata.schema.columns[static_cast<size_t>(column)].type,
                value
            );
        }
        return components;
    }

    std::regex queryRegex(
        "^\\s*(SUM|COUNT)\\(([A-Za-z_][A-Za-z0-9_]*)\\)\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+GROUP\\s+BY\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*>\\s*(-?\\d+)\\s+and\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*<\\s*(-?\\d+))?\\s*$",
        std::regex_constants::icase);
    std::smatch matches;
    if (!std::regex_match(query, matches, queryRegex)) {
        throw std::runtime_error("Unsupported query: " + query);
    }

    std::string aggregateName = matches[1];
    for (auto& ch : aggregateName) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    const std::string aggregateColumnName = matches[2];
    const std::string tableName = matches[3];
    const std::string groupByColumnName = matches[4];
    const auto& metadata = catalog.getTable(tableName);

    components.tableName = tableName;
    components.aggregateOperation = true;
    components.aggregateFunction = (aggregateName == "COUNT")
        ? AggrFuncType::COUNT
        : AggrFuncType::SUM;
    components.aggregateAttributeIndex = metadata.schema.getColumnIndex(aggregateColumnName);
    components.groupBy = true;
    components.groupByAttributeIndex = metadata.schema.getColumnIndex(groupByColumnName);
    components.selectAttributes.push_back(components.aggregateAttributeIndex);

    if (matches[5].matched) {
        const std::string lowerBoundColumnName = matches[5];
        const int lowerBound = std::stoi(matches[6]);
        const std::string upperBoundColumnName = matches[7];
        const int upperBound = std::stoi(matches[8]);

        if (lowerBoundColumnName != upperBoundColumnName) {
            throw std::runtime_error("WHERE clause conditions apply to different columns.");
        }

        components.whereCondition = true;
        components.whereAttributeIndex = metadata.schema.getColumnIndex(lowerBoundColumnName);
        components.lowerBound = lowerBound;
        components.upperBound = upperBound;
    }

    return components;
}


struct ParsedStatement {
    enum class Kind { Insert, Update, Delete, Select, Explain };
    Kind kind;
    std::string tableName;
    std::vector<std::string> values;
    std::string setColumnName;
    std::string setValue;
    std::string whereColumnName;
    std::string whereValue;
    QueryComponents query;
};

class QueryParser {
private:
    Catalog& catalog;

public:
    explicit QueryParser(Catalog& catalog) : catalog(catalog) {}

    QueryComponents parseQueryText(const std::string& query) {
        return parseQuery(query, catalog);
    }

    ParsedStatement parseStatement(const std::string& statement) {
        std::smatch matches;

        std::regex insertRegex("^\\s*INSERT\\s+INTO\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+VALUES\\s*\\((.*)\\)\\s*;?\\s*$",
                               std::regex_constants::icase);
        if (std::regex_match(statement, matches, insertRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Insert;
            parsed.tableName = matches[1];
            parsed.values = TextUtil::split(matches[2], ',');
            return parsed;
        }

        std::regex updateRegex("^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s*;?\\s*$",
                               std::regex_constants::icase);
        if (std::regex_match(statement, matches, updateRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Update;
            parsed.tableName = matches[1];
            parsed.setColumnName = matches[2];
            parsed.setValue = matches[3];
            parsed.whereColumnName = matches[4];
            parsed.whereValue = matches[5];
            return parsed;
        }

        std::regex deleteRegex("^\\s*DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s*;?\\s*$",
                               std::regex_constants::icase);
        if (std::regex_match(statement, matches, deleteRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Delete;
            parsed.tableName = matches[1];
            parsed.whereColumnName = matches[2];
            parsed.whereValue = matches[3];
            return parsed;
        }

        std::regex explainRegex("^\\s*EXPLAIN\\s+(?:SELECT|PROJECT)\\s+(.*)\\s*;?\\s*$",
                                std::regex_constants::icase);
        if (std::regex_match(statement, matches, explainRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Explain;
            parsed.query = parseQueryText(matches[1]);
            return parsed;
        }

        std::regex selectRegex("^\\s*(?:SELECT|PROJECT)\\s+(.*)\\s*;?\\s*$",
                               std::regex_constants::icase);
        if (std::regex_match(statement, matches, selectRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Select;
            parsed.query = parseQueryText(matches[1]);
            return parsed;
        }

        throw std::runtime_error("Unsupported statement: " + statement);
    }
};

std::string aggregateFunctionName(AggrFuncType function) {
    switch (function) {
        case AggrFuncType::COUNT:
            return "COUNT";
        case AggrFuncType::MAX:
            return "MAX";
        case AggrFuncType::MIN:
            return "MIN";
        case AggrFuncType::SUM:
            return "SUM";
    }
    throw std::runtime_error("Unsupported aggregation function.");
}

std::vector<std::string> outputColumnNames(const QueryComponents& components,
                                           const TableMetadata& metadata) {
    std::vector<std::string> names;
    if (components.groupBy) {
        names.push_back(metadata.schema.columns[components.groupByAttributeIndex].name);
    }
    if (components.aggregateOperation) {
        const auto& column = metadata.schema.columns[components.aggregateAttributeIndex].name;
        names.push_back(aggregateFunctionName(components.aggregateFunction) + "(" + column + ")");
    }
    if (!components.aggregateOperation) {
        for (auto attr : components.selectAttributes) {
            names.push_back(metadata.schema.columns[attr].name);
        }
    }
    return names;
}

void printColumnHeader(const QueryComponents& components,
                       const TableMetadata& metadata) {
    for (const auto& name : outputColumnNames(components, metadata)) {
        std::cout << name << " ";
    }
    std::cout << std::endl;
}

struct QueryResult {
    size_t row_count = 0;
    std::vector<std::vector<std::unique_ptr<Field>>> sample_rows;
};

struct ColumnStats {
    FieldType type = INT;
    size_t distinct_count = 0;
    std::map<std::string, size_t> mcv_counts;
};

struct TableStats {
    TableId table_id = INVALID_TABLE_ID;
    std::string table_name;
    size_t row_count = 0;
    size_t page_count = 0;
    std::map<size_t, ColumnStats> columns;
};

struct ColumnRelationStats {
    FieldType type = INT;
    double distinct_count = 0.0;
    bool unique = false;
    std::map<std::string, double> mcv_counts;
};

struct RelationStats {
    double rows = 0.0;
    std::map<TableRefId, std::map<size_t, ColumnRelationStats>> columns;
};

std::string fieldTypeName(FieldType type) {
    switch (type) {
        case INT:
            return "INT";
        case FLOAT:
            return "FLOAT";
        case STRING:
            return "STRING";
    }
    throw std::runtime_error("Unknown field type.");
}

size_t physicalOffset(const ColumnRef& column,
                      const std::map<TableRefId, size_t>& table_offsets) {
    auto offset = table_offsets.find(column.table_ref_id);
    if (offset == table_offsets.end()) {
        throw std::runtime_error("Column references a table not yet joined.");
    }
    return offset->second + column.column_index;
}

const TableRef& tableRefForId(const QueryComponents& components,
                              TableRefId table_ref_id) {
    for (const auto& table_ref : components.table_refs) {
        if (table_ref.id == table_ref_id) {
            return table_ref;
        }
    }
    throw std::runtime_error(
        "Unknown table reference id: " + std::to_string(table_ref_id)
    );
}

std::unique_ptr<IPredicate> makeTableFilterPredicate(
        const QueryComponents& components,
        TableRefId table_ref_id) {
    auto predicate = std::make_unique<ComplexPredicate>(
        ComplexPredicate::LogicOperator::AND
    );
    bool has_filter = false;

    for (const auto& filter : components.filters) {
        if (filter.column.table_ref_id != table_ref_id) {
            continue;
        }
        predicate->addPredicate(std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(filter.column.column_index),
            SimplePredicate::Operand(filter.value->clone()),
            SimplePredicate::ComparisonOperator::EQ
        ));
        has_filter = true;
    }

    if (!has_filter) {
        return nullptr;
    }
    return predicate;
}

std::string columnRefLabel(const QueryComponents& components,
                           Catalog& catalog,
                           const ColumnRef& column);
std::string formatEstimate(double value);

class BloomFilter {
private:
    std::vector<std::uint64_t> words;
    size_t hash_count = 4;
    size_t inserted_keys = 0;

    static size_t hashField(const Field& field, std::uint64_t salt) {
        size_t hash = IndexKeyHasher{}(IndexKey(field));
        hash ^= salt + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        return hash;
    }

    size_t bitCount() const {
        return words.size() * 64;
    }

    void setBit(size_t bit_index) {
        words[bit_index / 64] |= (1ULL << (bit_index % 64));
    }

    bool bitIsSet(size_t bit_index) const {
        return (words[bit_index / 64] & (1ULL << (bit_index % 64))) != 0;
    }

public:
    explicit BloomFilter(size_t expected_keys = 4096,
                         size_t bits_per_key = 12,
                         size_t hashes = 4)
        : words((std::max<size_t>(1024, expected_keys * bits_per_key) + 63) / 64),
          hash_count(hashes) {}

    void add(const Field& field) {
        size_t primary = hashField(field, 0x243f6a8885a308d3ULL);
        size_t secondary = hashField(field, 0x13198a2e03707344ULL);
        if (secondary == 0) {
            secondary = 1;
        }
        for (size_t i = 0; i < hash_count; i++) {
            setBit((primary + i * secondary) % bitCount());
        }
        inserted_keys++;
    }

    bool mayContain(const Field& field) const {
        if (inserted_keys == 0) {
            return false;
        }
        size_t primary = hashField(field, 0x243f6a8885a308d3ULL);
        size_t secondary = hashField(field, 0x13198a2e03707344ULL);
        if (secondary == 0) {
            secondary = 1;
        }
        for (size_t i = 0; i < hash_count; i++) {
            if (!bitIsSet((primary + i * secondary) % bitCount())) {
                return false;
            }
        }
        return true;
    }

};

struct LipRuntimeFilter {
    ColumnRef source_column;
    ColumnRef target_column;
    BloomFilter filter;
    size_t probes = 0;
    size_t rejected = 0;

    double rejectRate() const {
        if (probes == 0) {
            return 0.0;
        }
        return static_cast<double>(rejected) / static_cast<double>(probes);
    }
};

struct LipFilterSpec {
    ColumnRef source_column;
    ColumnRef target_column;
};

class LookaheadInfoPassingManager {
private:
    std::map<TableRefId, std::vector<LipRuntimeFilter>> filters_by_target;
    std::map<TableRefId, size_t> target_probe_counts;
    size_t adaptive_reorders = 0;

public:
    void build(const QueryComponents& components,
               Catalog& catalog,
               PageManager& page_manager) {
        filters_by_target.clear();
        target_probe_counts.clear();
        adaptive_reorders = 0;

        for (const auto& spec : deriveFilterSpecs(components)) {
            const auto& source_ref =
                tableRefForId(components, spec.source_column.table_ref_id);
            auto& source_metadata = catalog.getTable(source_ref.table_id);
            TableHeap source_heap(source_metadata, page_manager);
            ScanOperator source_scan(source_heap);
            auto source_predicate = makeTableFilterPredicate(
                components,
                spec.source_column.table_ref_id
            );

            std::optional<SelectOperator> filtered_source;
            Operator* source_op = &source_scan;
            if (source_predicate) {
                filtered_source.emplace(
                    source_scan,
                    std::move(source_predicate)
                );
                source_op = &*filtered_source;
            }

            LipRuntimeFilter runtime_filter;
            runtime_filter.source_column = spec.source_column;
            runtime_filter.target_column = spec.target_column;

            source_op->open();
            while (source_op->next()) {
                auto tuple = source_op->getOutput();
                if (spec.source_column.column_index >= tuple.size()) {
                    throw std::runtime_error("LIP source column is out of range.");
                }
                runtime_filter.filter.add(
                    *tuple[spec.source_column.column_index]
                );
            }
            source_op->close();

            filters_by_target[spec.target_column.table_ref_id].push_back(
                std::move(runtime_filter)
            );
        }
    }

    bool hasFilters(TableRefId table_ref_id) const {
        auto it = filters_by_target.find(table_ref_id);
        return it != filters_by_target.end() && !it->second.empty();
    }

    bool mayPass(TableRefId table_ref_id,
                 const std::vector<std::unique_ptr<Field>>& tuple_fields) {
        auto it = filters_by_target.find(table_ref_id);
        if (it == filters_by_target.end()) {
            return true;
        }

        size_t& target_probes = target_probe_counts[table_ref_id];
        target_probes++;
        if ((target_probes & (target_probes - 1)) == 0) {
            adaptFilterOrder(it->second);
        }
        for (auto& filter : it->second) {
            if (filter.target_column.column_index >= tuple_fields.size()) {
                throw std::runtime_error("LIP target column is out of range.");
            }

            filter.probes++;
            if (!filter.filter.mayContain(
                    *tuple_fields[filter.target_column.column_index])) {
                filter.rejected++;
                return false;
            }
        }
        return true;
    }

    size_t rejectedTuples() const {
        size_t total = 0;
        for (const auto& entry : filters_by_target) {
            for (const auto& filter : entry.second) {
                total += filter.rejected;
            }
        }
        return total;
    }

private:
    static bool betterFilterOrder(const LipRuntimeFilter& left,
                                  const LipRuntimeFilter& right) {
        if (left.probes == 0 && right.probes == 0) {
            return false;
        }
        if (left.probes == 0) {
            return false;
        }
        if (right.probes == 0) {
            return true;
        }
        if (left.rejectRate() != right.rejectRate()) {
            return left.rejectRate() > right.rejectRate();
        }
        return left.source_column.column_index <
               right.source_column.column_index;
    }

    static void orderFilters(std::vector<LipRuntimeFilter>& filters) {
        std::stable_sort(
            filters.begin(),
            filters.end(),
            betterFilterOrder
        );
    }

    void adaptFilterOrder(std::vector<LipRuntimeFilter>& filters) {
        if (filters.size() < 2) {
            return;
        }

        std::vector<ColumnRef> old_order;
        for (const auto& filter : filters) {
            old_order.push_back(filter.source_column);
        }
        orderFilters(filters);
        for (size_t i = 0; i < filters.size(); i++) {
            if (!sameColumnRef(old_order[i], filters[i].source_column)) {
                adaptive_reorders++;
                return;
            }
        }
    }

    static bool sameColumnRef(const ColumnRef& left,
                              const ColumnRef& right) {
        return left.table_ref_id == right.table_ref_id &&
               left.column_index == right.column_index;
    }

    std::vector<LipFilterSpec> deriveFilterSpecs(
            const QueryComponents& components) const {
        std::set<TableRefId> filtered_tables;
        for (const auto& filter : components.filters) {
            filtered_tables.insert(filter.column.table_ref_id);
        }

        std::set<std::string> seen;
        std::vector<LipFilterSpec> specs;
        auto addSpec = [&](const ColumnRef& source,
                           const ColumnRef& target) {
            if (filtered_tables.find(source.table_ref_id) ==
                filtered_tables.end()) {
                return;
            }
            if (source.table_ref_id == target.table_ref_id) {
                return;
            }

            std::string key =
                std::to_string(source.table_ref_id) + "." +
                std::to_string(source.column_index) + "->" +
                std::to_string(target.table_ref_id) + "." +
                std::to_string(target.column_index);
            if (!seen.insert(key).second) {
                return;
            }
            specs.push_back({source, target});
        };

        for (const auto& join : components.joins) {
            addSpec(join.left, join.right);
            addSpec(join.right, join.left);
        }
        for (const auto& equality : components.column_filters) {
            addSpec(equality.left, equality.right);
            addSpec(equality.right, equality.left);
        }
        return specs;
    }
};

class RuntimeFilterScanOperator : public UnaryOperator {
private:
    TableRefId table_ref_id;
    LookaheadInfoPassingManager& lip_manager;
    std::vector<std::unique_ptr<Field>> current_output;
    bool has_next = false;

public:
    RuntimeFilterScanOperator(Operator& input,
                              TableRefId table_ref_id,
                              LookaheadInfoPassingManager& lip_manager)
        : UnaryOperator(input),
          table_ref_id(table_ref_id),
          lip_manager(lip_manager) {}

    void open() override {
        input->open();
        current_output.clear();
        has_next = false;
    }

    bool next() override {
        while (input->next()) {
            auto tuple = input->getOutput();
            if (!lip_manager.mayPass(table_ref_id, tuple)) {
                continue;
            }
            current_output = std::move(tuple);
            has_next = true;
            return true;
        }
        current_output.clear();
        has_next = false;
        return false;
    }

    void close() override {
        input->close();
        current_output.clear();
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (!has_next) {
            return {};
        }
        return cloneFields(current_output);
    }
};

std::string columnRefLabel(const QueryComponents& components,
                           Catalog& catalog,
                           const ColumnRef& column) {
    const auto& table_ref = tableRefForId(components, column.table_ref_id);
    const auto& metadata = catalog.getTable(table_ref.table_id);
    return table_ref.alias + "." +
           metadata.schema.columns[column.column_index].name;
}

std::string formatEstimate(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

enum class PhysicalJoinKind {
    NestedLoopJoin,
    HashJoin,
    IndexNestedLoopJoin,
    SortMergeJoin
};

enum class PhysicalOpKind {
    TableScan,
    Sort,
    NestedLoopJoin,
    HashJoin,
    IndexNestedLoopJoin,
    SortMergeJoin
};

std::string physicalOpKindName(PhysicalOpKind kind) {
    switch (kind) {
        case PhysicalOpKind::TableScan:
            return "TableScan";
        case PhysicalOpKind::Sort:
            return "Sort";
        case PhysicalOpKind::NestedLoopJoin:
            return "NestedLoopJoin";
        case PhysicalOpKind::HashJoin:
            return "HashJoin";
        case PhysicalOpKind::IndexNestedLoopJoin:
            return "IndexNestedLoopJoin";
        case PhysicalOpKind::SortMergeJoin:
            return "SortMergeJoin";
    }
    throw std::runtime_error("Unknown physical operator kind.");
}

PhysicalOpKind physicalOpFromJoinKind(PhysicalJoinKind kind) {
    switch (kind) {
        case PhysicalJoinKind::NestedLoopJoin:
            return PhysicalOpKind::NestedLoopJoin;
        case PhysicalJoinKind::HashJoin:
            return PhysicalOpKind::HashJoin;
        case PhysicalJoinKind::IndexNestedLoopJoin:
            return PhysicalOpKind::IndexNestedLoopJoin;
        case PhysicalJoinKind::SortMergeJoin:
            return PhysicalOpKind::SortMergeJoin;
    }
    throw std::runtime_error("Unknown physical join kind.");
}

PhysicalJoinKind joinKindFromPhysicalOp(PhysicalOpKind kind) {
    switch (kind) {
        case PhysicalOpKind::NestedLoopJoin:
            return PhysicalJoinKind::NestedLoopJoin;
        case PhysicalOpKind::HashJoin:
            return PhysicalJoinKind::HashJoin;
        case PhysicalOpKind::IndexNestedLoopJoin:
            return PhysicalJoinKind::IndexNestedLoopJoin;
        case PhysicalOpKind::SortMergeJoin:
            return PhysicalJoinKind::SortMergeJoin;
        case PhysicalOpKind::TableScan:
        case PhysicalOpKind::Sort:
            break;
    }
    throw std::runtime_error("TableScan is not a join operator.");
}

struct JoinTreeStepEstimate {
    JoinClause join;
    double output_rows = 0.0;
    RelationStats output_relation;
    PhysicalJoinKind chosen_join_kind = PhysicalJoinKind::HashJoin;
    bool has_index_lookup = false;
    ColumnRef index_lookup_column;
    bool sort_left_input = false;
    bool sort_right_input = false;
    ColumnRef left_sort_column;
    ColumnRef right_sort_column;
    std::optional<ColumnRef> delivered_order;
    double chosen_cost = 0.0;
};

struct PhysicalPlanNode {
    PhysicalOpKind op = PhysicalOpKind::TableScan;
    TableRefId table_ref_id = INVALID_TABLE_REF_ID;
    std::shared_ptr<PhysicalPlanNode> left;
    std::shared_ptr<PhysicalPlanNode> right;
    JoinClause join;
    ColumnRef sort_column;
    std::set<TableRefId> table_refs;
    RelationStats relation;
    bool has_index_lookup = false;
    ColumnRef index_lookup_column;
    std::optional<ColumnRef> delivered_order;
    double cost = 0.0;

    bool isTableScan() const {
        return op == PhysicalOpKind::TableScan;
    }

    bool isSort() const {
        return op == PhysicalOpKind::Sort;
    }

    PhysicalJoinKind joinKind() const {
        return joinKindFromPhysicalOp(op);
    }
};

using GroupId = size_t;
using ExpressionId = size_t;

constexpr GroupId INVALID_GROUP_ID = 0;

enum class MemoOpKind {
    LogicalScan,
    LogicalSelect,
    LogicalEquiJoin,
    LogicalProject,
    TableScan,
    NestedLoopJoin,
    HashJoin,
    IndexNestedLoopJoin,
    SortMergeJoin
};

struct MemoExpression {
    ExpressionId id = 0;
    MemoOpKind op = MemoOpKind::LogicalScan;
    std::vector<GroupId> inputs;
    std::string label;
};

struct MemoGroup {
    GroupId id = INVALID_GROUP_ID;
    std::set<TableRefId> table_refs;
    std::vector<MemoExpression> expressions;
};

class Memo {
private:
    std::vector<MemoGroup> groups;
    ExpressionId next_expression_id = 1;
    GroupId final_group_id = INVALID_GROUP_ID;

public:
    GroupId addGroup(std::set<TableRefId> table_refs,
                     MemoOpKind op,
                     std::vector<GroupId> inputs,
                     std::string label) {
        GroupId group_id = static_cast<GroupId>(groups.size() + 1);
        MemoExpression expression{
            next_expression_id++,
            op,
            std::move(inputs),
            std::move(label)
        };
        groups.push_back({
            group_id,
            std::move(table_refs),
            {std::move(expression)}
        });
        return group_id;
    }

    MemoGroup& groupFor(GroupId group_id) {
        if (group_id == INVALID_GROUP_ID || group_id > groups.size()) {
            throw std::runtime_error("Memo group id is out of range.");
        }
        return groups[group_id - 1];
    }

    const MemoGroup& groupFor(GroupId group_id) const {
        if (group_id == INVALID_GROUP_ID || group_id > groups.size()) {
            throw std::runtime_error("Memo group id is out of range.");
        }
        return groups[group_id - 1];
    }

    bool addExpressionToGroup(GroupId group_id,
                              MemoOpKind op,
                              std::vector<GroupId> inputs,
                              std::string label) {
        auto& group = groupFor(group_id);
        for (const auto& expression : group.expressions) {
            if (expression.op == op &&
                expression.inputs == inputs &&
                expression.label == label) {
                return false;
            }
        }

        group.expressions.push_back({
            next_expression_id++,
            op,
            std::move(inputs),
            std::move(label)
        });
        return true;
    }

    void setFinalGroupId(GroupId group_id) {
        final_group_id = group_id;
    }

    GroupId finalGroupId() const {
        return final_group_id;
    }

    const std::vector<MemoGroup>& allGroups() const {
        return groups;
    }

    size_t groupCount() const {
        return groups.size();
    }

    size_t expressionCount() const {
        size_t count = 0;
        for (const auto& group : groups) {
            count += group.expressions.size();
        }
        return count;
    }
};

struct MemoRuleStats {
    size_t initial_groups = 0;
    size_t initial_expressions = 0;
    size_t final_groups = 0;
    size_t final_expressions = 0;
    size_t logical_join_alternatives = 0;
    size_t scan_implementations = 0;
    size_t join_implementations = 0;
    size_t sort_merge_implementations = 0;
    size_t sort_enforcers = 0;
    size_t optimize_group_tasks = 0;
    size_t apply_rule_tasks = 0;
    size_t optimize_input_tasks = 0;
    size_t tasks_executed = 0;
    size_t winner_candidates = 0;
};

struct MemoPlanChoice {
    std::shared_ptr<PhysicalPlanNode> plan_root;
    RelationStats relation;
    std::set<TableRefId> table_refs;
    double cost = std::numeric_limits<double>::infinity();
};

struct MemoOptimizationResult {
    QueryComponents components;
    MemoRuleStats rule_stats;
    std::shared_ptr<PhysicalPlanNode> plan_root;
    double final_estimate = 0.0;
    double estimated_cost = 0.0;
    size_t index_joins = 0;
    size_t sort_merge_joins = 0;
    size_t sort_enforcers = 0;
};

enum class CascadesTaskKind {
    OptimizeGroup,
    ApplyRule,
    OptimizeInput
};

struct CascadesTask {
    CascadesTaskKind kind = CascadesTaskKind::OptimizeGroup;
    GroupId group_id = INVALID_GROUP_ID;
    size_t expression_index = 0;

    static CascadesTask optimizeGroup(GroupId group_id) {
        return {CascadesTaskKind::OptimizeGroup, group_id, 0};
    }

    static CascadesTask applyRule(GroupId group_id, size_t expression_index) {
        return {CascadesTaskKind::ApplyRule, group_id, expression_index};
    }

    static CascadesTask optimizeInput(GroupId group_id) {
        return {CascadesTaskKind::OptimizeInput, group_id, 0};
    }
};

QueryResult executeJoinQuery(const QueryComponents& components,
                             Catalog& catalog,
                             PageManager& page_manager,
                             size_t sample_limit = 5,
                             const std::vector<PhysicalJoinKind>* physical_join_kinds = nullptr,
                             const std::shared_ptr<PhysicalPlanNode>& plan_root = nullptr,
                             const IndexManager* index_manager = nullptr,
                             LookaheadInfoPassingManager* lip_manager = nullptr) {
    if (components.base_table_ref_id == INVALID_TABLE_REF_ID) {
        throw std::runtime_error("Join query is missing a base table.");
    }
    if (!plan_root && physical_join_kinds &&
        physical_join_kinds->size() != components.joins.size()) {
        throw std::runtime_error("Physical join plan length does not match join count.");
    }

    std::map<TableRefId, size_t> table_offsets;
    std::map<TableRefId, size_t> table_widths;
    std::vector<std::unique_ptr<TableHeap>> heaps;
    std::vector<std::unique_ptr<ScanOperator>> scans;
    std::vector<std::unique_ptr<SelectOperator>> pushed_selects;
    std::vector<std::unique_ptr<RuntimeFilterScanOperator>> lip_scans;
    std::vector<std::unique_ptr<SortOperator>> sort_buffers;
    std::vector<std::unique_ptr<NestedLoopJoinOperator>> nested_loop_join_buffers;
    std::vector<std::unique_ptr<SortMergeJoinOperator>> sort_merge_join_buffers;
    std::vector<std::unique_ptr<HashJoinOperator>> hash_join_buffers;
    std::vector<std::unique_ptr<IndexNestedLoopJoinOperator>> index_join_buffers;

    auto addTableHeap = [&](TableRefId table_ref_id) -> TableHeap& {
        const auto& table_ref = tableRefForId(components, table_ref_id);
        auto& metadata = catalog.getTable(table_ref.table_id);
        table_widths[table_ref_id] = metadata.schema.columns.size();
        heaps.push_back(std::make_unique<TableHeap>(metadata, page_manager));
        return *heaps.back();
    };

    auto addScan = [&](TableRefId table_ref_id) -> Operator& {
        TableHeap& table_heap = addTableHeap(table_ref_id);
        scans.push_back(std::make_unique<ScanOperator>(table_heap));

        Operator* scan_root = scans.back().get();
        auto predicate = makeTableFilterPredicate(components, table_ref_id);
        if (predicate) {
            pushed_selects.push_back(std::make_unique<SelectOperator>(
                *scan_root,
                std::move(predicate)
            ));
            scan_root = pushed_selects.back().get();
        }
        if (lip_manager && lip_manager->hasFilters(table_ref_id)) {
            lip_scans.push_back(std::make_unique<RuntimeFilterScanOperator>(
                *scan_root,
                table_ref_id,
                *lip_manager
            ));
            scan_root = lip_scans.back().get();
        }
        return *scan_root;
    };

    auto tableOffsetIn = [&](const std::vector<TableRefId>& table_ref_ids,
                             TableRefId table_ref_id) {
        size_t offset = 0;
        for (TableRefId current_table_ref_id : table_ref_ids) {
            if (current_table_ref_id == table_ref_id) {
                return offset;
            }
            offset += table_widths.at(current_table_ref_id);
        }
        throw std::runtime_error("JOIN table is not in this subtree.");
    };

    auto sameColumnRef = [](const ColumnRef& left, const ColumnRef& right) {
        return left.table_ref_id == right.table_ref_id &&
               left.column_index == right.column_index;
    };

    auto otherJoinColumn = [&](const JoinClause& join,
                               const ColumnRef& column) {
        if (sameColumnRef(join.left, column)) {
            return join.right;
        }
        if (sameColumnRef(join.right, column)) {
            return join.left;
        }
        throw std::runtime_error("Index lookup column is not part of join.");
    };

    auto addJoinOperator = [&](Operator& left_op,
                               Operator& right_op,
                               size_t left_attr_index,
                               size_t right_attr_index,
                               PhysicalJoinKind join_kind) -> Operator& {
        if (join_kind == PhysicalJoinKind::NestedLoopJoin) {
            nested_loop_join_buffers.push_back(
                std::make_unique<NestedLoopJoinOperator>(
                    left_op,
                    right_op,
                    left_attr_index,
                    right_attr_index
                )
            );
            return *nested_loop_join_buffers.back();
        }
        if (join_kind == PhysicalJoinKind::SortMergeJoin) {
            sort_merge_join_buffers.push_back(
                std::make_unique<SortMergeJoinOperator>(
                    left_op,
                    right_op,
                    left_attr_index,
                    right_attr_index
                )
            );
            return *sort_merge_join_buffers.back();
        }

        hash_join_buffers.push_back(std::make_unique<HashJoinOperator>(
            left_op,
            right_op,
            left_attr_index,
            right_attr_index
        ));
        return *hash_join_buffers.back();
    };

    auto addIndexJoinOperator =
            [&](Operator& outer_op,
                const std::vector<TableRefId>& outer_table_ref_ids,
                TableRefId indexed_table_ref_id,
                const ColumnRef& outer_column,
                const ColumnRef& indexed_column,
                bool indexed_output_first) -> Operator& {
        if (!index_manager) {
            throw std::runtime_error("Index join requires an index manager.");
        }
        const auto& indexed_table_ref = tableRefForId(
            components,
            indexed_table_ref_id
        );
        IndexDescriptor descriptor{
            indexed_table_ref.table_id,
            indexed_column.column_index
        };
        TableHeap& indexed_heap = addTableHeap(indexed_table_ref_id);
        auto inner_predicate =
            makeTableFilterPredicate(components, indexed_table_ref_id);
        size_t outer_attr_index =
            tableOffsetIn(outer_table_ref_ids, outer_column.table_ref_id) +
            outer_column.column_index;
        index_join_buffers.push_back(
            std::make_unique<IndexNestedLoopJoinOperator>(
                outer_op,
                indexed_heap,
                index_manager->index(descriptor),
                outer_attr_index,
                indexed_column.column_index,
                indexed_output_first,
                std::move(inner_predicate)
            )
        );
        return *index_join_buffers.back();
    };

    struct BuiltPlanOperator {
        Operator* op = nullptr;
        std::vector<TableRefId> table_ref_ids;
        size_t width = 0;
    };

    std::function<BuiltPlanOperator(const std::shared_ptr<PhysicalPlanNode>&)>
        buildPlanNode =
            [&](const std::shared_ptr<PhysicalPlanNode>& node) -> BuiltPlanOperator {
        if (!node) {
            throw std::runtime_error("Missing physical plan node.");
        }
        if (node->isTableScan()) {
            Operator& scan = addScan(node->table_ref_id);
            return {
                &scan,
                {node->table_ref_id},
                table_widths.at(node->table_ref_id)
            };
        }
        if (node->isSort()) {
            auto input = buildPlanNode(node->left);
            size_t sort_attr_index =
                tableOffsetIn(input.table_ref_ids,
                              node->sort_column.table_ref_id) +
                node->sort_column.column_index;
            sort_buffers.push_back(std::make_unique<SortOperator>(
                *input.op,
                sort_attr_index
            ));
            return {
                sort_buffers.back().get(),
                input.table_ref_ids,
                input.width
            };
        }

        if (node->joinKind() == PhysicalJoinKind::IndexNestedLoopJoin) {
            if (!node->has_index_lookup) {
                throw std::runtime_error("Index join is missing lookup column.");
            }

            ColumnRef outer_column =
                otherJoinColumn(node->join, node->index_lookup_column);
            bool index_on_left =
                node->left &&
                node->left->isTableScan() &&
                node->left->table_ref_id ==
                    node->index_lookup_column.table_ref_id;
            bool index_on_right =
                node->right &&
                node->right->isTableScan() &&
                node->right->table_ref_id ==
                    node->index_lookup_column.table_ref_id;

            if (index_on_left) {
                auto right = buildPlanNode(node->right);
                Operator& join_op = addIndexJoinOperator(
                    *right.op,
                    right.table_ref_ids,
                    node->left->table_ref_id,
                    outer_column,
                    node->index_lookup_column,
                    true
                );
                std::vector<TableRefId> table_ref_ids{node->left->table_ref_id};
                table_ref_ids.insert(
                    table_ref_ids.end(),
                    right.table_ref_ids.begin(),
                    right.table_ref_ids.end()
                );
                size_t width =
                    table_widths.at(node->left->table_ref_id) + right.width;
                return {&join_op, table_ref_ids, width};
            }

            if (index_on_right) {
                auto left = buildPlanNode(node->left);
                Operator& join_op = addIndexJoinOperator(
                    *left.op,
                    left.table_ref_ids,
                    node->right->table_ref_id,
                    outer_column,
                    node->index_lookup_column,
                    false
                );
                left.table_ref_ids.push_back(node->right->table_ref_id);
                size_t width =
                    left.width + table_widths.at(node->right->table_ref_id);
                return {&join_op, left.table_ref_ids, width};
            }

            throw std::runtime_error(
                "Index nested-loop join can only probe a base-table child."
            );
        }

        auto left = buildPlanNode(node->left);
        auto right = buildPlanNode(node->right);
        bool left_has_join_left =
            std::find(left.table_ref_ids.begin(),
                      left.table_ref_ids.end(),
                      node->join.left.table_ref_id) != left.table_ref_ids.end();
        bool left_has_join_right =
            std::find(left.table_ref_ids.begin(),
                      left.table_ref_ids.end(),
                      node->join.right.table_ref_id) != left.table_ref_ids.end();
        bool right_has_join_left =
            std::find(right.table_ref_ids.begin(),
                      right.table_ref_ids.end(),
                      node->join.left.table_ref_id) != right.table_ref_ids.end();
        bool right_has_join_right =
            std::find(right.table_ref_ids.begin(),
                      right.table_ref_ids.end(),
                      node->join.right.table_ref_id) != right.table_ref_ids.end();

        size_t left_attr_index = 0;
        size_t right_attr_index = 0;
        if (left_has_join_left && right_has_join_right) {
            left_attr_index =
                tableOffsetIn(left.table_ref_ids, node->join.left.table_ref_id) +
                node->join.left.column_index;
            right_attr_index =
                tableOffsetIn(right.table_ref_ids, node->join.right.table_ref_id) +
                node->join.right.column_index;
        } else if (left_has_join_right && right_has_join_left) {
            left_attr_index =
                tableOffsetIn(left.table_ref_ids, node->join.right.table_ref_id) +
                node->join.right.column_index;
            right_attr_index =
                tableOffsetIn(right.table_ref_ids, node->join.left.table_ref_id) +
                node->join.left.column_index;
        } else {
            throw std::runtime_error(
                "Join tree edge does not connect the two child subtrees."
            );
        }

        Operator& join_op = addJoinOperator(
            *left.op,
            *right.op,
            left_attr_index,
            right_attr_index,
            node->joinKind()
        );
        left.table_ref_ids.insert(
            left.table_ref_ids.end(),
            right.table_ref_ids.begin(),
            right.table_ref_ids.end()
        );
        return {&join_op, left.table_ref_ids, left.width + right.width};
    };

    Operator* rootOp = nullptr;
    size_t output_width = 0;
    if (plan_root) {
        auto built = buildPlanNode(plan_root);
        rootOp = built.op;
        output_width = built.width;
        size_t offset = 0;
        for (TableRefId table_ref_id : built.table_ref_ids) {
            table_offsets[table_ref_id] = offset;
            offset += table_widths.at(table_ref_id);
        }
    } else {
        rootOp = &addScan(components.base_table_ref_id);
        table_offsets[components.base_table_ref_id] = 0;
        output_width = table_widths[components.base_table_ref_id];

        for (size_t join_index = 0;
             join_index < components.joins.size();
             join_index++) {
            const auto& join = components.joins[join_index];
            auto& right_scan = addScan(join.input_table_ref_id);

            size_t left_attr_index = 0;
            size_t right_attr_index = 0;
            if (table_offsets.find(join.left.table_ref_id) != table_offsets.end() &&
                join.right.table_ref_id == join.input_table_ref_id) {
                left_attr_index = physicalOffset(join.left, table_offsets);
                right_attr_index = join.right.column_index;
            } else if (table_offsets.find(join.right.table_ref_id) != table_offsets.end() &&
                       join.left.table_ref_id == join.input_table_ref_id) {
                left_attr_index = physicalOffset(join.right, table_offsets);
                right_attr_index = join.left.column_index;
            } else {
                throw std::runtime_error(
                    "JOIN must connect the new input table to an earlier table."
                );
            }

            PhysicalJoinKind join_kind = physical_join_kinds
                ? (*physical_join_kinds)[join_index]
                : PhysicalJoinKind::HashJoin;
            rootOp = &addJoinOperator(
                *rootOp,
                right_scan,
                left_attr_index,
                right_attr_index,
                join_kind
            );
            table_offsets[join.input_table_ref_id] = output_width;
            output_width += table_widths[join.input_table_ref_id];
        }
    }

    std::optional<SelectOperator> selectOpBuffer;
    if (!components.filters.empty() || !components.column_filters.empty()) {
        auto predicate = std::make_unique<ComplexPredicate>(
            ComplexPredicate::LogicOperator::AND
        );
        for (const auto& filter : components.filters) {
            predicate->addPredicate(std::make_unique<SimplePredicate>(
                SimplePredicate::Operand(physicalOffset(filter.column, table_offsets)),
                SimplePredicate::Operand(filter.value->clone()),
                SimplePredicate::ComparisonOperator::EQ
            ));
        }
        for (const auto& filter : components.column_filters) {
            predicate->addPredicate(std::make_unique<SimplePredicate>(
                SimplePredicate::Operand(physicalOffset(filter.left, table_offsets)),
                SimplePredicate::Operand(physicalOffset(filter.right, table_offsets)),
                SimplePredicate::ComparisonOperator::EQ
            ));
        }
        selectOpBuffer.emplace(*rootOp, std::move(predicate));
        rootOp = &*selectOpBuffer;
    }

    std::optional<ProjectionOperator> projectionOpBuffer;
    std::vector<size_t> projected_attrs;
    for (const auto& column : components.select_columns) {
        projected_attrs.push_back(physicalOffset(column, table_offsets));
    }
    projectionOpBuffer.emplace(*rootOp, std::move(projected_attrs));
    rootOp = &*projectionOpBuffer;

    QueryResult result;
    rootOp->open();
    while (rootOp->next()) {
        auto output = rootOp->getOutput();
        if (result.sample_rows.size() < sample_limit) {
            result.sample_rows.push_back(std::move(output));
        }
        result.row_count++;
    }
    rootOp->close();
    return result;
}

void printJoinQueryResult(const QueryComponents& components,
                          const QueryResult& result) {
    std::cout << "Join operator: HashJoin" << std::endl;
    std::cout << "Rows: " << result.row_count << std::endl;
    std::cout << "Sample rows:" << std::endl;
    if (!components.output_names.empty()) {
        std::cout << "  ";
        for (const auto& name : components.output_names) {
            std::cout << name << " ";
        }
        std::cout << std::endl;
    }

    for (const auto& row : result.sample_rows) {
        std::cout << "  ";
        for (const auto& field : row) {
            std::cout << fieldToString(*field) << " ";
        }
        std::cout << std::endl;
    }
}

void executeQuery(const QueryComponents& components, 
                  TableMetadata& metadata,
                  PageManager& page_manager) {
    TableHeap tableHeap(metadata, page_manager);
    ScanOperator scanOp(tableHeap);

    Operator* rootOp = &scanOp;

    std::optional<SelectOperator> selectOpBuffer;
    std::optional<HashAggregationOperator> hashAggOpBuffer;

    if (components.equalityCondition) {
        auto predicate = std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(components.equalityAttributeIndex),
            SimplePredicate::Operand(components.equalityValue->clone()),
            SimplePredicate::ComparisonOperator::EQ
        );

        selectOpBuffer.emplace(*rootOp, std::move(predicate));
        rootOp = &*selectOpBuffer;
    } else if (components.whereAttributeIndex != -1) {
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

        auto complexPredicate = std::make_unique<ComplexPredicate>(ComplexPredicate::LogicOperator::AND);
        complexPredicate->addPredicate(std::move(predicate1));
        complexPredicate->addPredicate(std::move(predicate2));

        selectOpBuffer.emplace(*rootOp, std::move(complexPredicate));
        rootOp = &*selectOpBuffer;
    }

    if (components.aggregateOperation || components.groupBy) {
        std::vector<size_t> groupByAttrs;
        if (components.groupBy) {
            groupByAttrs.push_back(static_cast<size_t>(components.groupByAttributeIndex));
        }
        std::vector<AggrFunc> aggrFuncs{
            {components.aggregateFunction, static_cast<size_t>(components.aggregateAttributeIndex)}
        };

        hashAggOpBuffer.emplace(*rootOp, groupByAttrs, aggrFuncs);
        rootOp = &*hashAggOpBuffer;
    }

    std::cout << "----------------------------------" << std::endl;
    printColumnHeader(components, metadata);
    std::cout << "++++++++++++++++++++++++++++" << std::endl;
    rootOp->open();
    while (rootOp->next()) {
        const auto& output = rootOp->getOutput();
        for (const auto& field : output) {
            field->print();
            std::cout << " ";
        }
        std::cout << std::endl;
    }
    std::cout << "----------------------------------" << std::endl;
    rootOp->close();
}

class InsertOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<Tuple> tupleToInsert;
    bool executed = false;
    size_t insertedCount = 0;

public:
    InsertOperator(TableHeap& tableHeap) : tableHeap(tableHeap) {}

    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {
        executed = false;
        insertedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        if (tupleToInsert && tableHeap.addTuple(std::move(tupleToInsert))) {
            insertedCount = 1;
        }

        executed = true;
        return true;
    }

    void close() override {}

    std::vector<std::unique_ptr<Field>> getOutput() override {
        std::vector<std::unique_ptr<Field>> output;
        output.push_back(std::make_unique<Field>(static_cast<int>(insertedCount)));
        return output;
    }
};

class UpdateOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<IPredicate> predicate;
    std::vector<std::pair<size_t, Field>> assignments;
    bool executed = false;
    size_t updatedCount = 0;

public:
    UpdateOperator(TableHeap& tableHeap,
                   std::unique_ptr<IPredicate> predicate,
                   std::vector<std::pair<size_t, Field>> assignments)
        : tableHeap(tableHeap),
          predicate(std::move(predicate)),
          assignments(std::move(assignments)) {}

    void open() override {
        executed = false;
        updatedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        for (PageID pageId = tableHeap.firstPage();
             pageId != INVALID_PAGE_ID;
             pageId = tableHeap.nextPage(pageId)) {
            for (size_t slot = 0; slot < MAX_SLOTS; ++slot) {
                auto tuple = tableHeap.getTuple(pageId, slot);
                if (!tuple || !predicate->check(tuple->fields)) {
                    continue;
                }

                for (const auto& assignment : assignments) {
                    if (assignment.first >= tuple->fields.size()) {
                        throw std::runtime_error("UPDATE column is out of range.");
                    }
                    tuple->fields[assignment.first] = assignment.second.clone();
                }
                tableHeap.updateTuple(pageId, slot, std::move(tuple));
                updatedCount++;
            }
        }

        executed = true;
        return true;
    }

    void close() override {}

    std::vector<std::unique_ptr<Field>> getOutput() override {
        std::vector<std::unique_ptr<Field>> output;
        output.push_back(std::make_unique<Field>(static_cast<int>(updatedCount)));
        return output;
    }
};

class DeleteOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<IPredicate> predicate;
    bool executed = false;
    size_t deletedCount = 0;

public:
    DeleteOperator(TableHeap& tableHeap,
                   std::unique_ptr<IPredicate> predicate)
        : tableHeap(tableHeap),
          predicate(std::move(predicate)) {}

    void open() override {
        executed = false;
        deletedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        for (PageID pageId = tableHeap.firstPage();
             pageId != INVALID_PAGE_ID;
             pageId = tableHeap.nextPage(pageId)) {
            for (size_t slot = 0; slot < MAX_SLOTS; ++slot) {
                auto tuple = tableHeap.getTuple(pageId, slot);
                if (!tuple || !predicate->check(tuple->fields)) {
                    continue;
                }
                tableHeap.deleteTuple(pageId, slot);
                deletedCount++;
            }
        }

        executed = true;
        return true;
    }

    void close() override {}

    std::vector<std::unique_ptr<Field>> getOutput() override {
        std::vector<std::unique_ptr<Field>> output;
        output.push_back(std::make_unique<Field>(static_cast<int>(deletedCount)));
        return output;
    }
};

std::string TextUtil::trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        start++;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        end--;
    }

    return input.substr(start, end - start);
}

std::vector<std::string> TextUtil::split(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(TextUtil::trim(token));
    }
    return tokens;
}


class QueryOptimizer {
private:
    Catalog& catalog;
    PageManager& page_manager;

    static constexpr size_t MCV_LIMIT = 5;

    static double clampEstimate(double estimate, double input_rows) {
        if (input_rows <= 0.0) {
            return 0.0;
        }
        return std::max(1.0, estimate);
    }

    static std::map<std::string, size_t> topKValueCounts(
            const std::map<std::string, size_t>& value_counts,
            size_t limit) {
        std::vector<std::pair<std::string, size_t>> counts(
            value_counts.begin(),
            value_counts.end()
        );
        std::sort(
            counts.begin(),
            counts.end(),
            [](const auto& left, const auto& right) {
                if (left.second != right.second) {
                    return left.second > right.second;
                }
                return left.first < right.first;
            }
        );

        std::map<std::string, size_t> top_counts;
        for (size_t i = 0; i < std::min(limit, counts.size()); i++) {
            top_counts[counts[i].first] = counts[i].second;
        }
        return top_counts;
    }

    static QueryComponents makeJoinPlanComponents(
            const QueryComponents& source,
            TableRefId base_table_ref_id,
            std::vector<JoinClause> join_order) {
        QueryComponents copy;
        copy.base_table_ref_id = base_table_ref_id;
        copy.table_refs = source.table_refs;
        copy.select_columns = source.select_columns;
        copy.joins = std::move(join_order);
        copy.column_filters = source.column_filters;
        copy.output_names = source.output_names;
        copy.order_by_column = source.order_by_column;

        for (const auto& filter : source.filters) {
            copy.filters.push_back({filter.column, filter.value->clone()});
        }

        // Treat every original ON predicate as a final equality predicate too,
        // so changing the physical join order does not change query semantics.
        for (const auto& join : source.joins) {
            copy.column_filters.push_back({join.left, join.right});
        }
        return copy;
    }

    static std::vector<JoinClause> equalityEdgesForJoinOrdering(
            const QueryComponents& components) {
        std::vector<JoinClause> edges = components.joins;
        for (const auto& equality : components.column_filters) {
            edges.push_back({
                INVALID_TABLE_REF_ID,
                equality.left,
                equality.right
            });
        }
        return edges;
    }

    static std::string joinLabels(const std::vector<std::string>& labels,
                                  const std::string& separator) {
        std::ostringstream output;
        for (size_t i = 0; i < labels.size(); i++) {
            if (i > 0) {
                output << separator;
            }
            output << labels[i];
        }
        return output.str();
    }

    std::string columnLabel(const QueryComponents& components,
                            const ColumnRef& column) {
        return columnRefLabel(components, catalog, column);
    }

    std::string filterLabel(const QueryComponents& components,
                            const FilterClause& filter) {
        return columnLabel(components, filter.column) + " = " +
               fieldToString(*filter.value);
    }

    std::string columnEqualityLabel(
            const QueryComponents& components,
            const ColumnEqualityClause& equality) {
        return columnLabel(components, equality.left) + " = " +
               columnLabel(components, equality.right);
    }

    std::string joinClauseLabel(const QueryComponents& components,
                                const JoinClause& join) {
        return columnLabel(components, join.left) + " = " +
               columnLabel(components, join.right);
    }

    std::vector<std::string> tableFilterLabels(
            const QueryComponents& components,
            TableRefId table_ref_id) {
        std::vector<std::string> labels;
        for (const auto& filter : components.filters) {
            if (filter.column.table_ref_id == table_ref_id) {
                labels.push_back(filterLabel(components, filter));
            }
        }
        return labels;
    }

    std::string projectionLabel(const QueryComponents& components) {
        std::vector<std::string> labels;
        for (const auto& column : components.select_columns) {
            labels.push_back(columnLabel(components, column));
        }
        return joinLabels(labels, ", ");
    }

    static std::pair<ColumnRef, ColumnRef> joinedAndInputColumns(
            const JoinClause& join,
            const std::set<TableRefId>& joined_table_refs) {
        bool left_joined =
            joined_table_refs.find(join.left.table_ref_id) != joined_table_refs.end();
        bool right_joined =
            joined_table_refs.find(join.right.table_ref_id) != joined_table_refs.end();

        if (left_joined && join.right.table_ref_id == join.input_table_ref_id) {
            return {join.left, join.right};
        }
        if (right_joined && join.left.table_ref_id == join.input_table_ref_id) {
            return {join.right, join.left};
        }

        throw std::runtime_error("Join edge is not connected to the current plan.");
    }

    static double tupleScanCost(double tuples) {
        return std::max(0.0, tuples);
    }

    static double tupleComparisonCost(double comparisons) {
        return 0.01 * std::max(0.0, comparisons);
    }

    static double tupleHashCost(double tuples) {
        return 0.05 * std::max(0.0, tuples);
    }

    static double tupleMaterializationCost(double tuples) {
        return 0.10 * std::max(0.0, tuples);
    }

    static double tupleIndexProbeCost(double probes) {
        return 10.0 * std::max(0.0, probes);
    }

    static double tupleFetchByRecordIdCost(double tuples) {
        return 0.50 * std::max(0.0, tuples);
    }

    static double tupleSortCost(double tuples) {
        double safe_tuples = std::max(0.0, tuples);
        if (safe_tuples <= 1.0) {
            return safe_tuples;
        }
        return 0.02 * safe_tuples * std::log2(safe_tuples);
    }

    static double nestedLoopJoinCost(double left_total_cost,
                                     double right_total_cost,
                                     double left_rows,
                                     double right_rows,
                                     double output_rows) {
        return left_total_cost +
               right_total_cost +
               tupleComparisonCost(left_rows * right_rows) +
               tupleMaterializationCost(output_rows);
    }

    static double hashJoinCost(double left_total_cost,
                               double right_total_cost,
                               double left_rows,
                               double right_rows,
                               double output_rows) {
        return left_total_cost +
               right_total_cost +
               tupleHashCost(left_rows + right_rows) +
               tupleMaterializationCost(output_rows);
    }

    static double indexNestedLoopJoinCost(double outer_total_cost,
                                          double outer_rows,
                                          double output_rows) {
        return outer_total_cost +
               tupleIndexProbeCost(outer_rows) +
               tupleFetchByRecordIdCost(output_rows) +
               tupleMaterializationCost(output_rows);
    }

    static double sortMergeJoinCost(double left_total_cost,
                                    double right_total_cost,
                                    double left_rows,
                                    double right_rows,
                                    double output_rows) {
        return left_total_cost +
               right_total_cost +
               tupleComparisonCost(left_rows + right_rows) +
               tupleMaterializationCost(output_rows);
    }

    std::optional<IndexDescriptor> indexDescriptorForColumn(
            const QueryComponents& components,
            const ColumnRef& column,
            const IndexManager* index_manager) {
        if (!index_manager) {
            return std::nullopt;
        }

        const auto& table_ref = tableRefForId(
            components,
            column.table_ref_id
        );
        IndexDescriptor descriptor{
            table_ref.table_id,
            column.column_index
        };
        if (!index_manager->hasIndex(descriptor)) {
            return std::nullopt;
        }
        return descriptor;
    }

    TableStats analyzeTable(TableMetadata& metadata) {
        TableStats stats;
        stats.table_id = metadata.table_id;
        stats.table_name = metadata.name;

        std::vector<std::set<std::string>> distinct_values(
            metadata.schema.columns.size());
        std::vector<std::map<std::string, size_t>> value_counts(
            metadata.schema.columns.size());
        for (size_t column = 0; column < metadata.schema.columns.size(); column++) {
            stats.columns[column].type = metadata.schema.columns[column].type;
        }

        TableHeap table_heap(metadata, page_manager);
        for (PageID page_id = table_heap.firstPage();
             page_id != INVALID_PAGE_ID;
             page_id = table_heap.nextPage(page_id)) {
            stats.page_count++;
            for (size_t slot = 0; slot < MAX_SLOTS; slot++) {
                auto tuple = table_heap.getTuple(page_id, slot);
                if (!tuple) {
                    continue;
                }

                stats.row_count++;
                for (size_t column = 0; column < tuple->fields.size(); column++) {
                    const auto& value = *tuple->fields[column];
                    auto value_string = fieldToString(value);
                    distinct_values[column].insert(value_string);
                    value_counts[column][value_string]++;
                }
            }
        }

        for (size_t column = 0; column < distinct_values.size(); column++) {
            stats.columns[column].distinct_count = distinct_values[column].size();
            stats.columns[column].mcv_counts = topKValueCounts(
                value_counts[column],
                MCV_LIMIT
            );
        }
        return stats;
    }

    static std::map<std::string, double> scaledMcvCounts(
            const std::map<std::string, double>& counts,
            double scale,
            double max_rows) {
        std::map<std::string, double> scaled;
        for (const auto& entry : counts) {
            double count = std::min(max_rows, entry.second * scale);
            if (count > 0.0) {
                scaled[entry.first] = count;
            }
        }
        return scaled;
    }

    static std::map<std::string, double> baseMcvCounts(
            const ColumnStats& column_stats,
            double old_rows,
            double new_rows) {
        std::map<std::string, double> base_counts;
        double scale = old_rows > 0.0 ? new_rows / old_rows : 0.0;
        for (const auto& entry : column_stats.mcv_counts) {
            double count = std::min(new_rows, entry.second * scale);
            if (count > 0.0) {
                base_counts[entry.first] = count;
            }
        }
        return base_counts;
    }

    static void clampRelationColumnsToRows(RelationStats& relation,
                                           double old_rows,
                                           double new_rows) {
        double scale = old_rows > 0.0 ? new_rows / old_rows : 0.0;
        relation.rows = new_rows;
        for (auto& table_columns : relation.columns) {
            for (auto& column_entry : table_columns.second) {
                auto& column_stats = column_entry.second;
                column_stats.distinct_count = std::min(
                    column_stats.distinct_count,
                    new_rows
                );
                if (new_rows <= 1.0) {
                    column_stats.unique = true;
                }
                column_stats.mcv_counts = scaledMcvCounts(
                    column_stats.mcv_counts,
                    scale,
                    new_rows
                );
            }
        }
    }

    void applyEqualityFilterToRelation(RelationStats& relation,
                                       const TableStats& table_stats,
                                       const FilterClause& filter) {
        const auto& base_column_stats =
            table_stats.columns.at(filter.column.column_index);
        double old_rows = relation.rows;
        double new_rows = estimateEqualityFilterRowsWithMcv(
            old_rows,
            base_column_stats,
            *filter.value
        );

        auto& filtered_column =
            relation.columns.at(filter.column.table_ref_id)
                            .at(filter.column.column_index);
        if (filtered_column.unique) {
            new_rows = std::min(new_rows, 1.0);
        }

        clampRelationColumnsToRows(relation, old_rows, new_rows);

        filtered_column.distinct_count = relation.rows > 0.0 ? 1.0 : 0.0;
        filtered_column.unique = relation.rows <= 1.0;
        filtered_column.mcv_counts.clear();
        if (relation.rows > 0.0) {
            filtered_column.mcv_counts[fieldToString(*filter.value)] =
                relation.rows;
        }
    }

    RelationStats makeBaseRelationStats(
            const QueryComponents& components,
            const TableRef& table_ref,
            const std::map<TableId, TableStats>& stats) {
        const auto& table_stats = stats.at(table_ref.table_id);
        RelationStats relation;
        relation.rows = static_cast<double>(table_stats.row_count);

        for (const auto& column_entry : table_stats.columns) {
            size_t column_index = column_entry.first;
            const auto& base_column_stats = column_entry.second;
            ColumnRelationStats column_stats;
            column_stats.type = base_column_stats.type;
            column_stats.distinct_count = std::min(
                static_cast<double>(base_column_stats.distinct_count),
                relation.rows
            );
            column_stats.unique =
                relation.rows > 0.0 &&
                static_cast<double>(base_column_stats.distinct_count) >=
                    relation.rows;
            column_stats.mcv_counts = baseMcvCounts(
                base_column_stats,
                relation.rows,
                relation.rows
            );
            relation.columns[table_ref.id][column_index] = std::move(column_stats);
        }

        for (const auto& filter : components.filters) {
            if (filter.column.table_ref_id != table_ref.id) {
                continue;
            }
            applyEqualityFilterToRelation(
                relation,
                table_stats,
                filter
            );
        }
        return relation;
    }

    std::map<TableRefId, RelationStats> estimateBaseRelations(
            const QueryComponents& components,
            const std::map<TableId, TableStats>& stats) {
        std::map<TableRefId, RelationStats> relations;
        for (const auto& table_ref : components.table_refs) {
            relations[table_ref.id] = makeBaseRelationStats(
                components,
                table_ref,
                stats
            );
        }
        return relations;
    }

    static const ColumnRelationStats& relationColumnStatsFor(
            const RelationStats& relation,
            const ColumnRef& column) {
        return relation.columns.at(column.table_ref_id).at(column.column_index);
    }

    static double estimateJoinRows(double left_rows,
                                   double right_rows,
                                   const ColumnRelationStats& left_stats,
                                   const ColumnRelationStats& right_stats) {
        if (left_rows <= 0.0 || right_rows <= 0.0) {
            return 0.0;
        }

        double denominator = std::max(
            left_stats.distinct_count,
            right_stats.distinct_count
        );
        if (denominator <= 0.0) {
            return 0.0;
        }

        return clampEstimate(left_rows * right_rows / denominator,
                             left_rows * right_rows);
    }

    RelationStats estimateJoinedRelationStats(
            const RelationStats& left,
            const RelationStats& right,
            const ColumnRef& left_column,
            const ColumnRef& right_column,
            double output_rows) {
        RelationStats output;
        output.rows = output_rows;

        auto add_side = [&](const RelationStats& side) {
            double scale = side.rows > 0.0 ? output_rows / side.rows : 0.0;
            bool rows_preserved = output_rows <= side.rows;
            for (const auto& table_columns : side.columns) {
                for (const auto& column_entry : table_columns.second) {
                    ColumnRelationStats column_stats = column_entry.second;
                    column_stats.distinct_count = std::min(
                        column_stats.distinct_count,
                        output_rows
                    );
                    column_stats.unique = column_stats.unique &&
                                          (rows_preserved || output_rows <= 1.0);
                    column_stats.mcv_counts = scaledMcvCounts(
                        column_stats.mcv_counts,
                        scale,
                        output_rows
                    );
                    output.columns[table_columns.first][column_entry.first] =
                        std::move(column_stats);
                }
            }
        };

        add_side(left);
        add_side(right);

        const auto& left_stats = relationColumnStatsFor(left, left_column);
        const auto& right_stats = relationColumnStatsFor(right, right_column);
        double join_distinct = std::min({
            left_stats.distinct_count,
            right_stats.distinct_count,
            output_rows
        });
        output.columns.at(left_column.table_ref_id)
                      .at(left_column.column_index)
                      .distinct_count = join_distinct;
        output.columns.at(right_column.table_ref_id)
                      .at(right_column.column_index)
                      .distinct_count = join_distinct;
        if (output_rows <= 1.0) {
            output.columns.at(left_column.table_ref_id)
                          .at(left_column.column_index)
                          .unique = true;
            output.columns.at(right_column.table_ref_id)
                          .at(right_column.column_index)
                          .unique = true;
        }
        return output;
    }

    static std::string joinedTableRefsTraceLabel(
            const QueryComponents& components,
            const std::set<TableRefId>& table_refs) {
        std::ostringstream output;
        bool first = true;
        for (TableRefId table_ref_id : table_refs) {
            if (!first) {
                output << ", ";
            }
            output << tableRefForId(components, table_ref_id).alias;
            first = false;
        }
        return output.str();
    }

    std::shared_ptr<PhysicalPlanNode> makeTableScanPlanNode(
            TableRefId table_ref_id,
            const RelationStats& relation) {
        auto node = std::make_shared<PhysicalPlanNode>();
        node->op = PhysicalOpKind::TableScan;
        node->table_ref_id = table_ref_id;
        node->table_refs.insert(table_ref_id);
        node->relation = relation;
        node->cost = tupleScanCost(relation.rows);
        return node;
    }

    static bool planDeliversOrder(const PhysicalPlanNode& plan,
                                  const ColumnRef& column) {
        return plan.delivered_order &&
               sameColumnRef(*plan.delivered_order, column);
    }

    std::string orderPropertyLabel(const QueryComponents& components,
                                   const std::optional<ColumnRef>& order) {
        if (!order) {
            return "unordered";
        }
        return "ordered by " + columnLabel(components, *order);
    }

    std::shared_ptr<PhysicalPlanNode> makeSortPlanNode(
            const std::shared_ptr<PhysicalPlanNode>& input,
            const ColumnRef& sort_column,
            double sorted_cost) {
        auto node = std::make_shared<PhysicalPlanNode>();
        node->op = PhysicalOpKind::Sort;
        node->left = input;
        node->sort_column = sort_column;
        node->table_refs = input->table_refs;
        node->relation = input->relation;
        node->delivered_order = sort_column;
        node->cost = sorted_cost;
        return node;
    }

    std::shared_ptr<PhysicalPlanNode> makePhysicalJoinNode(
            const std::shared_ptr<PhysicalPlanNode>& left,
            const std::shared_ptr<PhysicalPlanNode>& right,
            const JoinClause& join,
            const JoinTreeStepEstimate& step) {
        auto left_input = step.sort_left_input
            ? makeSortPlanNode(
                  left,
                  step.left_sort_column,
                  left->cost + tupleSortCost(left->relation.rows)
              )
            : left;
        auto right_input = step.sort_right_input
            ? makeSortPlanNode(
                  right,
                  step.right_sort_column,
                  right->cost + tupleSortCost(right->relation.rows)
              )
            : right;

        auto node = std::make_shared<PhysicalPlanNode>();
        node->op = physicalOpFromJoinKind(step.chosen_join_kind);
        node->left = left_input;
        node->right = right_input;
        node->join = join;
        node->table_refs = left_input->table_refs;
        node->table_refs.insert(right_input->table_refs.begin(),
                                right_input->table_refs.end());
        node->relation = step.output_relation;
        node->has_index_lookup = step.has_index_lookup;
        node->index_lookup_column = step.index_lookup_column;
        node->delivered_order = step.delivered_order;
        node->cost = step.chosen_cost;
        return node;
    }

    static bool planContainsTableRef(const PhysicalPlanNode& node,
                                     TableRefId table_ref_id) {
        return node.table_refs.find(table_ref_id) != node.table_refs.end();
    }

    std::optional<JoinClause> joinEdgeBetweenPlans(
            const PhysicalPlanNode& left,
            const PhysicalPlanNode& right,
            const std::vector<JoinClause>& edges) {
        for (const auto& edge : edges) {
            bool left_has_left = planContainsTableRef(left, edge.left.table_ref_id);
            bool left_has_right = planContainsTableRef(left, edge.right.table_ref_id);
            bool right_has_left = planContainsTableRef(right, edge.left.table_ref_id);
            bool right_has_right = planContainsTableRef(right, edge.right.table_ref_id);

            if (left_has_left && right_has_right) {
                JoinClause oriented = edge;
                oriented.input_table_ref_id = edge.right.table_ref_id;
                return oriented;
            }
            if (left_has_right && right_has_left) {
                JoinClause oriented = edge;
                oriented.input_table_ref_id = edge.left.table_ref_id;
                return oriented;
            }
        }
        return std::nullopt;
    }

    static std::optional<PhysicalJoinKind> physicalJoinKindForMemoOp(
            MemoOpKind op) {
        switch (op) {
            case MemoOpKind::NestedLoopJoin:
                return PhysicalJoinKind::NestedLoopJoin;
            case MemoOpKind::HashJoin:
                return PhysicalJoinKind::HashJoin;
            case MemoOpKind::IndexNestedLoopJoin:
                return PhysicalJoinKind::IndexNestedLoopJoin;
            case MemoOpKind::SortMergeJoin:
                return PhysicalJoinKind::SortMergeJoin;
            case MemoOpKind::LogicalScan:
            case MemoOpKind::LogicalSelect:
            case MemoOpKind::LogicalEquiJoin:
            case MemoOpKind::LogicalProject:
            case MemoOpKind::TableScan:
                return std::nullopt;
        }
        throw std::runtime_error("Unknown memo operator kind.");
    }

    static std::uint64_t maskForTableRefs(
            const std::set<TableRefId>& table_refs,
            const std::map<TableRefId, size_t>& bit_index) {
        std::uint64_t mask = 0;
        for (TableRefId table_ref_id : table_refs) {
            mask |= 1ULL << bit_index.at(table_ref_id);
        }
        return mask;
    }

    static std::set<TableRefId> tableRefsForMask(
            std::uint64_t mask,
            const std::vector<TableRefId>& table_ref_ids) {
        std::set<TableRefId> table_refs;
        for (size_t i = 0; i < table_ref_ids.size(); i++) {
            if (mask & (1ULL << i)) {
                table_refs.insert(table_ref_ids[i]);
            }
        }
        return table_refs;
    }

    static std::map<std::uint64_t, GroupId> buildGroupMaskIndex(
            const Memo& memo,
            const std::map<TableRefId, size_t>& bit_index) {
        std::map<std::uint64_t, GroupId> group_for_mask;
        for (const auto& group : memo.allGroups()) {
            group_for_mask[maskForTableRefs(group.table_refs, bit_index)] =
                group.id;
        }
        return group_for_mask;
    }

    std::optional<JoinClause> joinEdgeBetweenTableRefSets(
            const std::set<TableRefId>& left,
            const std::set<TableRefId>& right,
            const std::vector<JoinClause>& edges) {
        for (const auto& edge : edges) {
            bool left_has_left = left.find(edge.left.table_ref_id) != left.end();
            bool left_has_right = left.find(edge.right.table_ref_id) != left.end();
            bool right_has_left = right.find(edge.left.table_ref_id) != right.end();
            bool right_has_right = right.find(edge.right.table_ref_id) != right.end();

            if (left_has_left && right_has_right) {
                JoinClause oriented = edge;
                oriented.input_table_ref_id = edge.right.table_ref_id;
                return oriented;
            }
            if (left_has_right && right_has_left) {
                JoinClause oriented = edge;
                oriented.input_table_ref_id = edge.left.table_ref_id;
                return oriented;
            }
        }
        return std::nullopt;
    }

    bool memoInputsCanUseIndex(const QueryComponents& components,
                               const Memo& memo,
                               const MemoExpression& expression,
                               const std::vector<JoinClause>& edges,
                               const IndexManager* index_manager) {
        if (!index_manager || expression.inputs.size() != 2) {
            return false;
        }

        const auto& left_group = memo.groupFor(expression.inputs[0]);
        const auto& right_group = memo.groupFor(expression.inputs[1]);
        auto edge = joinEdgeBetweenTableRefSets(
            left_group.table_refs,
            right_group.table_refs,
            edges
        );
        if (!edge) {
            return false;
        }

        auto columns = joinedAndInputColumns(*edge, left_group.table_refs);
        bool left_is_base = left_group.table_refs.size() == 1;
        bool right_is_base = right_group.table_refs.size() == 1;
        return (right_is_base &&
                indexDescriptorForColumn(
                    components,
                    columns.second,
                    index_manager
                )) ||
               (left_is_base &&
                indexDescriptorForColumn(
                    components,
                    columns.first,
                    index_manager
                ));
    }

    std::optional<JoinTreeStepEstimate> estimateJoinTreeStepForKind(
            const std::set<TableRefId>& left_table_refs,
            const JoinClause& join,
            const RelationStats& left_relation,
            double left_cost,
            const RelationStats& right_relation,
            double right_cost,
            PhysicalJoinKind join_kind,
            const QueryComponents& components,
            const PhysicalPlanNode* left_plan,
            const PhysicalPlanNode* right_plan,
            const IndexManager* index_manager) {
        auto columns = joinedAndInputColumns(join, left_table_refs);
        double output_rows = estimateJoinRows(
            left_relation.rows,
            right_relation.rows,
            relationColumnStatsFor(left_relation, columns.first),
            relationColumnStatsFor(right_relation, columns.second)
        );
        RelationStats output_relation = estimateJoinedRelationStats(
            left_relation,
            right_relation,
            columns.first,
            columns.second,
            output_rows
        );

        JoinTreeStepEstimate step;
        step.join = join;
        step.output_rows = output_rows;
        step.output_relation = std::move(output_relation);
        step.chosen_join_kind = join_kind;

        if (join_kind == PhysicalJoinKind::NestedLoopJoin) {
            step.chosen_cost = nestedLoopJoinCost(
                left_cost,
                right_cost,
                left_relation.rows,
                right_relation.rows,
                output_rows
            );
            return step;
        }

        if (join_kind == PhysicalJoinKind::HashJoin) {
            step.chosen_cost = hashJoinCost(
                left_cost,
                right_cost,
                left_relation.rows,
                right_relation.rows,
                output_rows
            );
            return step;
        }

        if (join_kind == PhysicalJoinKind::SortMergeJoin) {
            step.left_sort_column = columns.first;
            step.right_sort_column = columns.second;
            step.sort_left_input =
                !left_plan || !planDeliversOrder(*left_plan, columns.first);
            step.sort_right_input =
                !right_plan || !planDeliversOrder(*right_plan, columns.second);
            double ordered_left_cost = left_cost +
                (step.sort_left_input ? tupleSortCost(left_relation.rows) : 0.0);
            double ordered_right_cost = right_cost +
                (step.sort_right_input ? tupleSortCost(right_relation.rows) : 0.0);
            step.delivered_order = columns.first;
            step.chosen_cost = sortMergeJoinCost(
                ordered_left_cost,
                ordered_right_cost,
                left_relation.rows,
                right_relation.rows,
                output_rows
            );
            return step;
        }

        std::optional<ColumnRef> index_lookup_column;
        double best_index_cost = std::numeric_limits<double>::infinity();
        auto considerIndexJoin =
                [&](const PhysicalPlanNode* indexed_plan,
                    const ColumnRef& indexed_column,
                    double outer_rows,
                    double outer_cost) {
            if (!indexed_plan || !indexed_plan->isTableScan()) {
                return;
            }
            if (!indexDescriptorForColumn(
                    components,
                    indexed_column,
                    index_manager
                )) {
                return;
            }

            double index_cost = indexNestedLoopJoinCost(
                outer_cost,
                outer_rows,
                output_rows
            );
            if (index_cost < best_index_cost) {
                best_index_cost = index_cost;
                index_lookup_column = indexed_column;
            }
        };

        considerIndexJoin(
            right_plan,
            columns.second,
            left_relation.rows,
            left_cost
        );
        considerIndexJoin(
            left_plan,
            columns.first,
            right_relation.rows,
            right_cost
        );

        if (!index_lookup_column) {
            return std::nullopt;
        }

        step.has_index_lookup = true;
        step.index_lookup_column = *index_lookup_column;
        step.chosen_cost = best_index_cost;
        return step;
    }

    void appendPhysicalPlanTree(
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& node,
            size_t depth,
            std::ostringstream& output) {
        if (!node) {
            return;
        }

        output << "  " << std::string(depth * 2, ' ');
        if (node->isTableScan()) {
            output << tableRefForId(components, node->table_ref_id).alias
                   << " = TableScan"
                   << ", est rows=" << formatEstimate(node->relation.rows)
                   << ", cost=" << formatEstimate(node->cost)
                   << "\n";
            return;
        }
        if (node->isSort()) {
            output << "Sort ["
                   << orderPropertyLabel(components, node->delivered_order)
                   << "], est rows=" << formatEstimate(node->relation.rows)
                   << ", cost=" << formatEstimate(node->cost)
                   << "\n";
            appendPhysicalPlanTree(components, node->left, depth + 1, output);
            return;
        }

        output << physicalOpKindName(node->op)
               << " {" << joinedTableRefsTraceLabel(components, node->left->table_refs)
               << "} x {"
               << joinedTableRefsTraceLabel(components, node->right->table_refs)
               << "}, est rows=" << formatEstimate(node->relation.rows)
               << ", cost=" << formatEstimate(node->cost)
               << ", delivers="
               << orderPropertyLabel(components, node->delivered_order);
        if (node->op == PhysicalOpKind::IndexNestedLoopJoin) {
            output << ", index lookup on "
                   << columnRefLabel(components, catalog, node->index_lookup_column);
        }
        output << "\n";
        appendPhysicalPlanTree(components, node->left, depth + 1, output);
        appendPhysicalPlanTree(components, node->right, depth + 1, output);
    }

    std::string prettyPhysicalPlanTree(
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& node) {
        std::ostringstream output;
        appendPhysicalPlanTree(components, node, 0, output);
        return output.str();
    }

    static bool betterMemoPlanChoice(const MemoPlanChoice& candidate,
                                     const MemoPlanChoice& incumbent) {
        if (candidate.cost != incumbent.cost) {
            return candidate.cost < incumbent.cost;
        }
        if (candidate.relation.rows != incumbent.relation.rows) {
            return candidate.relation.rows < incumbent.relation.rows;
        }
        return candidate.table_refs < incumbent.table_refs;
    }

    MemoPlanChoice enforceRequiredOrder(
            MemoPlanChoice candidate,
            const std::optional<ColumnRef>& required_order) {
        if (!required_order ||
            planDeliversOrder(*candidate.plan_root, *required_order)) {
            return candidate;
        }

        double sorted_cost =
            candidate.cost + tupleSortCost(candidate.relation.rows);
        candidate.plan_root = makeSortPlanNode(
            candidate.plan_root,
            *required_order,
            sorted_cost
        );
        candidate.cost = sorted_cost;
        return candidate;
    }

    MemoRuleStats applyMemoRules(
            Memo& memo,
            const QueryComponents& components,
            const IndexManager* index_manager) {
        MemoRuleStats stats;
        stats.initial_groups = memo.groupCount();
        stats.initial_expressions = memo.expressionCount();

        if (components.table_refs.empty() || components.table_refs.size() > 62) {
            throw std::runtime_error("Memo rules support 1..62 table refs.");
        }

        std::vector<TableRefId> table_ref_ids;
        std::map<TableRefId, size_t> bit_index;
        for (size_t i = 0; i < components.table_refs.size(); i++) {
            table_ref_ids.push_back(components.table_refs[i].id);
            bit_index[components.table_refs[i].id] = i;
        }

        std::uint64_t full_mask =
            (1ULL << components.table_refs.size()) - 1ULL;
        auto edges = equalityEdgesForJoinOrdering(components);
        auto group_for_mask = buildGroupMaskIndex(memo, bit_index);

        for (size_t level = 2; level <= components.table_refs.size(); level++) {
            for (std::uint64_t mask = 1; mask <= full_mask; mask++) {
                if (__builtin_popcountll(mask) != static_cast<int>(level)) {
                    continue;
                }

                for (std::uint64_t left_mask = (mask - 1ULL) & mask;
                     left_mask != 0;
                     left_mask = (left_mask - 1ULL) & mask) {
                    std::uint64_t right_mask = mask ^ left_mask;
                    if (right_mask == 0 || left_mask > right_mask) {
                        continue;
                    }

                    auto left_group = group_for_mask.find(left_mask);
                    auto right_group = group_for_mask.find(right_mask);
                    if (left_group == group_for_mask.end() ||
                        right_group == group_for_mask.end()) {
                        continue;
                    }

                    auto left_refs = tableRefsForMask(left_mask, table_ref_ids);
                    auto right_refs = tableRefsForMask(right_mask, table_ref_ids);
                    auto edge = joinEdgeBetweenTableRefSets(
                        left_refs,
                        right_refs,
                        edges
                    );
                    if (!edge) {
                        continue;
                    }

                    std::set<TableRefId> output_refs = left_refs;
                    output_refs.insert(right_refs.begin(), right_refs.end());
                    std::string label = joinClauseLabel(components, *edge);

                    auto output_group = group_for_mask.find(mask);
                    if (output_group == group_for_mask.end()) {
                        GroupId group_id = memo.addGroup(
                            output_refs,
                            MemoOpKind::LogicalEquiJoin,
                            {left_group->second, right_group->second},
                            label
                        );
                        group_for_mask[mask] = group_id;
                        stats.logical_join_alternatives++;
                    } else if (memo.addExpressionToGroup(
                                   output_group->second,
                                   MemoOpKind::LogicalEquiJoin,
                                   {left_group->second, right_group->second},
                                   label)) {
                        stats.logical_join_alternatives++;
                    }
                }
            }
        }

        size_t group_count = memo.groupCount();
        for (GroupId group_id = 1; group_id <= group_count; group_id++) {
            auto expressions = memo.groupFor(group_id).expressions;
            for (const auto& expression : expressions) {
                if (expression.op == MemoOpKind::LogicalScan) {
                    if (memo.addExpressionToGroup(
                            group_id,
                            MemoOpKind::TableScan,
                            {},
                            expression.label)) {
                        stats.scan_implementations++;
                    }
                    continue;
                }

                if (expression.op != MemoOpKind::LogicalEquiJoin) {
                    continue;
                }

                if (memo.addExpressionToGroup(
                        group_id,
                        MemoOpKind::NestedLoopJoin,
                        expression.inputs,
                        expression.label)) {
                    stats.join_implementations++;
                }
                if (memo.addExpressionToGroup(
                        group_id,
                        MemoOpKind::HashJoin,
                        expression.inputs,
                        expression.label)) {
                    stats.join_implementations++;
                }
                if (memo.addExpressionToGroup(
                        group_id,
                        MemoOpKind::SortMergeJoin,
                        expression.inputs,
                        expression.label)) {
                    stats.join_implementations++;
                    stats.sort_merge_implementations++;
                }
                if (memoInputsCanUseIndex(
                        components,
                        memo,
                        expression,
                        edges,
                        index_manager) &&
                    memo.addExpressionToGroup(
                        group_id,
                        MemoOpKind::IndexNestedLoopJoin,
                        expression.inputs,
                        expression.label)) {
                    stats.join_implementations++;
                }
            }
        }

        stats.final_groups = memo.groupCount();
        stats.final_expressions = memo.expressionCount();
        return stats;
    }

    static size_t countPlanJoinKind(
            const std::shared_ptr<PhysicalPlanNode>& node,
            PhysicalJoinKind kind) {
        if (!node) {
            return 0;
        }
        if (node->isTableScan()) {
            return 0;
        }
        if (node->isSort()) {
            return countPlanJoinKind(node->left, kind);
        }
        size_t count = node->joinKind() == kind ? 1 : 0;
        return count +
               countPlanJoinKind(node->left, kind) +
               countPlanJoinKind(node->right, kind);
    }

    static size_t countPlanOpKind(
            const std::shared_ptr<PhysicalPlanNode>& node,
            PhysicalOpKind kind) {
        if (!node) {
            return 0;
        }
        size_t count = node->op == kind ? 1 : 0;
        return count +
               countPlanOpKind(node->left, kind) +
               countPlanOpKind(node->right, kind);
    }

    bool recordCascadesCandidate(
            GroupId group_id,
            MemoPlanChoice candidate,
            GroupId root_group_id,
            const std::optional<ColumnRef>& required_order,
            std::map<GroupId, MemoPlanChoice>& winners,
            MemoRuleStats& stats) {
        if (group_id == root_group_id) {
            candidate = enforceRequiredOrder(
                std::move(candidate),
                required_order
            );
        }
        stats.winner_candidates++;
        auto winner = winners.find(group_id);
        bool improved =
            winner == winners.end() ||
            betterMemoPlanChoice(candidate, winner->second);
        if (!improved) {
            return false;
        }
        winners[group_id] = candidate;
        return true;
    }

    void scheduleCascadesInput(
            std::deque<CascadesTask>& task_queue,
            const CascadesTask& retry_task,
            GroupId input_group_id) {
        task_queue.push_front(retry_task);
        task_queue.push_front(CascadesTask::optimizeInput(input_group_id));
    }

    bool cascadesInputReady(
            GroupId group_id,
            const std::map<GroupId, MemoPlanChoice>& winners) {
        return winners.find(group_id) != winners.end();
    }

    static GroupId physicalRootGroupId(const Memo& memo) {
        GroupId group_id = memo.finalGroupId();
        while (true) {
            const auto& group = memo.groupFor(group_id);
            if (group.expressions.size() != 1) {
                return group_id;
            }

            const auto& expression = group.expressions.front();
            if (expression.op != MemoOpKind::LogicalProject ||
                expression.inputs.size() != 1) {
                return group_id;
            }
            group_id = expression.inputs[0];
        }
    }

    void applyCascadesExpression(
            const Memo& memo,
            GroupId group_id,
            size_t expression_index,
            const QueryComponents& components,
            const std::map<TableRefId, RelationStats>& base_relations,
            const std::vector<JoinClause>& edges,
            const IndexManager* index_manager,
            std::map<GroupId, MemoPlanChoice>& winners,
            std::deque<CascadesTask>& task_queue,
            MemoRuleStats& stats,
            GroupId root_group_id,
            const std::optional<ColumnRef>& required_order) {
        const auto& group = memo.groupFor(group_id);
        if (expression_index >= group.expressions.size()) {
            return;
        }

        const auto& expression = group.expressions[expression_index];
        if ((expression.op == MemoOpKind::LogicalSelect ||
             expression.op == MemoOpKind::LogicalProject) &&
            expression.inputs.size() == 1) {
            GroupId input_group_id = expression.inputs[0];
            if (!cascadesInputReady(input_group_id, winners)) {
                scheduleCascadesInput(
                    task_queue,
                    CascadesTask::applyRule(group_id, expression_index),
                    input_group_id
                );
                return;
            }
            recordCascadesCandidate(
                group_id,
                winners.at(input_group_id),
                root_group_id,
                required_order,
                winners,
                stats
            );
            return;
        }

        if (expression.op == MemoOpKind::TableScan) {
            if (group.table_refs.size() != 1) {
                return;
            }
            TableRefId table_ref_id = *group.table_refs.begin();
            auto plan = makeTableScanPlanNode(
                table_ref_id,
                base_relations.at(table_ref_id)
            );
            recordCascadesCandidate(
                group_id,
                MemoPlanChoice{
                    plan,
                    plan->relation,
                    plan->table_refs,
                    plan->cost
                },
                root_group_id,
                required_order,
                winners,
                stats
            );
            return;
        }

        auto join_kind = physicalJoinKindForMemoOp(expression.op);
        if (!join_kind || expression.inputs.size() != 2) {
            return;
        }

        GroupId left_group_id = expression.inputs[0];
        GroupId right_group_id = expression.inputs[1];
        if (!cascadesInputReady(left_group_id, winners)) {
            scheduleCascadesInput(
                task_queue,
                CascadesTask::applyRule(group_id, expression_index),
                left_group_id
            );
            return;
        }
        if (!cascadesInputReady(right_group_id, winners)) {
            scheduleCascadesInput(
                task_queue,
                CascadesTask::applyRule(group_id, expression_index),
                right_group_id
            );
            return;
        }

        const auto& left = winners.at(left_group_id);
        const auto& right = winners.at(right_group_id);
        auto edge = joinEdgeBetweenPlans(
            *left.plan_root,
            *right.plan_root,
            edges
        );
        if (!edge) {
            return;
        }

        auto step = estimateJoinTreeStepForKind(
            left.table_refs,
            *edge,
            left.relation,
            left.cost,
            right.relation,
            right.cost,
            *join_kind,
            components,
            left.plan_root.get(),
            right.plan_root.get(),
            index_manager
        );
        if (!step) {
            return;
        }

        auto plan = makePhysicalJoinNode(
            left.plan_root,
            right.plan_root,
            *edge,
            *step
        );
        recordCascadesCandidate(
            group_id,
            MemoPlanChoice{
                plan,
                plan->relation,
                plan->table_refs,
                plan->cost
            },
            root_group_id,
            required_order,
            winners,
            stats
        );
    }

public:
    QueryOptimizer(Catalog& catalog, PageManager& page_manager)
        : catalog(catalog), page_manager(page_manager) {}

    Memo buildInitialMemo(const QueryComponents& components) {
        Memo memo;
        std::map<TableRefId, GroupId> table_groups;

        for (const auto& table_ref : components.table_refs) {
            std::set<TableRefId> table_refs{table_ref.id};
            GroupId scan_group = memo.addGroup(
                table_refs,
                MemoOpKind::LogicalScan,
                {},
                table_ref.alias + " : " + table_ref.table_name
            );

            auto filters = tableFilterLabels(components, table_ref.id);
            if (!filters.empty()) {
                table_groups[table_ref.id] = memo.addGroup(
                    table_refs,
                    MemoOpKind::LogicalSelect,
                    {scan_group},
                    joinLabels(filters, " AND ")
                );
            } else {
                table_groups[table_ref.id] = scan_group;
            }
        }

        auto base_group = table_groups.find(components.base_table_ref_id);
        if (base_group == table_groups.end()) {
            throw std::runtime_error("Memo build could not find base table.");
        }

        GroupId current_group = base_group->second;
        std::set<TableRefId> joined_table_refs{components.base_table_ref_id};

        for (const auto& join : components.joins) {
            auto input_group = table_groups.find(join.input_table_ref_id);
            if (input_group == table_groups.end()) {
                throw std::runtime_error("Memo build could not find JOIN input.");
            }

            std::set<TableRefId> output_table_refs = joined_table_refs;
            output_table_refs.insert(join.input_table_ref_id);
            current_group = memo.addGroup(
                output_table_refs,
                MemoOpKind::LogicalEquiJoin,
                {current_group, input_group->second},
                joinClauseLabel(components, join)
            );
            joined_table_refs = std::move(output_table_refs);
        }

        if (!components.column_filters.empty()) {
            std::vector<std::string> labels;
            for (const auto& filter : components.column_filters) {
                labels.push_back(columnEqualityLabel(components, filter));
            }
            current_group = memo.addGroup(
                joined_table_refs,
                MemoOpKind::LogicalSelect,
                {current_group},
                joinLabels(labels, " AND ")
            );
        }

        current_group = memo.addGroup(
            joined_table_refs,
            MemoOpKind::LogicalProject,
            {current_group},
            projectionLabel(components)
        );
        memo.setFinalGroupId(current_group);
        return memo;
    }

    MemoOptimizationResult optimizeWithCascadesQueue(
            const QueryComponents& components,
            const std::map<TableId, TableStats>& stats,
            const IndexManager* index_manager = nullptr) {
        auto memo = buildInitialMemo(components);
        auto rule_stats = applyMemoRules(
            memo,
            components,
            index_manager
        );
        auto base_relations = estimateBaseRelations(components, stats);
        auto edges = equalityEdgesForJoinOrdering(components);

        std::map<GroupId, MemoPlanChoice> winners;
        std::set<GroupId> optimized_groups;
        std::deque<CascadesTask> task_queue;
        GroupId root_group_id = physicalRootGroupId(memo);
        task_queue.push_back(CascadesTask::optimizeGroup(root_group_id));

        while (!task_queue.empty()) {
            auto task = task_queue.front();
            task_queue.pop_front();
            rule_stats.tasks_executed++;

            if (task.kind == CascadesTaskKind::OptimizeGroup) {
                rule_stats.optimize_group_tasks++;
                if (!optimized_groups.insert(task.group_id).second) {
                    continue;
                }

                const auto& group = memo.groupFor(task.group_id);
                for (size_t i = group.expressions.size(); i > 0; i--) {
                    task_queue.push_front(
                        CascadesTask::applyRule(task.group_id, i - 1)
                    );
                }
            } else if (task.kind == CascadesTaskKind::ApplyRule) {
                rule_stats.apply_rule_tasks++;
                applyCascadesExpression(
                    memo,
                    task.group_id,
                    task.expression_index,
                    components,
                    base_relations,
                    edges,
                    index_manager,
                    winners,
                    task_queue,
                    rule_stats,
                    root_group_id,
                    components.order_by_column
                );
            } else if (task.kind == CascadesTaskKind::OptimizeInput) {
                rule_stats.optimize_input_tasks++;
                if (winners.find(task.group_id) == winners.end() &&
                    optimized_groups.find(task.group_id) == optimized_groups.end()) {
                    task_queue.push_front(
                        CascadesTask::optimizeGroup(task.group_id)
                    );
                }
            }
        }

        auto final_choice = winners.find(root_group_id);
        if (final_choice == winners.end()) {
            throw std::runtime_error("Cascades queue found no executable plan.");
        }

        auto planned_components = makeJoinPlanComponents(
            components,
            components.base_table_ref_id,
            components.joins
        );
        rule_stats.sort_enforcers = countPlanOpKind(
            final_choice->second.plan_root,
            PhysicalOpKind::Sort
        );
        return {
            std::move(planned_components),
            rule_stats,
            final_choice->second.plan_root,
            final_choice->second.relation.rows,
            final_choice->second.cost,
            countPlanJoinKind(
                final_choice->second.plan_root,
                PhysicalJoinKind::IndexNestedLoopJoin
            ),
            countPlanJoinKind(
                final_choice->second.plan_root,
                PhysicalJoinKind::SortMergeJoin
            ),
            countPlanOpKind(
                final_choice->second.plan_root,
                PhysicalOpKind::Sort
            )
        };
    }

    std::map<TableId, TableStats> analyzeQueryTables(
            const QueryComponents& components) {
        std::map<TableId, TableStats> stats;
        for (const auto& table_ref : components.table_refs) {
            if (stats.find(table_ref.table_id) != stats.end()) {
                continue;
            }
            auto& metadata = catalog.getTable(table_ref.table_id);
            stats[table_ref.table_id] = analyzeTable(metadata);
        }
        return stats;
    }

    double estimateEqualityFilterRowsWithMcv(double input_rows,
                                             const ColumnStats& column_stats,
                                             const Field& value) {
        if (input_rows <= 0.0 || column_stats.distinct_count == 0) {
            return 0.0;
        }

        auto value_it = column_stats.mcv_counts.find(fieldToString(value));
        if (value_it != column_stats.mcv_counts.end()) {
            return static_cast<double>(value_it->second);
        }

        size_t mcv_rows = 0;
        for (const auto& mcv : column_stats.mcv_counts) {
            mcv_rows += mcv.second;
        }
        size_t remaining_distinct =
            column_stats.distinct_count > column_stats.mcv_counts.size()
                ? column_stats.distinct_count - column_stats.mcv_counts.size()
                : 0;
        if (remaining_distinct == 0) {
            return 1.0;
        }

        double remaining_rows = std::max(
            0.0,
            input_rows - static_cast<double>(mcv_rows)
        );
        return clampEstimate(
            remaining_rows / static_cast<double>(remaining_distinct),
            input_rows
        );
    }

    void printPhysicalPlanTree(const std::string& title,
                               const QueryComponents& components,
                               const std::shared_ptr<PhysicalPlanNode>& plan_root) {
        std::cout << "\n" << title << ":" << std::endl;
        std::cout << prettyPhysicalPlanTree(components, plan_root);
    }

};


void checkStatementCrashLimit(int& statementsSeen, int crashAfterStatement) {
    if (crashAfterStatement <= 0) {
        return;
    }

    statementsSeen++;
    if (statementsSeen == crashAfterStatement) {
        throw std::runtime_error(
            "Simulated crash after statement " + std::to_string(statementsSeen)
        );
    }
}

struct TxnContext {
    int id;
    enum State { RUNNING, COMMITTED, ABORTED } state = RUNNING;
};

using TxnPtr = std::shared_ptr<TxnContext>;

enum class LockMode { IS, IX, S, X };

enum class ConcurrencyControlResourceKind { Table, Tuple };

struct ConcurrencyControlResource {
    ConcurrencyControlResourceKind kind;
    TableId table_id;
    PageID page_id = INVALID_PAGE_ID;
    size_t slot_id = 0;

    static ConcurrencyControlResource table(TableId table_id) {
        return {ConcurrencyControlResourceKind::Table, table_id};
    }

    static ConcurrencyControlResource tuple(TableId table_id,
                                            PageID page_id,
                                            size_t slot_id) {
        return {ConcurrencyControlResourceKind::Tuple,
                table_id,
                page_id,
                slot_id};
    }

    bool operator<(const ConcurrencyControlResource& other) const {
        if (kind != other.kind) {
            return kind < other.kind;
        }
        if (table_id != other.table_id) {
            return table_id < other.table_id;
        }
        if (page_id != other.page_id) {
            return page_id < other.page_id;
        }
        if (slot_id != other.slot_id) {
            return slot_id < other.slot_id;
        }
        return false;
    }
};

class DirectedGraph {
public:
    void setOutgoingEdges(int from, const std::vector<int>& to_nodes) {
        adjacency[from] = to_nodes;
        for (int to : to_nodes) {
            adjacency[to];
        }
    }

    void removeNode(int node) {
        adjacency.erase(node);
        for (auto& entry : adjacency) {
            auto& edges = entry.second;
            edges.erase(
                std::remove(edges.begin(), edges.end(), node),
                edges.end()
            );
        }
    }

    std::vector<int> cycleFrom(int node) const {
        std::vector<int> path;
        std::map<int, bool> visited;
        if (findCycleToTarget(node, node, visited, path)) {
            path.push_back(node);
            return path;
        }
        return {};
    }

private:
    std::map<int, std::vector<int>> adjacency;

    bool findCycleToTarget(int current,
                           int target,
                           std::map<int, bool>& visited,
                           std::vector<int>& path) const {
        visited[current] = true;
        path.push_back(current);

        auto it = adjacency.find(current);
        if (it != adjacency.end()) {
            for (int next : it->second) {
                if (next == target) {
                    return true;
                }
                if (!visited[next] &&
                    findCycleToTarget(next, target, visited, path)) {
                    return true;
                }
            }
        }

        path.pop_back();
        return false;
    }
};

class LockManager {
private:
    struct LockGrant {
        int txn_id;
        LockMode mode;
    };

    std::map<ConcurrencyControlResource, std::vector<LockGrant>> locks;
    DirectedGraph waits_for;
    std::mutex latch;
    std::condition_variable lock_cv;

public:
    struct LockRequestResult {
        bool granted = false;
        std::vector<int> blockers;
    };

    struct AcquireResult {
        bool waited = false;
        bool deadlock = false;
        std::vector<int> cycle;
    };

    bool hasLock(int txn_id, const ConcurrencyControlResource& resource, LockMode mode) {
        std::lock_guard<std::mutex> guard(latch);
        auto it = locks.find(resource);
        if (it == locks.end()) {
            return false;
        }
        for (const auto& grant : it->second) {
            if (grant.txn_id == txn_id && grant.mode == mode) {
                return true;
            }
        }
        return false;
    }

    AcquireResult acquire(int txn_id,
                          const ConcurrencyControlResource& resource,
                          LockMode mode) {
        std::unique_lock<std::mutex> guard(latch);
        bool waited = false;
        while (true) {
            auto attempt = tryAcquireLocked(txn_id, resource, mode);
            if (attempt.granted) {
                waits_for.removeNode(txn_id);
                AcquireResult result;
                result.waited = waited;
                return result;
            }
            waits_for.setOutgoingEdges(txn_id, attempt.blockers);
            if (!waited) {
                waited = true;
            }
            auto cycle = waits_for.cycleFrom(txn_id);
            if (!cycle.empty()) {
                waits_for.removeNode(txn_id);
                AcquireResult result;
                result.waited = waited;
                result.deadlock = true;
                result.cycle = std::move(cycle);
                return result;
            }
            lock_cv.wait(guard);
        }
    }

    void releaseAll(int txn_id) {
        std::lock_guard<std::mutex> guard(latch);
        bool released = false;
        waits_for.removeNode(txn_id);
        for (auto it = locks.begin(); it != locks.end();) {
            auto& grants = it->second;
            auto old_size = grants.size();
            grants.erase(
                std::remove_if(
                    grants.begin(),
                    grants.end(),
                    [txn_id](const LockGrant& grant) {
                        return grant.txn_id == txn_id;
                    }),
                grants.end()
            );
            released = released || old_size != grants.size();
            if (grants.empty()) {
                it = locks.erase(it);
            } else {
                ++it;
            }
        }
        if (released) {
            lock_cv.notify_all();
        }
    }

private:
    LockRequestResult tryAcquireLocked(int txn_id,
                                       const ConcurrencyControlResource& resource,
                                       LockMode mode) {
        auto& grants = locks[resource];
        LockMode effectiveMode = mode;
        for (const auto& grant : grants) {
            if (grant.txn_id == txn_id) {
                effectiveMode = coalescedMode(grant.mode, mode);
                break;
            }
        }

        auto blockers = blockersForLocked(txn_id, grants, effectiveMode);
        if (!blockers.empty()) {
            LockRequestResult result;
            result.blockers = std::move(blockers);
            return result;
        }

        for (auto& grant : grants) {
            if (grant.txn_id == txn_id) {
                grant.mode = effectiveMode;
                LockRequestResult result;
                result.granted = true;
                return result;
            }
        }

        grants.push_back({txn_id, effectiveMode});
        LockRequestResult result;
        result.granted = true;
        return result;
    }

    std::vector<int> blockersForLocked(int txn_id,
                                       const std::vector<LockGrant>& grants,
                                       LockMode mode) const {
        std::vector<int> blockers;
        for (const auto& grant : grants) {
            if (grant.txn_id != txn_id && !compatible(mode, grant.mode)) {
                blockers.push_back(grant.txn_id);
            }
        }
        return blockers;
    }

    static bool compatible(LockMode requested, LockMode held) {
        switch (requested) {
            case LockMode::IS:
                return held == LockMode::IS ||
                       held == LockMode::IX ||
                       held == LockMode::S;
            case LockMode::IX:
                return held == LockMode::IS ||
                       held == LockMode::IX;
            case LockMode::S:
                return held == LockMode::IS ||
                       held == LockMode::S;
            case LockMode::X:
                return false;
        }
        throw std::runtime_error("Unknown lock mode.");
    }

    static bool strongerOrEqual(LockMode held, LockMode requested) {
        if (held == requested || held == LockMode::X) {
            return true;
        }
        if (held == LockMode::S && requested == LockMode::IS) {
            return true;
        }
        if (held == LockMode::IX && requested == LockMode::IS) {
            return true;
        }
        return false;
    }

    static LockMode coalescedMode(LockMode held, LockMode requested) {
        if (strongerOrEqual(held, requested)) {
            return held;
        }
        if (strongerOrEqual(requested, held)) {
            return requested;
        }
        return LockMode::X;
    }
};

enum class AccessType { Read, Write };

static LockMode tableLockModeForAccess(AccessType type) {
    return type == AccessType::Read ? LockMode::S : LockMode::X;
}

struct LockRequest {
    ConcurrencyControlResource resource;
    LockMode mode;
};

struct ConcurrencyControlRequest {
    int txn_id;
    std::vector<LockRequest> locks;
};

struct ConcurrencyControlResult {
    bool granted = true;
    bool waited = false;
    bool deadlock = false;
    std::vector<int> cycle;
};

class ConcurrencyControlPolicy {
public:
    virtual ~ConcurrencyControlPolicy() = default;
    virtual void begin(int txn_id) = 0;
    virtual ConcurrencyControlResult beforeAccess(
        const ConcurrencyControlRequest& request) = 0;
    virtual ConcurrencyControlResult beforeCommit(int txn_id) = 0;
    virtual void afterCommit(int txn_id) = 0;
    virtual void abort(int txn_id) = 0;
};

class TwoPhaseLockingPolicy : public ConcurrencyControlPolicy {
private:
    LockManager& lock_manager;

public:
    explicit TwoPhaseLockingPolicy(LockManager& lock_manager)
        : lock_manager(lock_manager) {}

    void begin(int) override {}

    ConcurrencyControlResult beforeAccess(
            const ConcurrencyControlRequest& request) override {
        ConcurrencyControlResult policy_result;

        for (const auto& lock : request.locks) {
            auto lock_result = lock_manager.acquire(
                request.txn_id,
                lock.resource,
                lock.mode
            );
            policy_result.waited = policy_result.waited || lock_result.waited;
            if (lock_result.deadlock) {
                policy_result.granted = false;
                policy_result.deadlock = true;
                policy_result.cycle = std::move(lock_result.cycle);
                return policy_result;
            }
        }

        return policy_result;
    }

    ConcurrencyControlResult beforeCommit(int) override {
        return {};
    }

    void afterCommit(int txn_id) override {
        lock_manager.releaseAll(txn_id);
    }

    void abort(int txn_id) override {
        lock_manager.releaseAll(txn_id);
    }
};

class TransactionManager {
private:
    LockManager lock_manager;
    std::unique_ptr<ConcurrencyControlPolicy> concurrency_control_policy;

public:
    TransactionManager() {
        useTwoPhaseLockingPolicy();
    }

    void useTwoPhaseLockingPolicy() {
        concurrency_control_policy =
            std::make_unique<TwoPhaseLockingPolicy>(lock_manager);
    }

    TxnPtr begin(int txn_id) {
        auto txn = std::make_shared<TxnContext>(TxnContext{txn_id});
        concurrency_control_policy->begin(txn_id);
        return txn;
    }

    ConcurrencyControlResult requestAccess(
            const TxnPtr& txn,
            const std::vector<LockRequest>& locks) {
        if (!txn || locks.empty()) {
            return {};
        }
        return concurrency_control_policy->beforeAccess({
            txn->id,
            locks
        });
    }

    ConcurrencyControlResult prepareCommit(const TxnContext& txn) {
        return concurrency_control_policy->beforeCommit(txn.id);
    }

    void commit(TxnContext& txn) {
        txn.state = TxnContext::COMMITTED;
        concurrency_control_policy->afterCommit(txn.id);
    }

    void abort(TxnContext& txn) {
        txn.state = TxnContext::ABORTED;
        concurrency_control_policy->abort(txn.id);
    }
};

class TransactionalStorageManager {
public:
    LogManager log_manager;
    BufferManager buffer_manager;
    RecoveryManager recovery_manager;
    PageManager page_manager;
    Catalog catalog;
    IndexManager index_manager;
    TransactionManager txn_manager;

    TransactionalStorageManager()
        : log_manager(),
          buffer_manager(log_manager),
          recovery_manager(buffer_manager, log_manager),
          page_manager(buffer_manager, recovery_manager),
          catalog(buffer_manager, page_manager),
          index_manager(catalog, page_manager),
          txn_manager() {
        recovery_manager.recover();
        catalog.load();
    }

    TxnPtr beginTransaction() {
        int txn_id = recovery_manager.begin();
        return txn_manager.begin(txn_id);
    }

    void commit(const TxnPtr& txn) {
        requireRunningTransaction(txn);
        auto cc_result = txn_manager.prepareCommit(*txn);
        if (!cc_result.granted) {
            abort(txn);
            throw std::runtime_error(
                "Concurrency-control policy rejected COMMIT for txn " +
                std::to_string(txn->id)
            );
        }
        recovery_manager.commit(txn->id);
        txn_manager.commit(*txn);
    }

    void abort(const TxnPtr& txn) {
        if (!txn || txn->state != TxnContext::RUNNING) {
            return;
        }
        recovery_manager.abort(txn->id);
        txn_manager.abort(*txn);
    }

    void crashAt(CrashPoint point) {
        recovery_manager.crashAt(point);
    }

    void checkpoint() {
        recovery_manager.checkpoint();
    }

    void requestTableAccess(const TxnPtr& txn,
                            AccessType type,
                            const TableMetadata& metadata) {
        acquireTxnLocks(
            txn,
            {{tableResource(metadata), tableLockModeForAccess(type)}}
        );
    }

    void runLoggedStatement(const TxnPtr& txn,
                            const std::function<void()>& statement_body) {
        requireRunningTransaction(txn);
        recovery_manager.setCurrentTransaction(txn->id);
        try {
            statement_body();
        } catch (...) {
            recovery_manager.clearCurrentTransaction();
            throw;
        }
        recovery_manager.clearCurrentTransaction();
    }

    void loadTuple(const std::string& tableName,
                   const std::vector<std::string>& values,
                   std::unordered_map<TableId, PageID>& append_pages) {
        auto& metadata = catalog.getTable(tableName);
        auto tuple = makeTuple(metadata.schema, values);
        TableHeap tableHeap(metadata, page_manager);

        auto cursor = append_pages.find(metadata.table_id);
        if (cursor == append_pages.end()) {
            cursor = append_pages.emplace(
                metadata.table_id,
                metadata.first_page
            ).first;
        }

        if (!tableHeap.appendTuple(std::move(tuple), cursor->second)) {
            throw std::runtime_error("Failed to import row into table: " + tableName);
        }
    }

    void buildIndexes(const std::vector<IndexSpec>& specs) {
        index_manager.buildIndexes(specs);
    }

    void clearBufferPool() {
        buffer_manager.clearBufferPool();
    }

private:
    static void requireRunningTransaction(const TxnPtr& txn) {
        if (!txn || txn->state != TxnContext::RUNNING) {
            throw std::runtime_error("Statement requires a running transaction.");
        }
    }

    static ConcurrencyControlResource tableResource(
            const TableMetadata& metadata) {
        return ConcurrencyControlResource::table(metadata.table_id);
    }

    void acquireTxnLocks(
            const TxnPtr& txn,
            const std::vector<LockRequest>& locks) {
        if (!txn || locks.empty()) {
            return;
        }

        for (const auto& lock : locks) {
            auto result =
                txn_manager.requestAccess(
                    txn,
                    {lock}
                );

            if (result.deadlock) {
                abort(txn);
                throw std::runtime_error(
                    "DEADLOCK: txn " + std::to_string(txn->id) +
                    " chosen as victim"
                );
            }

            if (!result.granted) {
                throw std::runtime_error(
                    "Concurrency-control policy rejected access for txn " +
                    std::to_string(txn->id)
                );
            }
        }
    }

    static std::unique_ptr<Tuple> makeTuple(
            const TableSchema& schema,
            const std::vector<std::string>& values) {
        if (values.size() != schema.columns.size()) {
            throw std::runtime_error("Wrong field count for table row.");
        }

        auto tuple = std::make_unique<Tuple>();
        for (size_t i = 0; i < schema.columns.size(); i++) {
            tuple->addField(FieldParser::parseValue(schema.columns[i].type, values[i]));
        }
        return tuple;
    }
};

class QueryExecutor {
private:
    TransactionalStorageManager& transactional_storage_manager;

public:
    explicit QueryExecutor(
            TransactionalStorageManager& transactional_storage_manager)
        : transactional_storage_manager(transactional_storage_manager) {}

    void execute(const ParsedStatement& statement,
                 const TxnPtr& txn) {
        requireRunningTransaction(txn);
        switch (statement.kind) {
            case ParsedStatement::Kind::Insert:
                executeInsert(statement, txn);
                return;
            case ParsedStatement::Kind::Update:
                executeUpdate(statement, txn);
                return;
            case ParsedStatement::Kind::Delete:
                executeDelete(statement, txn);
                return;
            case ParsedStatement::Kind::Select:
                executeSelect(statement, txn);
                return;
            case ParsedStatement::Kind::Explain:
                throw std::runtime_error("EXPLAIN is handled by the query processor.");
        }
        throw std::runtime_error("Unsupported parsed statement.");
    }

    void execute(const ParsedStatement& statement) {
        switch (statement.kind) {
            case ParsedStatement::Kind::Insert:
                insertRow(statement.tableName, statement.values);
                return;
            case ParsedStatement::Kind::Update:
                executeUpdateBody(statement);
                return;
            case ParsedStatement::Kind::Delete:
                executeDeleteBody(statement);
                return;
            case ParsedStatement::Kind::Select:
                executeSelectBody(statement);
                return;
            case ParsedStatement::Kind::Explain:
                throw std::runtime_error("EXPLAIN is handled by the query processor.");
        }
        throw std::runtime_error("Unsupported parsed statement.");
    }

    void insertRow(const std::string& tableName,
                   const std::vector<std::string>& values,
                   bool printResult = false) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            tableName
        );
        auto tuple = makeTuple(metadata.schema, values);
        TableHeap tableHeap(metadata, transactional_storage_manager.page_manager);
        InsertOperator insertOp(tableHeap);
        insertOp.setTupleToInsert(std::move(tuple));
        executeStatementOperator(insertOp, printResult);
    }

    QueryResult executeJoinPlan(
            const QueryComponents& components,
            const std::vector<PhysicalJoinKind>* physical_join_kinds = nullptr,
            size_t sample_limit = 5,
            const std::shared_ptr<PhysicalPlanNode>& plan_root = nullptr,
            LookaheadInfoPassingManager* lip_manager = nullptr) {
        return executeJoinQuery(
            components,
            transactional_storage_manager.catalog,
            transactional_storage_manager.page_manager,
            sample_limit,
            physical_join_kinds,
            plan_root,
            &transactional_storage_manager.index_manager,
            lip_manager
        );
    }

    QueryResult executeJoinPlan(
            const QueryComponents& components,
            const TxnPtr& txn,
            const std::vector<PhysicalJoinKind>* physical_join_kinds = nullptr,
            size_t sample_limit = 5,
            const std::shared_ptr<PhysicalPlanNode>& plan_root = nullptr,
            LookaheadInfoPassingManager* lip_manager = nullptr) {
        requestSelectAccess(components, txn);
        QueryResult result;
        transactional_storage_manager.runLoggedStatement(txn, [&]() {
            result = executeJoinPlan(
                components,
                physical_join_kinds,
                sample_limit,
                plan_root,
                lip_manager
            );
        });
        return result;
    }

    void printJoinPlanResult(const QueryComponents& components,
                             const QueryResult& result) {
        printJoinQueryResult(components, result);
    }

private:
    static void requireRunningTransaction(const TxnPtr& txn) {
        if (!txn || txn->state != TxnContext::RUNNING) {
            throw std::runtime_error("SQL execution requires a running transaction.");
        }
    }

    std::unique_ptr<Tuple> makeTuple(const TableSchema& schema,
                                     const std::vector<std::string>& values) {
        if (values.size() != schema.columns.size()) {
            throw std::runtime_error("Wrong field count for table row.");
        }
        auto tuple = std::make_unique<Tuple>();
        for (size_t i = 0; i < schema.columns.size(); i++) {
            tuple->addField(FieldParser::parseValue(
                schema.columns[i].type,
                values[i]
            ));
        }
        return tuple;
    }

    std::unique_ptr<IPredicate> makeEqualityPredicate(
            const TableSchema& schema,
            const std::string& columnName,
            const std::string& value) {
        auto column = static_cast<size_t>(schema.getColumnIndex(columnName));
        auto field = FieldParser::parseValue(schema.columns[column].type, value);
        return std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(column),
            SimplePredicate::Operand(std::move(field)),
            SimplePredicate::ComparisonOperator::EQ
        );
    }

    void executeInsert(const ParsedStatement& statement, const TxnPtr& txn) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.tableName
        );
        transactional_storage_manager.requestTableAccess(
            txn,
            AccessType::Write,
            metadata
        );
        transactional_storage_manager.runLoggedStatement(txn, [&]() {
            insertRow(statement.tableName, statement.values);
        });
    }

    void executeUpdate(const ParsedStatement& statement, const TxnPtr& txn) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.tableName
        );
        transactional_storage_manager.requestTableAccess(
            txn,
            AccessType::Write,
            metadata
        );
        transactional_storage_manager.runLoggedStatement(txn, [&]() {
            executeUpdateBody(statement);
        });
    }

    void executeDelete(const ParsedStatement& statement, const TxnPtr& txn) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.tableName
        );
        transactional_storage_manager.requestTableAccess(
            txn,
            AccessType::Write,
            metadata
        );
        transactional_storage_manager.runLoggedStatement(txn, [&]() {
            executeDeleteBody(statement);
        });
    }

    void executeSelect(const ParsedStatement& statement,
                       const TxnPtr& txn) {
        requestSelectAccess(statement.query, txn);
        transactional_storage_manager.runLoggedStatement(txn, [&]() {
            executeSelectBody(statement);
        });
    }

    void requestSelectAccess(const QueryComponents& components,
                             const TxnPtr& txn) {
        std::set<TableId> locked_tables;
        if (components.isJoinQuery()) {
            for (const auto& table_ref : components.table_refs) {
                if (!locked_tables.insert(table_ref.table_id).second) {
                    continue;
                }
                auto& metadata =
                    transactional_storage_manager.catalog.getTable(
                        table_ref.table_id
                    );
                transactional_storage_manager.requestTableAccess(
                    txn,
                    AccessType::Read,
                    metadata
                );
            }
            return;
        }

        auto& metadata = transactional_storage_manager.catalog.getTable(
            components.tableName
        );
        transactional_storage_manager.requestTableAccess(
            txn,
            AccessType::Read,
            metadata
        );
    }

    void executeUpdateBody(const ParsedStatement& statement) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.tableName
        );
        const size_t setColumn = static_cast<size_t>(
            metadata.schema.getColumnIndex(statement.setColumnName));
        auto predicate = makeEqualityPredicate(
            metadata.schema,
            statement.whereColumnName,
            statement.whereValue
        );
        auto field = FieldParser::parseValue(
            metadata.schema.columns[setColumn].type,
            statement.setValue
        );
        TableHeap tableHeap(metadata, transactional_storage_manager.page_manager);
        UpdateOperator updateOp(
            tableHeap,
            std::move(predicate),
            {{setColumn, *field}}
        );
        executeStatementOperator(updateOp, false);
    }

    void executeDeleteBody(const ParsedStatement& statement) {
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.tableName
        );
        auto predicate = makeEqualityPredicate(
            metadata.schema,
            statement.whereColumnName,
            statement.whereValue
        );
        TableHeap tableHeap(metadata, transactional_storage_manager.page_manager);
        DeleteOperator deleteOp(tableHeap, std::move(predicate));
        executeStatementOperator(deleteOp, false);
    }

    void executeSelectBody(const ParsedStatement& statement) {
        if (statement.query.isJoinQuery()) {
            auto result = executeJoinPlan(statement.query);
            printJoinPlanResult(statement.query, result);
            return;
        }
        auto& metadata = transactional_storage_manager.catalog.getTable(
            statement.query.tableName
        );
        executeQuery(
            statement.query,
            metadata,
            transactional_storage_manager.page_manager
        );
    }

    void executeStatementOperator(Operator& statementOperator, bool printResult) {
        statementOperator.open();
        while (statementOperator.next()) {
            if (printResult) {
                const auto& output = statementOperator.getOutput();
                for (const auto& field : output) {
                    field->print();
                    std::cout << " ";
                }
                std::cout << std::endl;
            }
        }
        statementOperator.close();
    }
};

class QueryProcessor {
private:
    TransactionalStorageManager& transactional_storage_manager;
    QueryParser query_parser;
    QueryOptimizer query_optimizer;
    QueryExecutor query_executor;

public:
    explicit QueryProcessor(
            TransactionalStorageManager& transactional_storage_manager)
        : transactional_storage_manager(transactional_storage_manager),
          query_parser(transactional_storage_manager.catalog),
          query_optimizer(transactional_storage_manager.catalog,
                          transactional_storage_manager.page_manager),
          query_executor(transactional_storage_manager) {}

    ParsedStatement parseStatement(const std::string& statement) {
        return query_parser.parseStatement(statement);
    }

    void execute(const ParsedStatement& statement,
                 const TxnPtr& txn) {
        if (statement.kind == ParsedStatement::Kind::Explain) {
            explain(statement.query);
            return;
        }
        if (statement.kind == ParsedStatement::Kind::Select &&
            statement.query.isJoinQuery()) {
            auto stats = query_optimizer.analyzeQueryTables(statement.query);
            auto search = query_optimizer.optimizeWithCascadesQueue(
                statement.query,
                stats,
                &transactional_storage_manager.index_manager
            );
            auto result = query_executor.executeJoinPlan(
                search.components,
                txn,
                nullptr,
                5,
                search.plan_root
            );
            query_executor.printJoinPlanResult(search.components, result);
            return;
        }
        query_executor.execute(statement, txn);
    }

    void execute(const std::string& statement,
                 const TxnPtr& txn) {
        execute(query_parser.parseStatement(statement), txn);
    }

    void execute(const ParsedStatement& statement) {
        if (statement.kind == ParsedStatement::Kind::Explain) {
            explain(statement.query);
            return;
        }
        if (statement.kind == ParsedStatement::Kind::Select &&
            statement.query.isJoinQuery()) {
            auto stats = query_optimizer.analyzeQueryTables(statement.query);
            auto search = query_optimizer.optimizeWithCascadesQueue(
                statement.query,
                stats,
                &transactional_storage_manager.index_manager
            );
            auto result = query_executor.executeJoinPlan(
                search.components,
                nullptr,
                5,
                search.plan_root
            );
            query_executor.printJoinPlanResult(search.components, result);
            return;
        }
        query_executor.execute(statement);
    }

    void execute(const std::string& statement) {
        execute(query_parser.parseStatement(statement));
    }

    QueryComponents parseSelectStatement(const std::string& statement) {
        auto parsed = query_parser.parseStatement(statement);
        if (parsed.kind != ParsedStatement::Kind::Select &&
            parsed.kind != ParsedStatement::Kind::Explain) {
            throw std::runtime_error("Expected SELECT or PROJECT statement.");
        }
        return std::move(parsed.query);
    }

    QueryResult executeJoinPlan(
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& plan_root,
            LookaheadInfoPassingManager* lip_manager = nullptr,
            size_t sample_limit = 5) {
        return query_executor.executeJoinPlan(
            components,
            static_cast<const std::vector<PhysicalJoinKind>*>(nullptr),
            sample_limit,
            plan_root,
            lip_manager
        );
    }

    QueryResult executeJoinOrder(
            const QueryComponents& components,
            const std::vector<PhysicalJoinKind>& physical_join_kinds,
            LookaheadInfoPassingManager* lip_manager = nullptr,
            size_t sample_limit = 5) {
        return query_executor.executeJoinPlan(
            components,
            &physical_join_kinds,
            sample_limit,
            nullptr,
            lip_manager
        );
    }

    MemoOptimizationResult optimizeWithCascadesQueue(
            const QueryComponents& components) {
        auto stats = query_optimizer.analyzeQueryTables(components);
        return query_optimizer.optimizeWithCascadesQueue(
            components,
            stats,
            &transactional_storage_manager.index_manager
        );
    }

    void printPhysicalPlanTree(
            const std::string& label,
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& root) {
        query_optimizer.printPhysicalPlanTree(label, components, root);
    }

    void buildLookaheadFilters(LookaheadInfoPassingManager& lip_manager,
                               const QueryComponents& components) {
        lip_manager.build(
            components,
            transactional_storage_manager.catalog,
            transactional_storage_manager.page_manager
        );
    }

private:
    void explain(const QueryComponents& components) {
        if (!components.isJoinQuery()) {
            throw std::runtime_error("EXPLAIN currently supports join queries.");
        }

        auto stats = query_optimizer.analyzeQueryTables(components);
        auto search = query_optimizer.optimizeWithCascadesQueue(
            components,
            stats,
            &transactional_storage_manager.index_manager
        );
        query_optimizer.printPhysicalPlanTree(
            "EXPLAIN physical plan",
            search.components,
            search.plan_root
        );
    }
};

class BuzzDB {
private:
    TransactionalStorageManager transactional_storage_manager;
    QueryProcessor query_processor;
    TxnPtr active_txn;

public:
    BuzzDB()
        : transactional_storage_manager(),
          query_processor(transactional_storage_manager) {}

    TxnPtr beginTransaction() {
        return transactional_storage_manager.beginTransaction();
    }

    void execute(const TxnPtr& txn, const std::string& statement) {
        requireRunningTransaction(txn);
        query_processor.execute(statement, txn);
    }

    void execute(const std::string& statement) {
        query_processor.execute(statement);
    }

    void commit(const TxnPtr& txn) {
        requireRunningTransaction(txn);
        transactional_storage_manager.commit(txn);
    }

    void abort(const TxnPtr& txn) {
        requireRunningTransaction(txn);
        transactional_storage_manager.abort(txn);
    }

    void begin() {
        if (active_txn) {
            throw std::runtime_error("BEGIN while another transaction is active.");
        }
        active_txn = beginTransaction();
    }

    void commit() {
        if (!active_txn) {
            throw std::runtime_error("COMMIT without BEGIN.");
        }
        commit(active_txn);
        active_txn.reset();
    }

    void abort() {
        if (!active_txn) {
            throw std::runtime_error("ABORT without BEGIN.");
        }
        abort(active_txn);
        active_txn.reset();
    }

    void crashAt(CrashPoint point) {
        transactional_storage_manager.crashAt(point);
    }

    void checkpoint() {
        transactional_storage_manager.checkpoint();
    }

    bool createTable(const std::string& name, TableSchema schema) {
        return transactional_storage_manager.catalog.createTable(
            name,
            std::move(schema)
        );
    }

    bool hasTable(const std::string& name) const {
        return transactional_storage_manager.catalog.hasTable(name);
    }

    bool isDatabaseEmpty() const {
        return transactional_storage_manager.catalog.empty();
    }

    void loadTuple(const std::string& tableName,
                   const std::vector<std::string>& values,
                   std::unordered_map<TableId, PageID>& append_pages) {
        transactional_storage_manager.loadTuple(
            tableName,
            values,
            append_pages
        );
    }

    void buildIndexes(
            const std::vector<IndexSpec>& specs) {
        transactional_storage_manager.buildIndexes(specs);
    }

    QueryComponents parseSelectStatement(const std::string& statement) {
        return query_processor.parseSelectStatement(statement);
    }

    void clearBufferPool() {
        transactional_storage_manager.clearBufferPool();
    }

    void buildLookaheadFilters(LookaheadInfoPassingManager& lip_manager,
                               const QueryComponents& components) {
        query_processor.buildLookaheadFilters(lip_manager, components);
    }

    MemoOptimizationResult optimizeWithCascadesQueue(
            const QueryComponents& components) {
        return query_processor.optimizeWithCascadesQueue(components);
    }

    void printPhysicalPlanTree(
            const std::string& label,
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& root) {
        query_processor.printPhysicalPlanTree(label, components, root);
    }

    QueryResult executeJoinPlan(
            const QueryComponents& components,
            const std::shared_ptr<PhysicalPlanNode>& plan_root,
            LookaheadInfoPassingManager* lip_manager = nullptr,
            size_t sample_limit = 5) {
        return query_processor.executeJoinPlan(
            components,
            plan_root,
            lip_manager,
            sample_limit
        );
    }

    QueryResult executeJoinOrder(
            const QueryComponents& components,
            const std::vector<PhysicalJoinKind>& physical_join_kinds,
            LookaheadInfoPassingManager* lip_manager = nullptr,
            size_t sample_limit = 5) {
        return query_processor.executeJoinOrder(
            components,
            physical_join_kinds,
            lip_manager,
            sample_limit
        );
    }

    void executeStatementsAndQueries(const std::vector<std::string>& statements,
                                     int crashAfterStatement = 0) {
        int statementsSeen = 0;
        for (const auto& statement : statements) {
            std::cout << statement << "\n";
            auto statementFinished = [&]() {
                checkStatementCrashLimit(statementsSeen, crashAfterStatement);
            };

            std::regex beginRegex("^\\s*BEGIN\\s*;?\\s*$",
                                  std::regex_constants::icase);
            if (std::regex_match(statement, beginRegex)) {
                begin();
                statementFinished();
                continue;
            }

            std::regex commitRegex("^\\s*COMMIT\\s*;?\\s*$",
                                   std::regex_constants::icase);
            if (std::regex_match(statement, commitRegex)) {
                commit();
                statementFinished();
                continue;
            }

            std::regex abortRegex("^\\s*ABORT\\s*;?\\s*$",
                                  std::regex_constants::icase);
            if (std::regex_match(statement, abortRegex)) {
                abort();
                statementFinished();
                continue;
            }

            std::regex checkpointRegex("^\\s*CHECKPOINT\\s*;?\\s*$",
                                       std::regex_constants::icase);
            if (std::regex_match(statement, checkpointRegex)) {
                checkpoint();
                statementFinished();
                continue;
            }

            if (active_txn) {
                execute(active_txn, statement);
            } else {
                query_processor.execute(statement);
            }
            statementFinished();
        }
    }

private:
    static void requireRunningTransaction(const TxnPtr& txn) {
        if (!txn || txn->state != TxnContext::RUNNING) {
            throw std::runtime_error("Statement requires a running transaction.");
        }
    }
};

class DatabaseImporter {
private:
    static FieldType parseFieldType(const std::string& typeName) {
        if (typeName == "INT") {
            return INT;
        }
        if (typeName == "FLOAT") {
            return FLOAT;
        }
        if (typeName == "STRING") {
            return STRING;
        }
        throw std::runtime_error("Unknown field type: " + typeName);
    }

    static TableSchema parseTableSchema(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            throw std::runtime_error("TABLE line must include at least one column.");
        }

        TableSchema schema;
        for (size_t i = 2; i < tokens.size(); i++) {
            auto separator = tokens[i].find(':');
            if (separator == std::string::npos) {
                throw std::runtime_error("Bad column declaration: " + tokens[i]);
            }

            auto column_name = tokens[i].substr(0, separator);
            auto type_name = tokens[i].substr(separator + 1);
            schema.columns.push_back({column_name, parseFieldType(type_name)});
        }
        return schema;
    }

public:
    static void importFile(BuzzDB& db, const std::string& filename) {
        if (!db.isDatabaseEmpty()) {
            return;
        }

        std::ifstream inputFile(filename);
        if (!inputFile) {
            throw std::runtime_error("Unable to open file: " + filename);
        }

        std::unordered_map<TableId, PageID> append_pages;
        std::string line;
        while (std::getline(inputFile, line)) {
            line = TextUtil::trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto tokens = TextUtil::split(line, '|');
            if (tokens.empty()) {
                continue;
            }

            if (tokens[0] == "TABLE") {
                if (tokens.size() < 3) {
                    throw std::runtime_error("Bad TABLE line: " + line);
                }
                const std::string tableName = tokens[1];
                if (!db.createTable(tableName, parseTableSchema(tokens))) {
                    throw std::runtime_error("Duplicate TABLE declaration: " + tableName);
                }
                continue;
            }

            const std::string tableName = tokens[0];
            if (!db.hasTable(tableName)) {
                throw std::runtime_error("Row appears before TABLE declaration: " + tableName);
            }

            std::vector<std::string> values(tokens.begin() + 1, tokens.end());
            db.loadTuple(tableName, values, append_pages);
        }
    }
};

const std::string imdb_data_filename = "imdb_large.txt";
const std::string imdb_join_query =
    "PROJECT {cn.id}, {cn.name}, {miidx.info}, {t.title} "
    "FROM info_type AS it "
    "JOIN movie_info_idx AS miidx ON {it.id} = {miidx.info_type_id} "
    "JOIN movie_companies AS mc ON {miidx.movie_id} = {mc.movie_id} "
    "JOIN company_name AS cn ON {mc.company_id} = {cn.id} "
    "JOIN movie_info AS mi ON {mc.movie_id} = {mi.movie_id} "
    "JOIN title AS t ON {mi.movie_id} = {t.id} "
    "JOIN kind_type AS kt ON {t.kind_id} = {kt.id} "
    "JOIN company_type AS ct ON {mc.company_type_id} = {ct.id} "
    "JOIN info_type AS it2 ON {mi.info_type_id} = {it2.id} "
    "WHERE {cn.country_code} = [us] "
    "AND {ct.kind} = production_companies "
    "AND {it.info} = rating "
    "AND {it2.info} = release_dates "
    "AND {kt.kind} = movie "
    "AND {mi.movie_id} = {miidx.movie_id} "
    "AND {mi.movie_id} = {mc.movie_id} "
    "AND {miidx.movie_id} = {mc.movie_id} "
    "ORDER BY {cn.id}";

void ensureImdbDatasetLoaded(BuzzDB& db) {
    if (db.isDatabaseEmpty()) {
        DatabaseImporter::importFile(db, imdb_data_filename);
    }
}

std::vector<IndexSpec> imdbIndexSpecs() {
    return {
        {"movie_companies", "company_type_id"},
        {"title", "kind_id"},
        {"movie_info_idx", "info_type_id"},
        {"movie_info", "info_type_id"},
        {"company_name", "country_code"},
        {"title", "production_year"},
        {"movie_companies", "note"}
    };
}

void printMemoRuleStats(const std::string& label,
                        const MemoRuleStats& stats) {
    std::cout << "  " << label
              << ": groups " << stats.initial_groups << " -> "
              << stats.final_groups
              << ", expressions " << stats.initial_expressions << " -> "
              << stats.final_expressions
              << ", logical_join_alternatives="
              << stats.logical_join_alternatives
              << ", scan_implementations=" << stats.scan_implementations
              << ", join_implementations=" << stats.join_implementations
              << ", sort_merge_implementations="
              << stats.sort_merge_implementations
              << ", tasks=" << stats.tasks_executed
              << " (OptimizeGroup=" << stats.optimize_group_tasks
              << ", ApplyRule=" << stats.apply_rule_tasks
              << ", OptimizeInput=" << stats.optimize_input_tasks << ")"
              << ", sort_enforcers_in_winner=" << stats.sort_enforcers
              << ", winner_candidates=" << stats.winner_candidates
              << std::endl;
}

struct TimedLipResult {
    QueryResult result;
    double elapsed_seconds = 0.0;
};

TimedLipResult executeTimedPlan(
        BuzzDB& db,
        const QueryComponents& components,
        const std::shared_ptr<PhysicalPlanNode>& plan_root,
        LookaheadInfoPassingManager* lip_manager = nullptr) {
    db.clearBufferPool();
    auto start = std::chrono::steady_clock::now();
    auto result = db.executeJoinPlan(
        components,
        plan_root,
        lip_manager,
        0
    );
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    return {std::move(result), elapsed.count()};
}

TimedLipResult executeTimedJoinOrder(
        BuzzDB& db,
        const QueryComponents& components,
        const std::vector<PhysicalJoinKind>& physical_join_kinds,
        LookaheadInfoPassingManager* lip_manager = nullptr) {
    db.clearBufferPool();
    auto start = std::chrono::steady_clock::now();
    auto result = db.executeJoinOrder(
        components,
        physical_join_kinds,
        lip_manager,
        0
    );
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    return {std::move(result), elapsed.count()};
}

std::string joinOrderLabel(const QueryComponents& components) {
    std::ostringstream output;
    output << tableRefForId(components, components.base_table_ref_id).alias;
    for (const auto& join : components.joins) {
        output << " -> "
               << tableRefForId(components, join.input_table_ref_id).alias;
    }
    return output.str();
}

void runImdbLookaheadInfoPassing() {
    BuzzDB db;
    ensureImdbDatasetLoaded(db);

    auto components = db.parseSelectStatement(imdb_join_query);
    std::vector<PhysicalJoinKind> written_join_kinds(
        components.joins.size(),
        PhysicalJoinKind::HashJoin
    );

    auto indexes = imdbIndexSpecs();
    db.buildIndexes(indexes);
    auto search = db.optimizeWithCascadesQueue(components);

    std::cout << "\nWritten-order plan:" << std::endl;
    std::cout << "  order=" << joinOrderLabel(components) << std::endl;
    std::cout << "  operators=HashJoin for each JOIN clause" << std::endl;

    std::cout << "\nCascades queue work:" << std::endl;
    printMemoRuleStats("with index implementations", search.rule_stats);

    std::cout << "\nCascades winner:" << std::endl;
    std::cout << "  cost=" << formatEstimate(search.estimated_cost)
              << ", final_est=" << formatEstimate(search.final_estimate)
              << ", index_joins=" << search.index_joins
              << ", sort_merge_joins=" << search.sort_merge_joins
              << ", sort_enforcers=" << search.rule_stats.sort_enforcers
              << std::endl;

    db.printPhysicalPlanTree(
        "Cascades winner",
        search.components,
        search.plan_root
    );

    auto written_without_lip = executeTimedJoinOrder(
        db,
        components,
        written_join_kinds
    );

    LookaheadInfoPassingManager written_lip_manager;
    auto written_filter_start = std::chrono::steady_clock::now();
    db.buildLookaheadFilters(written_lip_manager, components);
    auto written_filter_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> written_filter_elapsed =
        written_filter_end - written_filter_start;

    auto written_with_lip = executeTimedJoinOrder(
        db,
        components,
        written_join_kinds,
        &written_lip_manager
    );

    auto cascades_without_lip = executeTimedPlan(
        db,
        search.components,
        search.plan_root
    );

    LookaheadInfoPassingManager cascades_lip_manager;
    auto cascades_filter_start = std::chrono::steady_clock::now();
    db.buildLookaheadFilters(cascades_lip_manager, search.components);
    auto cascades_filter_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> cascades_filter_elapsed =
        cascades_filter_end - cascades_filter_start;

    auto cascades_with_lip = executeTimedPlan(
        db,
        search.components,
        search.plan_root,
        &cascades_lip_manager
    );

    std::cout << "\nExecuted plans:" << std::endl;
    std::cout << "  written order, without LIP: rows="
              << written_without_lip.result.row_count
              << ", elapsed="
              << formatEstimate(written_without_lip.elapsed_seconds)
              << " seconds" << std::endl;
    std::cout << "  written order, with LIP:    rows="
              << written_with_lip.result.row_count
              << ", filter_build="
              << formatEstimate(written_filter_elapsed.count())
              << " seconds, elapsed="
              << formatEstimate(written_with_lip.elapsed_seconds)
              << " seconds, early_rejections="
              << written_lip_manager.rejectedTuples() << std::endl;
    std::cout << "  Cascades, without LIP:      rows="
              << cascades_without_lip.result.row_count
              << ", elapsed="
              << formatEstimate(cascades_without_lip.elapsed_seconds)
              << " seconds" << std::endl;
    std::cout << "  Cascades, with LIP:         rows="
              << cascades_with_lip.result.row_count
              << ", filter_build="
              << formatEstimate(cascades_filter_elapsed.count())
              << " seconds, elapsed="
              << formatEstimate(cascades_with_lip.elapsed_seconds)
              << " seconds, early_rejections="
              << cascades_lip_manager.rejectedTuples() << std::endl;

    std::cout << "\nTakeaway: Lookahead Information Passing adds runtime"
              << " Bloom filters to an existing physical plan. It helps most"
              << " when the chosen order would otherwise scan many tuples"
              << " before reaching selective joins. An indexed Cascades plan"
              << " may already avoid much of that work.\n";
}

int main() {
    try {

    std::cout << "IMDB Lookahead Information Passing\n";

    runImdbLookaheadInfoPassing();

    return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
