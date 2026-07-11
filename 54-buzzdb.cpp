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
#include <stdexcept>
#include <cassert>
#include <cctype>
#include <cstring>
#include <set>

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

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

using PageID = uint16_t;
using TableId = uint16_t;

constexpr PageID CATALOG_PAGE_ID = 0;
constexpr PageID INVALID_PAGE_ID = std::numeric_limits<PageID>::max();
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId SYS_TABLES_ID = 1;
constexpr TableId SYS_COLUMNS_ID = 2;
constexpr TableId FIRST_USER_TABLE_ID = 100;
const std::string BOOTSTRAP_MAGIC = "BUZZDB_BOOTSTRAP";

struct PageHeader {
    TableId table_id = INVALID_TABLE_ID;
    PageID next_page = INVALID_PAGE_ID;
};

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS + sizeof(PageHeader);

    SlottedPage(){
        reset();
    }

    void reset() {
        std::memset(page_data.get(), 0, PAGE_SIZE);

        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }

        auto* header = getHeader();
        header->table_id = INVALID_TABLE_ID;
        header->next_page = INVALID_PAGE_ID;
    }

    PageHeader* getHeader() {
        return reinterpret_cast<PageHeader*>(
            page_data.get() + sizeof(Slot) * MAX_SLOTS
        );
    }

    TableId getTableId() {
        return getHeader()->table_id;
    }

    void setTableId(TableId table_id) {
        getHeader()->table_id = table_id;
    }

    PageID getNextPage() {
        return getHeader()->next_page;
    }

    void setNextPage(PageID page_id) {
        getHeader()->next_page = page_id;
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {
        return addTupleAndReturnSlot(std::move(tuple)).has_value();
    }

    std::optional<size_t> addTupleAndReturnSlot(std::unique_ptr<Tuple> tuple) {
        auto serializedTuple = tuple->serialize();
        auto slot = findAvailableSlot(serializedTuple.size());
        if (!slot || !putSerializedTupleAtSlot(*slot, serializedTuple)) {
            return std::nullopt;
        }
        return slot;
    }

    bool putTupleAtSlot(size_t index, std::unique_ptr<Tuple> tuple) {
        return putSerializedTupleAtSlot(index, tuple->serialize());
    }

    std::unique_ptr<Tuple> getTuple(size_t index) const {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        if (index >= MAX_SLOTS || slot_array[index].empty) {
            return nullptr;
        }

        assert(slot_array[index].offset != INVALID_VALUE);
        const char* tuple_data = page_data.get() + slot_array[index].offset;
        std::istringstream iss(std::string(tuple_data, slot_array[index].length));
        return Tuple::deserialize(iss);
    }

    bool updateTuple(size_t index, std::unique_ptr<Tuple> tuple) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        if (index >= MAX_SLOTS || slot_array[index].empty) {
            return false;
        }

        auto serializedTuple = tuple->serialize();
        if (serializedTuple.size() > slot_array[index].length) {
            return false;
        }

        std::memcpy(page_data.get() + slot_array[index].offset,
                    serializedTuple.c_str(),
                    serializedTuple.size());
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
    }

private:
    std::optional<size_t> findAvailableSlot(size_t tuple_size) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == true &&
                slot_array[slot_itr].length >= tuple_size) {
                return slot_itr;
            }
        }
        return std::nullopt;
    }

    bool putSerializedTupleAtSlot(size_t slot_itr,
                                  const std::string& serializedTuple) {
        if (slot_itr >= MAX_SLOTS) {
            return false;
        }

        size_t tuple_size = serializedTuple.size();
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        auto& slot = slot_array[slot_itr];
        if (slot.length != INVALID_VALUE && tuple_size > slot.length) {
            return false;
        }

        size_t offset = slot.offset;
        if (offset == INVALID_VALUE) {
            if (slot_itr != 0) {
                auto previous_offset = slot_array[slot_itr - 1].offset;
                auto previous_length = slot_array[slot_itr - 1].length;
                if (previous_offset == INVALID_VALUE ||
                    previous_length == INVALID_VALUE) {
                    return false;
                }
                offset = previous_offset + previous_length;
            } else {
                offset = metadata_size;
            }
        }

        if (offset + tuple_size >= PAGE_SIZE) {
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        slot.empty = false;
        slot.offset = offset;
        if (slot.length == INVALID_VALUE) {
            slot.length = tuple_size;
        }

        std::memcpy(page_data.get() + offset,
                    serializedTuple.c_str(),
                    tuple_size);

        return true;
    }
};

