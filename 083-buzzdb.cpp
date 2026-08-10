#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>
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
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <functional>
#include <utility>
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

    int getColumnIndex(const std::string& name) const {
        for (size_t i = 0; i < columns.size(); i++) {
            if (columns[i].name == name) {
                return static_cast<int>(i);
            }
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

    bool isJoinQuery() const {
        return !joins.empty();
    }
};

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
                        stripOptionalBrackets(matches[3])
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

struct ParsedStatement {
    enum class Kind { Insert, Update, Delete, Select };
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

    QueryComponents parseQuery(const std::string& query) {
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

public:
    explicit QueryParser(Catalog& catalog) : catalog(catalog) {}

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

        std::regex selectRegex("^\\s*(?:SELECT|PROJECT)\\s+(.*)\\s*;?\\s*$",
                               std::regex_constants::icase);
        if (std::regex_match(statement, matches, selectRegex)) {
            ParsedStatement parsed;
            parsed.kind = ParsedStatement::Kind::Select;
            parsed.query = parseQuery(matches[1]);
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

QueryResult executeJoinQuery(const QueryComponents& components,
                             Catalog& catalog,
                             PageManager& page_manager,
                             size_t sample_limit = 5) {
    if (components.base_table_ref_id == INVALID_TABLE_REF_ID) {
        throw std::runtime_error("Join query is missing a base table.");
    }

    std::map<TableRefId, size_t> table_offsets;
    std::map<TableRefId, size_t> table_widths;
    std::vector<std::unique_ptr<TableHeap>> heaps;
    std::vector<std::unique_ptr<ScanOperator>> scans;
    std::vector<std::unique_ptr<NestedLoopJoinOperator>> joinOpBuffers;

    auto addScan = [&](TableRefId table_ref_id) -> ScanOperator& {
        const auto& table_ref = tableRefForId(components, table_ref_id);
        auto& metadata = catalog.getTable(table_ref.table_id);
        table_widths[table_ref_id] = metadata.schema.columns.size();
        heaps.push_back(std::make_unique<TableHeap>(metadata, page_manager));
        scans.push_back(std::make_unique<ScanOperator>(*heaps.back()));
        return *scans.back();
    };

    Operator* rootOp = &addScan(components.base_table_ref_id);
    table_offsets[components.base_table_ref_id] = 0;
    size_t output_width = table_widths[components.base_table_ref_id];

    for (const auto& join : components.joins) {
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

        joinOpBuffers.push_back(std::make_unique<NestedLoopJoinOperator>(
            *rootOp,
            right_scan,
            left_attr_index,
            right_attr_index
        ));
        rootOp = joinOpBuffers.back().get();
        table_offsets[join.input_table_ref_id] = output_width;
        output_width += table_widths[join.input_table_ref_id];
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
    std::cout << "Join operator: NestedLoopJoin" << std::endl;
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
    TransactionManager txn_manager;

    TransactionalStorageManager()
        : log_manager(),
          buffer_manager(log_manager),
          recovery_manager(buffer_manager, log_manager),
          page_manager(buffer_manager, recovery_manager),
          catalog(buffer_manager, page_manager),
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
            auto result = txn_manager.requestAccess(txn, {lock});

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
};

class QueryExecutor {
private:
    TransactionalStorageManager& transactional_storage_manager;

public:
    explicit QueryExecutor(
            TransactionalStorageManager& transactional_storage_manager)
        : transactional_storage_manager(transactional_storage_manager) {}

    void execute(const ParsedStatement& statement, const TxnPtr& txn) {
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

    void executeSelect(const ParsedStatement& statement, const TxnPtr& txn) {
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
            auto result = executeJoinQuery(
                statement.query,
                transactional_storage_manager.catalog,
                transactional_storage_manager.page_manager
            );
            printJoinQueryResult(statement.query, result);
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
    QueryParser parser;
    QueryExecutor executor;

public:
    explicit QueryProcessor(
            TransactionalStorageManager& transactional_storage_manager)
        : parser(transactional_storage_manager.catalog),
          executor(transactional_storage_manager) {}

    ParsedStatement parseStatement(const std::string& statement) {
        return parser.parseStatement(statement);
    }

    void execute(const ParsedStatement& statement, const TxnPtr& txn) {
        executor.execute(statement, txn);
    }

    void execute(const std::string& statement, const TxnPtr& txn) {
        execute(parser.parseStatement(statement), txn);
    }

    void execute(const ParsedStatement& statement) {
        executor.execute(statement);
    }

    void execute(const std::string& statement) {
        execute(parser.parseStatement(statement));
    }

    void insertRow(const std::string& tableName,
                   const std::vector<std::string>& values,
                   bool printResult = false) {
        executor.insertRow(tableName, values, printResult);
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

    Catalog& catalog() {
        return transactional_storage_manager.catalog;
    }

    const Catalog& catalog() const {
        return transactional_storage_manager.catalog;
    }

    PageManager& pageManager() {
        return transactional_storage_manager.page_manager;
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

    void insertRow(const std::string& tableName,
                   const std::vector<std::string>& values,
                   bool printResult = false) {
        query_processor.insertRow(tableName, values, printResult);
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

    static void checkStatementCrashLimit(int& statementsSeen,
                                         int crashAfterStatement) {
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
};

struct ImportResult {
    bool loaded = false;
    bool skipped = false;
    size_t tables_created = 0;
    size_t rows_inserted = 0;
    std::string reason;
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

    static void appendRow(BuzzDB& db,
                          const std::string& tableName,
                          const std::vector<std::string>& values,
                          std::unordered_map<std::string, PageID>& append_pages) {
        auto& metadata = db.catalog().getTable(tableName);
        auto tuple = makeTuple(metadata.schema, values);
        TableHeap tableHeap(metadata, db.pageManager());

        auto cursor = append_pages.find(tableName);
        if (cursor == append_pages.end()) {
            cursor = append_pages.emplace(tableName, metadata.first_page).first;
        }

        if (!tableHeap.appendTuple(std::move(tuple), cursor->second)) {
            throw std::runtime_error("Failed to import row into table: " + tableName);
        }
    }

public:
    static ImportResult importFile(BuzzDB& db, const std::string& filename) {
        ImportResult result;
        if (!db.isDatabaseEmpty()) {
            result.skipped = true;
            result.reason = "database already has user tables";
            return result;
        }

        std::ifstream inputFile(filename);
        if (!inputFile) {
            throw std::runtime_error("Unable to open file: " + filename);
        }

        std::unordered_map<std::string, PageID> append_pages;
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
                result.tables_created++;
                continue;
            }

            const std::string tableName = tokens[0];
            if (!db.hasTable(tableName)) {
                throw std::runtime_error("Row appears before TABLE declaration: " + tableName);
            }

            std::vector<std::string> values(tokens.begin() + 1, tokens.end());
            appendRow(db, tableName, values, append_pages);
            result.rows_inserted++;
        }

        result.loaded = true;
        return result;
    }
};

const std::string imdb_data_filename = "imdb_large.txt";
const std::string imdb_nested_loop_join_query =
    "SELECT {t.title}, {mii.info}, {it.info} "
    "FROM title AS t "
    "JOIN movie_info_idx AS mii ON {t.id} = {mii.movie_id} "
    "JOIN info_type AS it ON {mii.info_type_id} = {it.id} "
    "WHERE {t.id} = 2008135 AND {it.info} = rating";

ImportResult ensureImdbDatasetLoaded(BuzzDB& db) {
    if (db.isDatabaseEmpty()) {
        return DatabaseImporter::importFile(db, imdb_data_filename);
    }

    ImportResult result;
    result.skipped = true;
    result.reason = "database already has user tables";
    return result;
}

void printImportResult(const ImportResult& result) {
    if (result.loaded) {
        std::cout << "Loaded " << imdb_data_filename << ": "
                  << result.tables_created << " table(s), "
                  << result.rows_inserted << " row(s)." << std::endl;
        return;
    }

    if (result.skipped) {
        std::cout << "Skipped " << imdb_data_filename << ": "
                  << result.reason << "." << std::endl;
        return;
    }

    throw std::runtime_error("Importer finished without loading or skipping.");
}

void runImdbNestedLoopJoin() {
    std::cout << "\nIMDB Query: title joined with rating metadata" << std::endl;

    BuzzDB db;
    auto import_start = std::chrono::high_resolution_clock::now();
    auto import_result = ensureImdbDatasetLoaded(db);
    auto import_end = std::chrono::high_resolution_clock::now();
    printImportResult(import_result);
    if (import_result.loaded) {
        std::chrono::duration<double> import_elapsed =
            import_end - import_start;
        std::cout << "Elapsed database importing time: "
                  << import_elapsed.count() << " seconds" << std::endl;
    }

    auto join_reader = db.beginTransaction();
    auto query_start = std::chrono::high_resolution_clock::now();
    db.execute(join_reader, imdb_nested_loop_join_query);
    auto query_end = std::chrono::high_resolution_clock::now();
    db.commit(join_reader);
    std::chrono::duration<double> query_elapsed = query_end - query_start;
    std::cout << "Elapsed query processing time: "
              << query_elapsed.count() << " seconds" << std::endl;

    std::cout << "\nResult: Starts from a persistent IMDB database; "
              << "reruns reuse the loaded tables.\n";
}

int main() {
    try {

    std::cout << "IMDB nested-loop join\n";

    runImdbNestedLoopJoin();

    return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