const std::string database_filename = "buzzdb.dat";
const std::string log_filename = "buzzdb.log";

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

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;
    std::set<PageID> pinned_pages;

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
            evictUnpinnedPage();
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        //std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(page);
        return pageMap[page_id];
    }

    void flushPage(int page_id) {
        storage_manager.flush(page_id, pageMap[page_id]);
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

            storage_manager.flush(evictedPageId, pageMap[evictedPageId]);
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
    COMMIT,
    ABORT
};

bool isRedoRecord(LogRecordType type);

struct LogRecord {
    LogRecordType type;
    int txn_id = 0;
    TableId table_id = INVALID_TABLE_ID;
    PageID page_id = INVALID_PAGE_ID;
    size_t slot_id = 0;
    std::unique_ptr<Tuple> before_tuple;
    std::unique_ptr<Tuple> after_tuple;

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

struct RecoveryAnalysis {
    std::map<int, bool> committed_txns;
    std::map<int, bool> aborted_txns;
    int next_txn_id = 1;
};

bool isRedoRecord(LogRecordType type) {
    return type == LogRecordType::UPDATE ||
           type == LogRecordType::INSERT ||
           type == LogRecordType::DELETE;
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
        case LogRecordType::COMMIT:
            return "COMMIT";
        case LogRecordType::ABORT:
            return "ABORT";
    }
    throw std::runtime_error("Unknown log record.");
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
    if (type == "COMMIT") {
        record_type = LogRecordType::COMMIT;
        return true;
    }
    if (type == "ABORT") {
        record_type = LogRecordType::ABORT;
        return true;
    }
    return false;
}

class LogManager {
private:
    size_t records_written = 0;
    size_t bytes_written = 0;

public:
    void reset() {
        std::ofstream output(log_filename, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to reset recovery log.");
        }
        records_written = 0;
        bytes_written = 0;
    }

    void append(const LogRecord& record) {
        auto text = serialize(record);
        std::ofstream output(log_filename, std::ios::app);
        if (!output) {
            throw std::runtime_error("Unable to append to recovery log.");
        }
        output << text << "\n";
        output.flush();
        records_written++;
        bytes_written += text.size() + 1;
    }

    std::vector<LogRecord> readAll() const {
        std::ifstream input(log_filename);
        std::vector<LogRecord> records;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty()) {
                records.push_back(parse(line));
            }
        }
        return records;
    }

private:
    LogRecord parse(const std::string& line) const {
        std::istringstream input(line);
        std::string type;
        int txn_id = 0;
        input >> type >> txn_id;
        if (!input) {
            throw std::runtime_error("Bad log record: " + line);
        }

        LogRecordType record_type = LogRecordType::BEGIN;
        if (!parseLogRecordType(type, record_type)) {
            throw std::runtime_error("Unknown log record: " + line);
        }

        LogRecord record{record_type, txn_id};
        if (isRedoRecord(record_type)) {
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
            if (record_type != LogRecordType::DELETE) {
                record.after_tuple = Tuple::deserialize(input);
            }
        }
        return record;
    }

    std::string serialize(const LogRecord& record) const {
        std::ostringstream output;
        output << logRecordName(record.type) << " "
               << record.txn_id;
        if (isRedoRecord(record.type)) {
            output << " " << record.table_id
                   << " " << record.page_id
                   << " " << record.slot_id;
            if (record.type != LogRecordType::DELETE) {
                if (!record.after_tuple) {
                    throw std::runtime_error("Log record is missing after image.");
                }
                output << " " << record.after_tuple->serialize();
            }
        }
        return output.str();
    }
};

class RecoveryManager {
private:
    BufferManager& buffer_manager;
    bool txn_active = false;
    bool crash_after_commit_log = false;
    int next_txn_id = 1;
    int current_txn_id = 0;
    LogManager log_manager;
    std::vector<LogRecord> staged_records;
    std::vector<PageID> dirty_pages;

public:
    RecoveryManager(BufferManager& buffer_manager)
        : buffer_manager(buffer_manager) {}

    bool isActive() const {
        return txn_active;
    }

    void recover() {
        if (!logExists()) {
            if (databaseHasExistingPages()) {
                throwInconsistentDatabase("missing WAL file");
            }
            initializeEmpty();
            return;
        }

        auto records = log_manager.readAll();
        auto analysis = analysisPass(records);
        next_txn_id = std::max(next_txn_id, analysis.next_txn_id);
        std::cout << "Recovery analysis: "
                  << analysis.committed_txns.size()
                  << " committed transaction(s), "
                  << records.size()
                  << " log record(s), "
                  << countRedoRecords(records)
                  << " redo record(s)." << std::endl;
        redoPass(records, analysis);
    }

    void resetLog() {
        log_manager.reset();
    }

    PageID allocatePhysicalPage(TableId table_id) {
        return buffer_manager.appendPage(table_id);
    }

    int begin() {
        if (txn_active) {
            throw std::runtime_error("Transaction already active.");
        }
        txn_active = true;
        current_txn_id = next_txn_id++;
        staged_records.clear();
        dirty_pages.clear();
        log_manager.append(LogRecord{LogRecordType::BEGIN, current_txn_id});
        std::cout << "\nWAL txn " << current_txn_id << " BEGIN" << std::endl;
        return current_txn_id;
    }

    void commit() {
        if (!txn_active) {
            throw std::runtime_error("COMMIT without BEGIN.");
        }

        for (const auto& record : staged_records) {
            log_manager.append(record);
        }
        log_manager.append(LogRecord{LogRecordType::COMMIT, current_txn_id});

        if (!staged_records.empty()) {
            std::cout << "WAL: forced "
                      << staged_records.size()
                      << " redo record(s) and COMMIT; data pages stay no-force."
                      << std::endl;

            if (crash_after_commit_log) {
                throw std::runtime_error(
                    "Simulated crash after COMMIT log, before data page flush"
                );
            }
        } else {
            std::cout << "WAL: forced COMMIT for read-only transaction."
                      << std::endl;
        }

        unpinDirtyPages();
        staged_records.clear();
        dirty_pages.clear();
        current_txn_id = 0;
        crash_after_commit_log = false;
        txn_active = false;
    }

    void abort() {
        if (!txn_active) {
            throw std::runtime_error("ABORT without BEGIN.");
        }

        for (auto it = staged_records.rbegin(); it != staged_records.rend(); ++it) {
            auto& page = buffer_manager.getPage(it->page_id);
            if (it->type == LogRecordType::INSERT) {
                page->deleteTuple(it->slot_id);
            } else {
                if (!it->before_tuple ||
                    !page->putTupleAtSlot(it->slot_id, it->before_tuple->clone())) {
                    throw std::runtime_error("Unable to restore tuple during ABORT.");
                }
            }
        }

        log_manager.append(LogRecord{LogRecordType::ABORT, current_txn_id});
        unpinDirtyPages();
        std::cout << "WAL: ABORT restored "
                  << staged_records.size()
                  << " in-memory operation(s), wrote ABORT; no restart undo needed."
                  << std::endl;
        staged_records.clear();
        dirty_pages.clear();
        current_txn_id = 0;
        crash_after_commit_log = false;
        txn_active = false;
    }

    void stageUpdate(TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple,
                     std::unique_ptr<Tuple> after_tuple) {
        if (!txn_active) {
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
        if (!txn_active) {
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
        if (!txn_active) {
            return;
        }

        stageRecord(LogRecordType::DELETE,
                    table_id,
                    page_id,
                    slot_id,
                    std::move(before_tuple),
                    nullptr);
    }

    void simulateCrashAfterCommitLog() {
        crash_after_commit_log = true;
    }

private:
    void initializeEmpty() {
        log_manager.reset();
        std::cout << "Recovery: initialized empty WAL file "
                  << log_filename << "." << std::endl;
    }

    bool databaseHasExistingPages() const {
        return buffer_manager.getNumPages() > 1;
    }

    bool logExists() const {
        std::ifstream input(log_filename);
        return static_cast<bool>(input);
    }

    [[noreturn]] void throwInconsistentDatabase(const std::string& reason) const {
        throw std::runtime_error(
            "Inconsistent BuzzDB files: " + reason + ". " +
            "buzzdb.dat and buzzdb.log must be kept together. "
            "Remove all of them to start a fresh database."
        );
    }

    bool isDirtyPage(PageID page_id) const {
        for (PageID dirty_page_id : dirty_pages) {
            if (dirty_page_id == page_id) {
                return true;
            }
        }
        return false;
    }

    void unpinDirtyPages() {
        for (PageID page_id : dirty_pages) {
            buffer_manager.unpinPage(page_id);
        }
    }

    void stageRecord(LogRecordType type,
                     TableId table_id,
                     PageID page_id,
                     size_t slot_id,
                     std::unique_ptr<Tuple> before_tuple,
                     std::unique_ptr<Tuple> after_tuple) {
        if (!isDirtyPage(page_id)) {
            buffer_manager.pinPage(page_id);
            dirty_pages.push_back(page_id);
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
        staged_records.push_back(std::move(record));

        std::cout << "WAL: staged " << logRecordName(type)
                  << " for table " << table_id
                  << ", page " << page_id
                  << ", slot " << slot_id
                  << "; page is pinned for no-steal." << std::endl;
    }

    RecoveryAnalysis analysisPass(const std::vector<LogRecord>& records) const {
        RecoveryAnalysis analysis;
        for (const auto& record : records) {
            analysis.next_txn_id = std::max(analysis.next_txn_id,
                                            record.txn_id + 1);
            if (record.type == LogRecordType::COMMIT) {
                analysis.committed_txns[record.txn_id] = true;
            } else if (record.type == LogRecordType::ABORT) {
                analysis.aborted_txns[record.txn_id] = true;
            }
        }
        return analysis;
    }

    size_t countRedoRecords(const std::vector<LogRecord>& records) const {
        size_t count = 0;
        for (const auto& record : records) {
            if (isRedoRecord(record.type)) {
                count++;
            }
        }
        return count;
    }

    void redoPass(const std::vector<LogRecord>& records,
                  const RecoveryAnalysis& analysis) {
        size_t applied = 0;
        for (const auto& record : records) {
            if (!isRedoRecord(record.type)) {
                continue;
            }
            if (analysis.committed_txns.find(record.txn_id) ==
                analysis.committed_txns.end()) {
                continue;
            }
            applyRedo(record.table_id,
                      record.page_id,
                      record.slot_id,
                      record.type,
                      record.after_tuple ? record.after_tuple->clone() : nullptr);
            applied++;
        }

        if (applied != 0) {
            std::cout << "Restart recovery: redid " << applied
                      << " committed log record(s)." << std::endl;
        }
    }

    void applyRedo(TableId table_id,
                   PageID page_id,
                   size_t slot_id,
                   LogRecordType type,
                   std::unique_ptr<Tuple> after_tuple) {
        auto& page = buffer_manager.getPage(page_id);
        if (page->getTableId() != table_id) {
            throw std::runtime_error("WAL redo page ownership mismatch.");
        }
        if (type == LogRecordType::DELETE) {
            page->deleteTuple(slot_id);
        } else if (!after_tuple ||
                   !page->putTupleAtSlot(slot_id, std::move(after_tuple))) {
            throw std::runtime_error("Unable to redo WAL operation.");
        }
        buffer_manager.flushPage(page_id);
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

        std::cout << "PageManager scan " << table_name
                  << ": page " << page_id << " -> ";
        if (next_page_id == INVALID_PAGE_ID) {
            std::cout << "END";
        } else {
            std::cout << "page " << next_page_id;
        }
        std::cout << std::endl;

        return next_page_id;
    }

    void flushWritePage(TableId, PageID page_id) {
        if (recovery_manager.isActive()) {
            std::cout << "WAL: keeping page " << page_id
                      << " dirty in memory until commit." << std::endl;
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

        std::cout << "Allocated page " << page_id
                  << " for table " << table_id << "." << std::endl;
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

std::string trim(const std::string& input);
std::vector<std::string> split(const std::string& input, char delimiter);

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
                "INSERT requiring a new page inside a WAL transaction is not supported in v54."
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
            auto tokens = split(line, '|');
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
        std::cout << "Scan Operator tuple_count: " << tuple_count << "\n";
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
};

QueryComponents parseQuery(const std::string& query, Catalog& catalog) {
    QueryComponents components;

    std::regex selectAllRegex(
        "^\\s*\\{\\*\\}\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*$",
        std::regex_constants::icase);
    std::smatch selectAllMatches;
    if (std::regex_match(query, selectAllMatches, selectAllRegex)) {
        const std::string tableName = selectAllMatches[1];
        const auto& metadata = catalog.getTable(tableName);

        components.tableName = tableName;
        for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
            components.selectAttributes.push_back(static_cast<int>(i));
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

void executeQuery(const QueryComponents& components, 
                  TableMetadata& metadata,
                  PageManager& page_manager) {
    TableHeap tableHeap(metadata, page_manager);
    ScanOperator scanOp(tableHeap);

    Operator* rootOp = &scanOp;

    std::optional<SelectOperator> selectOpBuffer;
    std::optional<HashAggregationOperator> hashAggOpBuffer;

    if (components.whereAttributeIndex != -1) {
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

std::string trim(const std::string& input) {
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

std::vector<std::string> split(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}


void maybeCrashAfterStatement(int& statementsSeen, int crashAfterStatement) {
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

class TransactionManager {
public:
    TxnPtr begin(int txn_id) {
        std::cout << "BEGIN txn " << txn_id << std::endl;
        return std::make_shared<TxnContext>(TxnContext{txn_id});
    }

    void commit(TxnContext& txn) {
        txn.state = TxnContext::COMMITTED;
        std::cout << "COMMIT txn " << txn.id << std::endl;
    }

    void abort(TxnContext& txn) {
        txn.state = TxnContext::ABORTED;
        std::cout << "ABORT txn " << txn.id << std::endl;
    }
};

class BuzzDB {
public:
    BufferManager buffer_manager;
    RecoveryManager recovery_manager;
    PageManager page_manager;
    Catalog catalog;
    TransactionManager txn_manager;
    TxnPtr active_txn;

public:
    BuzzDB() : recovery_manager(buffer_manager),
               page_manager(buffer_manager, recovery_manager),
               catalog(buffer_manager, page_manager) {
        recovery_manager.recover();
        catalog.load();
    }

    void begin() {
        if (active_txn) {
            throw std::runtime_error("BEGIN while another transaction is active.");
        }
        int txn_id = recovery_manager.begin();
        active_txn = txn_manager.begin(txn_id);
    }

    void commit() {
        if (!active_txn) {
            throw std::runtime_error("COMMIT without BEGIN.");
        }
        recovery_manager.commit();
        txn_manager.commit(*active_txn);
        active_txn.reset();
    }

    void abort() {
        if (!active_txn) {
            throw std::runtime_error("ABORT without BEGIN.");
        }
        recovery_manager.abort();
        txn_manager.abort(*active_txn);
        active_txn.reset();
    }

    void simulateCrashAfterCommitLog() {
        recovery_manager.simulateCrashAfterCommitLog();
    }

    bool createTable(const std::string& name, TableSchema schema) {
        return catalog.createTable(name, std::move(schema));
    }

    bool isDatabaseEmpty() const {
        return catalog.empty();
    }

    static std::unique_ptr<Field> parseFieldValue(FieldType type, const std::string& value) {
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

    static std::unique_ptr<Tuple> makeTuple(const TableSchema& schema,
                                            const std::vector<std::string>& values) {
        if (values.size() != schema.columns.size()) {
            throw std::runtime_error("Wrong field count for table row.");
        }

        auto tuple = std::make_unique<Tuple>();
        for (size_t i = 0; i < schema.columns.size(); i++) {
            tuple->addField(parseFieldValue(schema.columns[i].type, values[i]));
        }
        return tuple;
    }

    static std::unique_ptr<IPredicate> makeEqualityPredicate(const TableSchema& schema,
                                                            const std::string& columnName,
                                                            const std::string& value) {
        auto column = static_cast<size_t>(schema.getColumnIndex(columnName));
        auto field = parseFieldValue(schema.columns[column].type, value);
        return std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(column),
            SimplePredicate::Operand(std::move(field)),
            SimplePredicate::ComparisonOperator::EQ
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

    void insertRow(const std::string& tableName,
                   const std::vector<std::string>& values,
                   bool printResult = false) {
        auto& metadata = catalog.getTable(tableName);
        auto tuple = makeTuple(metadata.schema, values);
        TableHeap tableHeap(metadata, page_manager);
        InsertOperator insertOp(tableHeap);
        insertOp.setTupleToInsert(std::move(tuple));
        executeStatementOperator(insertOp, printResult);
    }

    void executeStatementsAndQueries(const std::vector<std::string>& statements,
                                     bool printResult = true,
                                     int crashAfterStatement = 0) {
        int statementsSeen = 0;
        for (const auto& statement : statements) {
            std::cout << statement << "\n";
            std::smatch matches;
            auto statementFinished = [&]() {
                maybeCrashAfterStatement(statementsSeen, crashAfterStatement);
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

            std::regex insertRegex("^\\s*INSERT\\s+INTO\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+VALUES\\s*\\((.*)\\)\\s*;?\\s*$",
                                   std::regex_constants::icase);
            if (std::regex_match(statement, matches, insertRegex)) {
                const std::string tableName = matches[1];
                const std::string valuesText = matches[2];
                insertRow(tableName, split(valuesText, ','), printResult);
                statementFinished();
                continue;
            }

            std::regex updateRegex("^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s*;?\\s*$",
                                   std::regex_constants::icase);
            if (std::regex_match(statement, matches, updateRegex)) {
                const std::string tableName = matches[1];
                const std::string setColumnName = matches[2];
                const std::string setValue = matches[3];
                const std::string whereColumnName = matches[4];
                const std::string whereValue = matches[5];

                auto& metadata = catalog.getTable(tableName);
                const size_t setColumn = static_cast<size_t>(
                    metadata.schema.getColumnIndex(setColumnName));
                auto predicate = makeEqualityPredicate(metadata.schema, whereColumnName, whereValue);
                auto field = parseFieldValue(metadata.schema.columns[setColumn].type, setValue);
                TableHeap tableHeap(metadata, page_manager);
                UpdateOperator updateOp(
                    tableHeap,
                    std::move(predicate),
                    {{setColumn, *field}}
                );
                executeStatementOperator(updateOp, printResult);
                statementFinished();
                continue;
            }

            std::regex deleteRegex("^\\s*DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s;]+)\\s*;?\\s*$",
                                   std::regex_constants::icase);
            if (std::regex_match(statement, matches, deleteRegex)) {
                const std::string tableName = matches[1];
                const std::string whereColumnName = matches[2];
                const std::string whereValue = matches[3];

                auto& metadata = catalog.getTable(tableName);
                auto predicate = makeEqualityPredicate(metadata.schema, whereColumnName, whereValue);
                TableHeap tableHeap(metadata, page_manager);
                DeleteOperator deleteOp(tableHeap, std::move(predicate));
                executeStatementOperator(deleteOp, printResult);
                statementFinished();
                continue;
            }

            std::regex selectRegex("^\\s*SELECT\\s+(.*)\\s*;?\\s*$",
                                   std::regex_constants::icase);
            if (std::regex_match(statement, matches, selectRegex)) {
                const std::string queryText = matches[1];
                auto components = parseQuery(queryText, catalog);
                auto& metadata = catalog.getTable(components.tableName);
                executeQuery(components, metadata, page_manager);
                statementFinished();
                continue;
            }

            throw std::runtime_error("Unsupported statement: " + statement);
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

        std::string line;
        while (std::getline(inputFile, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto tokens = split(line, '|');
            if (tokens.empty()) {
                continue;
            }

            if (tokens[0] == "TABLE") {
                if (tokens.size() < 3) {
                    throw std::runtime_error("Bad TABLE line: " + line);
                }
                const std::string tableName = tokens[1];
                db.createTable(tableName, parseTableSchema(tokens));
                continue;
            }

            const std::string tableName = tokens[0];
            if (!db.catalog.hasTable(tableName)) {
                throw std::runtime_error("Row appears before TABLE declaration: " + tableName);
            }

            std::vector<std::string> values(tokens.begin() + 1, tokens.end());
            db.insertRow(tableName, values, false);
        }
    }
};

void checkBookingInvariant(BuzzDB& db) {
    auto& seatsMetadata = db.catalog.getTable("seats");
    auto& holdsMetadata = db.catalog.getTable("holds");
    TableHeap seatsHeap(seatsMetadata, db.page_manager);
    TableHeap holdsHeap(holdsMetadata, db.page_manager);

    auto seats = seatsHeap.readAllTuples();
    auto holds = holdsHeap.readAllTuples();

    auto seatIdColumn = static_cast<size_t>(seatsMetadata.schema.getColumnIndex("id"));
    auto seatNoColumn = static_cast<size_t>(seatsMetadata.schema.getColumnIndex("seat_no"));
    auto seatStatusColumn = static_cast<size_t>(seatsMetadata.schema.getColumnIndex("status"));
    auto seatCustomerColumn = static_cast<size_t>(seatsMetadata.schema.getColumnIndex("customer"));
    auto holdSeatIdColumn = static_cast<size_t>(holdsMetadata.schema.getColumnIndex("seat_id"));
    auto holdCustomerColumn = static_cast<size_t>(holdsMetadata.schema.getColumnIndex("customer"));
    auto holdStatusColumn = static_cast<size_t>(holdsMetadata.schema.getColumnIndex("status"));

    bool ok = true;
    std::cout << "\nBooking invariant check\n";
    for (const auto& seat : seats) {
        if (seat->fields[seatStatusColumn]->asString() != "held") {
            continue;
        }

        int seatId = seat->fields[seatIdColumn]->asInt();
        std::string seatNo = seat->fields[seatNoColumn]->asString();
        std::string customer = seat->fields[seatCustomerColumn]->asString();

        bool foundOpenHold = false;
        for (const auto& hold : holds) {
            if (hold->fields[holdSeatIdColumn]->asInt() == seatId &&
                hold->fields[holdCustomerColumn]->asString() == customer &&
                hold->fields[holdStatusColumn]->asString() == "open") {
                foundOpenHold = true;
                break;
            }
        }

        if (!foundOpenHold) {
            std::cout << "Invariant violation: seat " << seatNo
                      << " is held for " << customer
                      << " but has no matching open hold." << std::endl;
            ok = false;
        }
    }

    if (ok) {
        std::cout << "Booking invariant holds." << std::endl;
    }
}

int main() {
    try {

    auto start = std::chrono::high_resolution_clock::now();

    {
        BuzzDB db;
        DatabaseImporter::importFile(db, "booking.txt");

        try {
            db.executeStatementsAndQueries({
                "BEGIN",
                "UPDATE flights SET destination = LAX WHERE id = 1",
                "COMMIT",
                "BEGIN",
                "UPDATE seats SET status = held WHERE seat_no = 1A",
                "UPDATE seats SET customer = garcia WHERE seat_no = 1A",
                "ABORT",
            });

            db.simulateCrashAfterCommitLog();
            db.executeStatementsAndQueries({
                "BEGIN",
                "UPDATE seats SET status = held WHERE seat_no = 1B",
                "UPDATE seats SET customer = zhang WHERE seat_no = 1B",
                "INSERT INTO holds VALUES (1, 2, zhang, open)",
                "COMMIT",
            });
        } catch (const std::runtime_error& crash) {
            std::cout << crash.what() << std::endl;
        }
    }

    std::cout << "\nRestart after crash\n";
    {
        BuzzDB db;
        db.executeStatementsAndQueries({
            "SELECT {*} FROM flights",
            "SELECT {*} FROM seats",
            "SELECT {*} FROM holds",
        });
        checkBookingInvariant(db);
    }

    auto end = std::chrono::high_resolution_clock::now();

    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << 
    std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() 
          << " microseconds" << std::endl;
    
    return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
