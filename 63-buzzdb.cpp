#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>

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
#include <cctype>
#include <utility>
#include <initializer_list>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <cerrno>
#include <fcntl.h>
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

static constexpr size_t PAGE_SIZE = 128;  // Tiny pages for recovery workloads
static constexpr size_t MAX_SLOTS = 4;    // Small slot directory leaves tuple space
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

using PageID = uint16_t;
using BootstrapPageID = uint8_t;
using TableId = uint16_t;
using LSN = uint64_t;

constexpr PageID INVALID_PAGE_ID = std::numeric_limits<PageID>::max();
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId SYS_TABLES_ID = 1;
constexpr TableId SYS_COLUMNS_ID = 2;
constexpr TableId FIRST_USER_TABLE_ID = 100;
constexpr uint32_t BUZZDB_MAGIC = 0x425A4442;
constexpr uint16_t BUZZDB_VERSION = 63;
constexpr size_t MAX_SYSTEM_TABLE_PAGES = 16;
const std::string log_filename = "buzzdb.log";
const std::string master_record_filename = "buzzdb.master";

void forceFileToStableStorage(const std::string& filename,
                              const std::string& description) {
    int fd = ::open(filename.c_str(), O_RDWR);
    if (fd < 0) {
        throw std::runtime_error("Unable to open " + description + " for fsync.");
    }
    if (::fsync(fd) != 0) {
        int error_code = errno;
        ::close(fd);
        throw std::runtime_error(
            "Unable to fsync " + description + ": " + std::strerror(error_code)
        );
    }
    if (::close(fd) != 0) {
        throw std::runtime_error("Unable to close " + description + " after fsync.");
    }
}

struct CatalogRoot {
    uint64_t catalog_version = 0;
    uint16_t tables_page_count = 0;
    BootstrapPageID tables_pages[MAX_SYSTEM_TABLE_PAGES] = {};
    uint16_t columns_page_count = 0;
    BootstrapPageID columns_pages[MAX_SYSTEM_TABLE_PAGES] = {};
    uint32_t checksum = 0;
};

// Page 0 stores two catalog roots; the highest valid version wins.
struct BootstrapPage {
    uint32_t magic = BUZZDB_MAGIC;
    uint16_t version = BUZZDB_VERSION;
    TableId next_table_id = FIRST_USER_TABLE_ID;
    CatalogRoot catalog_roots[2];
};

static_assert(sizeof(BootstrapPage) <= PAGE_SIZE,
              "BootstrapPage must fit in page 0.");

struct GarbageCollectionStats {
    size_t pages_before = 0;
    size_t pages_after = 0;
    size_t pages_reclaimed = 0;
};

uint32_t catalogRootChecksum(const CatalogRoot& root) {
    uint32_t checksum = 2166136261u;
    auto mix = [&](uint64_t value) {
        for (size_t i = 0; i < sizeof(value); i++) {
            checksum ^= static_cast<uint8_t>(value >> (i * 8));
            checksum *= 16777619u;
        }
    };
    mix(root.catalog_version);
    mix(root.tables_page_count);
    mix(root.columns_page_count);
    for (BootstrapPageID page_id : root.tables_pages) mix(page_id);
    for (BootstrapPageID page_id : root.columns_pages) mix(page_id);
    return checksum == 0 ? 1 : checksum;
}

bool isValidCatalogRoot(const CatalogRoot& root) {
    return root.tables_page_count <= MAX_SYSTEM_TABLE_PAGES &&
           root.columns_page_count <= MAX_SYSTEM_TABLE_PAGES &&
           root.checksum != 0 &&
           root.checksum == catalogRootChecksum(root);
}

int activeCatalogRootIndex(const BootstrapPage& bootstrap) {
    bool first_valid = isValidCatalogRoot(bootstrap.catalog_roots[0]);
    bool second_valid = isValidCatalogRoot(bootstrap.catalog_roots[1]);
    if (first_valid && second_valid) {
        return bootstrap.catalog_roots[0].catalog_version >=
               bootstrap.catalog_roots[1].catalog_version ? 0 : 1;
    }
    if (first_valid) return 0;
    if (second_valid) return 1;
    throw std::runtime_error("No valid catalog root found.");
}

const CatalogRoot& activeCatalogRoot(const BootstrapPage& bootstrap) {
    return bootstrap.catalog_roots[activeCatalogRootIndex(bootstrap)];
}

CatalogRoot& inactiveCatalogRoot(BootstrapPage& bootstrap) {
    return bootstrap.catalog_roots[1 - activeCatalogRootIndex(bootstrap)];
}

// Heap page order lives in TableMetadata::page_ids.
struct PageHeader {
    TableId table_id = INVALID_TABLE_ID;
    LSN page_lsn = 0;
};

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

static_assert(sizeof(Slot) * MAX_SLOTS + sizeof(PageHeader) < PAGE_SIZE,
              "Slot directory must leave space for tuples.");

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS + sizeof(PageHeader);

    SlottedPage(){
        auto* header = getHeader();
        header->table_id = INVALID_TABLE_ID;

        // Empty page -> initialize slot array inside page
        Slot* slot_array = getSlotArray();
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    PageHeader* getHeader() {
        // Header stays after slots so v47-style scans still work.
        return reinterpret_cast<PageHeader*>(
            page_data.get() + sizeof(Slot) * MAX_SLOTS
        );
    }

    Slot* getSlotArray() {
        return reinterpret_cast<Slot*>(page_data.get());
    }

    TableId getTableId() {
        return getHeader()->table_id;
    }

    void setTableId(TableId table_id) {
        getHeader()->table_id = table_id;
    }

    LSN getPageLSN() {
        return getHeader()->page_lsn;
    }

    void setPageLSN(LSN page_lsn) {
        getHeader()->page_lsn = page_lsn;
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
    }

    bool updateTuple(size_t index, std::unique_ptr<Tuple> tuple) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        if (index >= MAX_SLOTS || slot_array[index].empty) {
            return false;
        }

        auto serializedTuple = tuple->serialize();
        if (serializedTuple.size() > slot_array[index].length) {
            throw std::runtime_error("Updated tuple is too large to fit in existing slot.");
        }

        std::memset(
            page_data.get() + slot_array[index].offset,
            0,
            slot_array[index].length
        );
        std::memcpy(
            page_data.get() + slot_array[index].offset,
            serializedTuple.c_str(),
            serializedTuple.size()
        );
        return true;
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
constexpr bool TRACE_STORAGE = false;

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;
    size_t stable_storage_forces = 0;

    void forceDatabaseFileToStableStorage() {
        fileStream.flush();
        if (!fileStream) {
            throw std::runtime_error("Unable to flush database file stream.");
        }
        forceFileToStableStorage(database_filename, "database file");
        stable_storage_forces++;
    }

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

        if (TRACE_STORAGE) {
            std::cout << "Storage Manager :: Num pages: " << num_pages << "\n";
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
        forceDatabaseFileToStableStorage();
    }

    // Extend database file by one page
    void extend() {
        if (TRACE_STORAGE) {
            std::cout << "Extending database file \n";
        }

        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        forceDatabaseFileToStableStorage();

        // Update number of pages
        num_pages += 1;
    }

    void rewrite(const std::vector<PageID>& source_pages,
                 const std::map<PageID, std::unique_ptr<SlottedPage>>& replacement_pages) {
        std::string temp_filename = database_filename + ".gc";
        std::fstream output(temp_filename, std::ios::out | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to create temporary GC file.");
        }

        for (PageID new_page_id = 0; new_page_id < source_pages.size(); new_page_id++) {
            auto replacement = replacement_pages.find(new_page_id);
            if (replacement != replacement_pages.end()) {
                output.write(replacement->second->page_data.get(), PAGE_SIZE);
                continue;
            }

            PageID old_page_id = source_pages[new_page_id];
            if (old_page_id == INVALID_PAGE_ID) {
                throw std::runtime_error("GC rewrite is missing a source page.");
            }
            auto page = load(old_page_id);
            output.write(page->page_data.get(), PAGE_SIZE);
        }
        output.flush();
        output.close();
        forceFileToStableStorage(temp_filename, "GC database file");

        if (fileStream.is_open()) {
            fileStream.close();
        }
        if (std::rename(temp_filename.c_str(), database_filename.c_str()) != 0) {
            throw std::runtime_error("Unable to replace buzzdb.dat during GC.");
        }

        fileStream.open(database_filename, std::ios::in | std::ios::out);
        if (!fileStream) {
            throw std::runtime_error("Unable to reopen buzzdb.dat after GC.");
        }
        num_pages = source_pages.size();
    }

};

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

struct BufferPoolStats {
    size_t page_loads = 0;
    size_t cache_hits = 0;
    size_t evictions = 0;
    size_t data_page_writes = 0;
    size_t page_flushes_from_eviction = 0;
    size_t evictions_blocked_by_pins = 0;
    size_t max_pinned_pages = 0;
    size_t wal_page_flush_checks = 0;
    size_t wal_log_forces_before_page_flush = 0;
    size_t wal_log_force_skips = 0;
    std::map<std::string, size_t> data_page_writes_by_tag;
};

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, std::unique_ptr<SlottedPage>>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;
    std::map<PageID, size_t> pin_count;
    BufferPoolStats stats;
    std::function<bool(LSN)> wal_force_callback;
    std::function<void(PageID, LSN, const std::string&)> page_flush_callback;

    std::string evictionWriteTag(PageID page_id) {
        if (page_id == 0) {
            return "catalog eviction";
        }
        TableId table_id = pageMap[page_id]->getTableId();
        if (table_id == SYS_TABLES_ID || table_id == SYS_COLUMNS_ID) {
            return "catalog eviction";
        }
        return "eviction";
    }

public:
    BufferManager(): 
    policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {}

    void setWalForceCallback(std::function<bool(LSN)> callback) {
        wal_force_callback = std::move(callback);
    }

    void setPageFlushCallback(std::function<void(PageID, LSN, const std::string&)> callback) {
        page_flush_callback = std::move(callback);
    }

    void recordDataPageWrite(const std::string& tag) {
        stats.data_page_writes++;
        stats.data_page_writes_by_tag[tag]++;
    }

    void forceLogBeforeFlush(PageID page_id, const std::string& tag) {
        if (!wal_force_callback || page_id == 0) {
            return;
        }
        LSN page_lsn = pageMap[page_id]->getPageLSN();
        if (page_lsn == 0) {
            return;
        }

        stats.wal_page_flush_checks++;
        std::cout << "  WAL rule: before durable flush of page " << page_id
                  << " for " << tag
                  << ", force log through pageLSN " << page_lsn
                  << std::endl;
        if (wal_force_callback(page_lsn)) {
            stats.wal_log_forces_before_page_flush++;
            std::cout << "  WAL rule: forced log through LSN "
                      << page_lsn << " before durable page flush" << std::endl;
        } else {
            stats.wal_log_force_skips++;
            std::cout << "  WAL rule: log already durable through LSN "
                      << page_lsn << ", page can be forced" << std::endl;
        }
    }

    void flushPageToDisk(PageID page_id, const std::string& tag) {
        forceLogBeforeFlush(page_id, tag);
        LSN page_lsn = pageMap[page_id]->getPageLSN();
        storage_manager.flush(page_id, pageMap[page_id]);
        recordDataPageWrite(tag);
        // DPT cleanup is safe only after StorageManager makes the page durable.
        if (page_flush_callback && page_id != 0 && page_lsn != 0) {
            page_flush_callback(page_id, page_lsn, tag);
        }
    }

    std::unique_ptr<SlottedPage>& getPage(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            stats.cache_hits++;
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            PageID evictedPageId = INVALID_VALUE;
            size_t attempts = pageMap.size();
            while (attempts-- > 0) {
                PageID candidate = policy->evict();
                if (candidate == INVALID_VALUE) {
                    break;
                }
                if (pin_count.find(candidate) != pin_count.end()) {
                    stats.evictions_blocked_by_pins++;
                    policy->touch(candidate);
                    continue;
                }
                evictedPageId = candidate;
                break;
            }
            if (evictedPageId == INVALID_VALUE) {
                throw std::runtime_error("All buffer pages are pinned.");
            }
            if(evictedPageId != INVALID_VALUE){
                if (TRACE_STORAGE) {
                    std::cout << "Evicting page " << evictedPageId << "\n";
                }
                std::string eviction_tag = evictionWriteTag(evictedPageId);
                forceLogBeforeFlush(evictedPageId, eviction_tag);
                LSN page_lsn = pageMap[evictedPageId]->getPageLSN();
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
                recordDataPageWrite(eviction_tag);
                // DPT cleanup is safe only after StorageManager makes the page durable.
                if (page_flush_callback && evictedPageId != 0 && page_lsn != 0) {
                    page_flush_callback(evictedPageId, page_lsn, eviction_tag);
                }
                stats.evictions++;
                stats.page_flushes_from_eviction++;
                pageMap.erase(evictedPageId);
            }
        }

        auto page = storage_manager.load(page_id);
        stats.page_loads++;
        policy->touch(page_id);
        if (TRACE_STORAGE) {
            std::cout << "Loading page: " << page_id << "\n";
        }
        pageMap[page_id] = std::move(page);
        return pageMap[page_id];
    }

    void flushPage(int page_id, const std::string& tag = "explicit") {
        //std::cout << "Flush page " << page_id << "\n";
        if (pin_count.find(page_id) != pin_count.end()) {
            throw std::runtime_error("Cannot flush a pinned uncommitted page.");
        }
        flushPageToDisk(page_id, tag);
    }

    void flushAllPages(const std::string& tag = "flush all") {
        for (auto& entry : pageMap) {
            if (pin_count.find(entry.first) != pin_count.end()) {
                continue;
            }
            flushPageToDisk(entry.first, tag);
        }
    }

    void pinPage(PageID page_id) {
        pin_count[page_id]++;
        stats.max_pinned_pages = std::max(stats.max_pinned_pages, pin_count.size());
    }

    void unpinPage(PageID page_id) {
        auto it = pin_count.find(page_id);
        if (it == pin_count.end()) {
            return;
        }
        if (--it->second == 0) {
            pin_count.erase(it);
        }
    }

    PageID extend(TableId table_id = INVALID_TABLE_ID,
                  const std::string& tag = "new page initialization"){
        storage_manager.extend();
        recordDataPageWrite("new empty page");
        PageID page_id = static_cast<PageID>(storage_manager.num_pages - 1);
        auto& page = getPage(page_id);
        page->setTableId(table_id);
        flushPage(page_id, tag);
        return page_id;
    }

    size_t getNumPages(){
        return storage_manager.num_pages;
    }

    void printBufferPoolSummary() const {
        const std::vector<std::string> recovery_action_tags = {
            "commit force",
            "runtime abort undo",
            "restart redo",
            "restart undo"
        };
        const std::vector<std::string> buffer_pressure_tags = {
            "eviction",
            "uncommitted flush"
        };
        auto countTag = [&](const std::string& tag) {
            auto it = stats.data_page_writes_by_tag.find(tag);
            return it == stats.data_page_writes_by_tag.end() ? 0 : it->second;
        };
        auto sumTags = [&](const std::vector<std::string>& tags) {
            size_t total = 0;
            for (const auto& tag : tags) {
                total += countTag(tag);
            }
            return total;
        };
        auto printTags = [&](const std::vector<std::string>& tags) {
            for (const auto& tag : tags) {
                size_t count = countTag(tag);
                if (count != 0) {
                    std::cout << "    " << tag << ": " << count << std::endl;
                }
            }
        };

        std::cout << "Durable database page write summary:" << std::endl;
        size_t recovery_writes = sumTags(recovery_action_tags);
        size_t buffer_pressure_writes = sumTags(buffer_pressure_tags);
        std::cout << "  Recovery-applied durable page writes: "
                  << recovery_writes << std::endl;
        if (recovery_writes != 0) {
            std::cout << "  Recovery-applied writes by tag:" << std::endl;
            printTags(recovery_action_tags);
        }
        std::cout << "  User-data buffer-pressure durable page writes: "
                  << buffer_pressure_writes << std::endl;
        if (buffer_pressure_writes != 0) {
            std::cout << "  User-data buffer-pressure writes by tag:" << std::endl;
            printTags(buffer_pressure_tags);
        }
        std::cout << "  WAL page flush checks: "
                  << stats.wal_page_flush_checks << std::endl;
        std::cout << "  WAL log forces before page flush: "
                  << stats.wal_log_forces_before_page_flush << std::endl;
        std::cout << "  WAL force skips because log was already durable: "
                  << stats.wal_log_force_skips << std::endl;
    }

    void rewriteDatabase(
        const std::vector<PageID>& source_pages,
        const std::map<PageID, std::unique_ptr<SlottedPage>>& replacement_pages) {
        storage_manager.rewrite(source_pages, replacement_pages);
        pageMap.clear();
        policy = std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY);
    }

    bool isEmptyDatabase() const {
        return storage_manager.num_pages == 0;
    }



};


class Catalog;

struct MasterRecord {
    LSN checkpoint_begin_lsn = 0;
    size_t checkpoint_begin_offset = 0;
};

class LogManager {
private:
    struct PendingRecord {
        LSN lsn;
        size_t offset;
        std::string record;
    };

    std::vector<PendingRecord> pending_records;
    std::map<LSN, size_t> log_offsets;
    LSN next_lsn = 1;
    LSN flushed_lsn = 0;
    size_t next_log_offset = 0;
    size_t records_written = 0;
    size_t bytes_written = 0;
    size_t stable_log_forces = 0;
    size_t stable_master_forces = 0;

    size_t durableLogSize() const {
        std::ifstream input(log_filename, std::ios::binary | std::ios::ate);
        if (!input) {
            return 0;
        }
        return static_cast<size_t>(input.tellg());
    }

public:
    void reset() {
        std::ofstream output(log_filename, std::ios::trunc);
        std::remove(master_record_filename.c_str());
        pending_records.clear();
        log_offsets.clear();
        next_lsn = 1;
        flushed_lsn = 0;
        next_log_offset = 0;
        records_written = 0;
        bytes_written = 0;
        stable_log_forces = 0;
        stable_master_forces = 0;
    }

    LSN append(const std::string& record) {
        LSN lsn = next_lsn++;
        std::string durable_record = std::to_string(lsn) + " " + record;
        pending_records.push_back({lsn, next_log_offset, durable_record});
        log_offsets[lsn] = next_log_offset;
        records_written++;
        bytes_written += durable_record.size() + 1;
        next_log_offset += durable_record.size() + 1;
        return lsn;
    }

    bool forceUpTo(LSN lsn) {
        if (lsn <= flushed_lsn) {
            return false;
        }

        std::ofstream output(log_filename, std::ios::app);
        if (!output) {
            throw std::runtime_error("Unable to force recovery log.");
        }

        size_t records_to_remove = 0;
        LSN durable_lsn = flushed_lsn;
        while (records_to_remove < pending_records.size() &&
               pending_records[records_to_remove].lsn <= lsn) {
            output << pending_records[records_to_remove].record << "\n";
            durable_lsn = pending_records[records_to_remove].lsn;
            records_to_remove++;
        }
        output.flush();
        if (!output) {
            throw std::runtime_error("Unable to write recovery log.");
        }
        output.close();
        if (records_to_remove == 0) {
            return false;
        }
        forceFileToStableStorage(log_filename, "recovery log");
        stable_log_forces++;
        flushed_lsn = durable_lsn;
        pending_records.erase(
            pending_records.begin(),
            pending_records.begin() + static_cast<std::ptrdiff_t>(records_to_remove)
        );
        return true;
    }

    std::vector<std::string> readFromOffset(size_t start_offset) {
        std::ifstream input(log_filename, std::ios::binary);
        std::vector<std::string> records;
        next_log_offset = std::max(next_log_offset, durableLogSize());
        if (!input) {
            return records;
        }
        if (start_offset != 0) {
            input.seekg(static_cast<std::streamoff>(start_offset), std::ios::beg);
            if (!input) {
                throw std::runtime_error("Unable to seek to ARIES log offset.");
            }
        }
        std::string line;
        LSN durable_lsn = flushed_lsn;
        size_t line_offset = start_offset;
        while (std::getline(input, line)) {
            if (!line.empty()) {
                std::istringstream record_input(line);
                LSN lsn;
                record_input >> lsn;
                log_offsets[lsn] = line_offset;
                durable_lsn = std::max(durable_lsn, lsn);
                records.push_back(line);
            }
            line_offset += line.size() + 1;
        }
        flushed_lsn = durable_lsn;
        next_lsn = std::max(next_lsn, durable_lsn + 1);
        return records;
    }

    size_t getRecordsWritten() const {
        return records_written;
    }

    size_t getBytesWritten() const {
        return bytes_written;
    }

    size_t getBytesOnDisk() const {
        std::ifstream input(log_filename, std::ios::binary | std::ios::ate);
        if (!input) {
            return 0;
        }
        return static_cast<size_t>(input.tellg());
    }

    LSN getFlushedLSN() const {
        return flushed_lsn;
    }

    size_t getStableLogForces() const {
        return stable_log_forces;
    }

    size_t getStableMasterForces() const {
        return stable_master_forces;
    }

    size_t getLogOffset(LSN lsn) const {
        auto offset = log_offsets.find(lsn);
        if (offset == log_offsets.end()) {
            throw std::runtime_error("No log offset found for LSN.");
        }
        return offset->second;
    }

    void writeMasterRecord(LSN checkpoint_begin_lsn) {
        std::ofstream output(master_record_filename, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to write ARIES master record.");
        }
        output << "checkpoint_lsn " << checkpoint_begin_lsn << "\n"
               << "checkpoint_offset " << getLogOffset(checkpoint_begin_lsn) << "\n";
        output.flush();
        if (!output) {
            throw std::runtime_error("Unable to write ARIES master record.");
        }
        output.close();
        forceFileToStableStorage(master_record_filename, "ARIES master record");
        stable_master_forces++;
    }

    MasterRecord readMasterRecord() const {
        std::ifstream input(master_record_filename);
        MasterRecord master_record;
        if (input) {
            std::string key;
            input >> key >> master_record.checkpoint_begin_lsn;
            if (key != "checkpoint_lsn") {
                throw std::runtime_error("Malformed ARIES master record.");
            }
            input >> key >> master_record.checkpoint_begin_offset;
            if (key != "checkpoint_offset") {
                throw std::runtime_error("Malformed ARIES master record.");
            }
        }
        return master_record;
    }
};

// In-memory copy of one ARIES page-update record for runtime undo.
struct PageUpdateLogRecord {
    LSN lsn = 0;
    LSN prev_lsn = 0;
    TableId table_id;
    PageID page_id;
    size_t slot_id;
    std::unique_ptr<Tuple> before_tuple;
    std::unique_ptr<Tuple> after_tuple;
};

// Recovery policy for v63: ARIES checkpoints plus durable log/page flushes.
class RecoveryManager {
private:
    enum class TxnStatus {
        RUNNING,
        COMMITTING,
        ABORTING
    };

    struct TxnTableEntry {
        TxnStatus status = TxnStatus::RUNNING;
        LSN last_lsn = 0;
    };

    // Shared storage, catalog, and WAL components used by recovery.
    BufferManager& buffer_manager;
    Catalog& catalog;
    LogManager log_manager;
    // Current transaction state and ARIES lastLSN chain.
    bool txn_active = false;
    bool txn_logged = false;
    LSN current_txn_last_lsn = 0;
    int next_txn_id = 1;
    int current_txn_id = 0;
    // Crash knobs for the two recovery demos in main().
    bool crash_after_commit_before_flush = false;
    bool crash_after_steal_before_commit = false;
    bool checkpoint_active = false;
    LSN active_checkpoint_begin_lsn = 0;
    // In-memory page-update records used for abort and savepoint rollback.
    std::vector<PageUpdateLogRecord> page_update_log_records;
    // Savepoints remember a transaction-local target LSN.
    std::map<std::string, LSN> savepoints;
    // Pages dirtied by the active transaction.
    std::vector<PageID> dirty_pages;
    // Runtime DPT snapshot written by fuzzy checkpoints.
    std::map<PageID, LSN> dirty_page_table;
    size_t before_image_records_logged = 0;
    size_t before_image_bytes_logged = 0;
    size_t after_image_records_logged = 0;
    size_t after_image_bytes_logged = 0;
    size_t committed_update_records = 0;
    size_t aborted_update_records = 0;
    size_t runtime_undo_records = 0;
    size_t partial_rollback_records = 0;
    size_t restart_undo_records = 0;
    size_t restart_redo_records = 0;
    size_t restart_analysis_records_total = 0;
    size_t restart_analysis_records_replayed = 0;
    size_t restart_analysis_bytes_skipped_by_checkpoint = 0;
    size_t clr_records_logged = 0;
    size_t restart_redo_records_examined = 0;
    size_t restart_redo_records_skipped_by_dpt = 0;
    size_t restart_redo_records_skipped_by_page_lsn = 0;
    size_t commit_log_forces = 0;
    size_t data_pages_forced_at_commit = 0;
    size_t abort_log_records = 0;
    size_t log_force_requests = 0;
    size_t log_force_writes = 0;
    size_t log_force_skips = 0;
    size_t checkpoints_written = 0;
    size_t checkpoint_analysis_start_lsn = 0;
    std::map<int, TxnTableEntry> active_transaction_table;

    static std::string txnStatusName(TxnStatus status) {
        switch (status) {
            case TxnStatus::RUNNING:
                return "RUNNING";
            case TxnStatus::COMMITTING:
                return "COMMITTING";
            case TxnStatus::ABORTING:
                return "ABORTING";
        }
        return "UNKNOWN";
    }

    static TxnStatus parseTxnStatusName(const std::string& status) {
        if (status == "RUNNING") {
            return TxnStatus::RUNNING;
        }
        if (status == "COMMITTING") {
            return TxnStatus::COMMITTING;
        }
        if (status == "ABORTING") {
            return TxnStatus::ABORTING;
        }
        throw std::runtime_error("Unknown transaction status in checkpoint: " + status);
    }

    LSN appendTxnRecord(int txn_id,
                        const std::string& type,
                        LSN prev_lsn) {
        return log_manager.append(
            type + " " + std::to_string(txn_id) + " " +
            std::to_string(prev_lsn)
        );
    }

    LSN appendTxnRecord(const std::string& type,
                        TxnStatus status,
                        LSN* prev_lsn_out = nullptr) {
        LSN prev_lsn = current_txn_last_lsn;
        LSN lsn = appendTxnRecord(current_txn_id, type, prev_lsn);
        current_txn_last_lsn = lsn;
        active_transaction_table[current_txn_id] = {status, lsn};
        if (prev_lsn_out != nullptr) {
            *prev_lsn_out = prev_lsn;
        }
        return lsn;
    }

    LSN appendClrRecord(int txn_id,
                        LSN prev_lsn,
                        LSN undo_next_lsn,
                        TableId table_id,
                        PageID page_id,
                        size_t slot_id,
                        Tuple& after_undo_tuple) {
        auto after_undo_image = after_undo_tuple.serialize();
        LSN clr_lsn = log_manager.append(
            "CLR " + std::to_string(txn_id) + " " +
            std::to_string(prev_lsn) + " " +
            std::to_string(undo_next_lsn) + " " +
            std::to_string(table_id) + " " +
            std::to_string(page_id) + " " +
            std::to_string(slot_id) + " " +
            after_undo_image
        );
        clr_records_logged++;
        if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
            dirty_page_table[page_id] = clr_lsn;
        }
        return clr_lsn;
    }

public:
    RecoveryManager(BufferManager& buffer_manager, Catalog& catalog)
        : buffer_manager(buffer_manager), catalog(catalog) {
        this->buffer_manager.setWalForceCallback([this](LSN lsn) {
            return forceLogUpTo(lsn);
        });
        this->buffer_manager.setPageFlushCallback(
            [this](PageID page_id, LSN page_lsn, const std::string& tag) {
                notePageFlushed(page_id, page_lsn, tag);
            }
        );
    }
    bool isActive() const { return txn_active; }
    void resetLog() {
        log_manager.reset();
        dirty_page_table.clear();
    }
    void simulateCrashAfterCommitBeforeFlush() { crash_after_commit_before_flush = true; }
    void simulateCrashAfterStealBeforeCommit() { crash_after_steal_before_commit = true; }
    void begin();
    void commit();
    void abort();
    void savepoint(const std::string& name);
    void rollbackTo(const std::string& name);
    void beginCheckpoint();
    void endCheckpoint();
    void checkpoint();
    void recover();
    LSN logUpdate(TableId table_id,
                   PageID page_id,
                   size_t slot_id,
                   std::unique_ptr<Tuple> before_tuple,
                   std::unique_ptr<Tuple> after_tuple);
    bool forceLogUpTo(LSN lsn);
    void notePageFlushed(PageID page_id, LSN page_lsn, const std::string& tag);
    void maybeCrashAfterSteal(PageID page_id);
    int getCurrentTxnId() const { return current_txn_id; }
    void printPolicy() const;
    void printSummary();
};


// One column in a table schema.
struct ColumnSchema {
    std::string name;
    FieldType type;
};

// Ordered list of columns for a table.
struct TableSchema {
    std::vector<ColumnSchema> columns;
};

// In-memory metadata for one table and the pages owned by it.
struct TableMetadata {
    TableId table_id;
    std::string name;
    TableSchema schema;
    std::vector<PageID> page_ids;
    PageID first_page;
    PageID last_page;
    size_t row_count;
    bool system_table = false;
};

struct CreateTableResult {
    TableId table_id;
    bool created;
};

// Runtime handle for one table's heap pages; owns no catalog metadata.
class TableHeap {
private:
    TableMetadata& metadata;
    BufferManager& buffer_manager;
    bool flush_on_insert;

public:
    TableHeap(TableMetadata& metadata,
              BufferManager& buffer_manager,
              bool flush_on_insert = true)
        : metadata(metadata),
          buffer_manager(buffer_manager),
          flush_on_insert(flush_on_insert) {
        if (metadata.page_ids.empty() && metadata.first_page != INVALID_PAGE_ID) {
            metadata.page_ids.push_back(metadata.first_page);
        }
    }

    PageID allocatePage() {
        PageID page_id = allocatePhysicalPage();
        metadata.page_ids.push_back(page_id);
        if (metadata.first_page == INVALID_PAGE_ID) {
            metadata.first_page = page_id;
        }
        metadata.last_page = page_id;
        return page_id;
    }

    std::unique_ptr<SlottedPage>& getPage(PageID page_id) {
        auto& page = buffer_manager.getPage(page_id);
        if (page->getTableId() != metadata.table_id) {
            throw std::runtime_error("Page ownership mismatch for table: " + metadata.name);
        }
        return page;
    }

    const std::vector<PageID>& getPageIds() const {
        return metadata.page_ids;
    }

    PageID getLastPage() const {
        return metadata.last_page;
    }

    TableId getTableId() const {
        return metadata.table_id;
    }

    void flushInsertedPage(PageID page_id) {
        if (flush_on_insert) {
            buffer_manager.flushPage(page_id, "insert");
        }
    }

    void recordInsertedTuple() {
        metadata.row_count++;
    }

    size_t updateTuples(size_t where_column,
                        const Field& where_value,
                        const std::vector<std::pair<size_t, Field>>& assignments,
                        RecoveryManager* recovery_manager = nullptr) {
        size_t updated_count = 0;

        for (PageID page_id : metadata.page_ids) {
            auto* page = &getPage(page_id);
            char* page_buffer = (*page)->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);
            bool page_updated = false;

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_array[slot_itr].empty) {
                    continue;
                }

                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                std::istringstream iss(
                    std::string(tuple_data, slot_array[slot_itr].length)
                );
                auto old_tuple = Tuple::deserialize(iss);
                if (where_column >= old_tuple->fields.size() ||
                    !(*old_tuple->fields[where_column] == where_value)) {
                    continue;
                }

                auto new_tuple = std::make_unique<Tuple>();
                for (size_t field_index = 0;
                     field_index < old_tuple->fields.size();
                     field_index++) {
                    const Field* field = old_tuple->fields[field_index].get();
                    for (const auto& assignment : assignments) {
                        if (assignment.first == field_index) {
                            field = &assignment.second;
                            break;
                        }
                    }
                    new_tuple->addField(field->clone());
                }

                LSN update_lsn = 0;
                if (recovery_manager != nullptr) {
                    update_lsn = recovery_manager->logUpdate(
                        metadata.table_id,
                        page_id,
                        slot_itr,
                        old_tuple->clone(),
                        new_tuple->clone()
                    );
                }

                bool status = (*page)->updateTuple(slot_itr, std::move(new_tuple));
                assert(status == true);
                if (recovery_manager != nullptr) {
                    (*page)->setPageLSN(update_lsn);
                    std::cout << "  pageLSN: page " << page_id
                              << " = " << update_lsn << std::endl;
                }
                if (recovery_manager != nullptr) {
                    recovery_manager->maybeCrashAfterSteal(page_id);
                }
                page_updated = true;
                updated_count++;
            }

            if (page_updated && recovery_manager == nullptr) {
                buffer_manager.flushPage(page_id, "update without recovery");
            }
        }

        return updated_count;
    }

    void replacePage(PageID old_page_id, PageID new_page_id) {
        bool replaced = false;
        for (auto& page_id : metadata.page_ids) {
            if (page_id == old_page_id) {
                page_id = new_page_id;
                replaced = true;
            }
        }
        if (!replaced) {
            throw std::runtime_error("Shadow page source is not in table: " + metadata.name);
        }

        if (metadata.first_page == old_page_id) {
            metadata.first_page = new_page_id;
        }
        if (metadata.last_page == old_page_id) {
            metadata.last_page = new_page_id;
        }
    }

    // Physiological: identify the page physically, then update a slot within it.
    void applyPhysiologicalUpdate(PageID page_id,
                                  size_t slot_id,
                                  std::unique_ptr<Tuple> tuple,
                                  bool flush_page = true,
                                  const std::string& flush_tag = "recovery apply",
                                  LSN page_lsn = 0) {
        auto& page = getPage(page_id);
        bool status = page->updateTuple(slot_id, std::move(tuple));
        assert(status == true);
        if (page_lsn != 0) {
            page->setPageLSN(page_lsn);
        }
        if (flush_page) {
            buffer_manager.flushPage(page_id, flush_tag);
        }
    }

    size_t deleteTuples(size_t where_column, const Field& where_value) {
        size_t deleted_count = 0;

        for (PageID page_id : metadata.page_ids) {
            auto& page = getPage(page_id);
            char* page_buffer = page->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);
            bool page_updated = false;

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_array[slot_itr].empty) {
                    continue;
                }

                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                std::istringstream iss(
                    std::string(tuple_data, slot_array[slot_itr].length)
                );
                auto tuple = Tuple::deserialize(iss);
                if (where_column >= tuple->fields.size() ||
                    !(*tuple->fields[where_column] == where_value)) {
                    continue;
                }

                page->deleteTuple(slot_itr);
                page_updated = true;
                deleted_count++;
            }

            if (page_updated) {
                buffer_manager.flushPage(page_id, "delete");
            }
        }

        metadata.row_count -= deleted_count;
        return deleted_count;
    }

    std::vector<std::unique_ptr<Tuple>> readAllTuples() {
        std::vector<std::unique_ptr<Tuple>> tuples;

        for (PageID page_id : metadata.page_ids) {
            auto& page = getPage(page_id);
            char* page_buffer = page->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (!slot_array[slot_itr].empty) {
                    assert(slot_array[slot_itr].offset != INVALID_VALUE);
                    const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                    std::istringstream iss(
                        std::string(tuple_data, slot_array[slot_itr].length)
                    );
                    tuples.push_back(Tuple::deserialize(iss));
                }
            }
        }

        return tuples;
    }

private:
    PageID allocatePhysicalPage() {
        PageID page_id = buffer_manager.extend(metadata.table_id);
        auto& page = buffer_manager.getPage(page_id);
        page->setTableId(metadata.table_id);
        buffer_manager.flushPage(page_id, "page allocation");
        return page_id;
    }
};

bool insertTupleIntoTable(TableHeap& table, std::unique_ptr<Tuple> tuple) {
    auto insertIntoPage = [&](PageID page_id) {
        auto& page = table.getPage(page_id);
        if (page->addTuple(tuple->clone())) {
            table.flushInsertedPage(page_id);
            table.recordInsertedTuple();
            return true;
        }
        return false;
    };

    PageID last_page = table.getLastPage();
    if (last_page != INVALID_PAGE_ID && insertIntoPage(last_page)) {
        return true;
    }

    for (PageID page_id : table.getPageIds()) {
        if (page_id != last_page && insertIntoPage(page_id)) {
            return true;
        }
    }

    PageID page_id = table.allocatePage();
    auto& page = table.getPage(page_id);
    if (page->addTuple(std::move(tuple))) {
        table.flushInsertedPage(page_id);
        table.recordInsertedTuple();
        return true;
    }

    return false;
}

// Catalog records are stored as ordinary tuples in system tables.
class Catalog {
private:
    BufferManager& buffer_manager;
    TableId next_table_id = FIRST_USER_TABLE_ID;
    bool initialized_new_database = false;
    std::unordered_map<std::string, TableMetadata> tables_by_name;
    std::unordered_map<TableId, std::string> table_names_by_id;

public:
    explicit Catalog(BufferManager& buffer_manager) : buffer_manager(buffer_manager) {}

    void bootstrap() {
        // Empty files create the catalog; existing files load it.
        if (buffer_manager.isEmptyDatabase()) {
            initializeNewDatabase();
            initialized_new_database = true;
            return;
        }

        const auto& bootstrap_page = getBootstrap();
        installSystemTables(bootstrap_page);
        next_table_id = bootstrap_page.next_table_id;
    }

    bool isNewDatabase() const {
        return initialized_new_database;
    }

    std::vector<std::string> listUserTableNames() {
        auto& tables_metadata = getTable(SYS_TABLES_ID);
        TableHeap tables_heap(tables_metadata, buffer_manager);
        std::vector<std::string> table_names;

        for (auto& tuple : tables_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            if (table_id == SYS_TABLES_ID || table_id == SYS_COLUMNS_ID) {
                continue;
            }
            table_names.push_back(tuple->fields[1]->asString());
        }

        return table_names;
    }

    CreateTableResult createTable(const std::string& name, TableSchema schema) {
        auto it = tables_by_name.find(name);
        if (it != tables_by_name.end()) {
            // Existing declarations must match the stored schema.
            validateSchema(name, it->second.schema, schema);
            return {it->second.table_id, false};
        }

        if (loadTableByName(name)) {
            auto& metadata = tables_by_name.at(name);
            // Lazily load existing table metadata from the catalog.
            validateSchema(name, metadata.schema, schema);
            return {metadata.table_id, false};
        }

        // New tables start with one owned heap page.
        TableId table_id = next_table_id++;
        PageID first_page = buffer_manager.extend(table_id);
        auto& page = buffer_manager.getPage(first_page);
        page->setTableId(table_id);
        buffer_manager.flushPage(first_page, "table create");

        TableMetadata metadata{
            table_id, name, std::move(schema), {first_page},
            first_page, first_page, 0, false
        };
        auto& cached_metadata = cacheTable(std::move(metadata));

        // Store table and column metadata in catalog tables.
        persistTableRecord(cached_metadata);
        persistColumns(cached_metadata);
        persistNextTableId();
        return {table_id, true};
    }

    TableMetadata& getTable(const std::string& name) {
        auto it = tables_by_name.find(name);
        if (it != tables_by_name.end()) {
            return it->second;
        }

        if (loadTableByName(name)) {
            return tables_by_name.at(name);
        }

        throw std::runtime_error("Unknown table: " + name);
    }

    TableMetadata& getTable(TableId table_id) {
        auto it = table_names_by_id.find(table_id);
        if (it != table_names_by_id.end()) {
            return tables_by_name.at(it->second);
        }

        if (loadTableById(table_id)) {
            return tables_by_name.at(table_names_by_id.at(table_id));
        }

        throw std::runtime_error("Unknown table id.");
    }

    void persistTableMetadata(TableMetadata& metadata) {
        if (!metadata.system_table) {
            persistTableRecord(metadata);
        }
    }

    size_t persistTableMetadataAtomically(const std::vector<TableMetadata>& updates,
                                          bool crash_before_root_switch) {
        if (updates.empty()) {
            return 0;
        }

        auto& tables_metadata = getTable(SYS_TABLES_ID);
        TableMetadata shadow_tables_metadata = tables_metadata;
        shadow_tables_metadata.page_ids.clear();
        for (PageID page_id : tables_metadata.page_ids) {
            shadow_tables_metadata.page_ids.push_back(copyCatalogPage(page_id));
        }
        shadow_tables_metadata.first_page = shadow_tables_metadata.page_ids.front();
        shadow_tables_metadata.last_page = shadow_tables_metadata.page_ids.back();

        persistTableRecordInHeap(shadow_tables_metadata, shadow_tables_metadata, false);
        for (const auto& metadata : updates) {
            persistTableRecordInHeap(shadow_tables_metadata, metadata, false);
        }

        BootstrapPage bootstrap_page = getBootstrap();
        const auto& active_root = activeCatalogRoot(bootstrap_page);
        auto& new_root = inactiveCatalogRoot(bootstrap_page);
        new_root = active_root;
        new_root.catalog_version = active_root.catalog_version + 1;
        storeBootstrapPageIds(new_root.tables_page_count,
                              new_root.tables_pages,
                              shadow_tables_metadata.page_ids);
        new_root.checksum = catalogRootChecksum(new_root);
        if (crash_before_root_switch) {
            std::cout << "Simulated crash before catalog root switch." << std::endl;
            std::exit(2);
        }

        // The catalog root switch is the shadow transaction commit point.
        flushBootstrap(bootstrap_page);
        tables_metadata.page_ids = shadow_tables_metadata.page_ids;
        tables_metadata.first_page = shadow_tables_metadata.first_page;
        tables_metadata.last_page = shadow_tables_metadata.last_page;
        return shadow_tables_metadata.page_ids.size();
    }

    GarbageCollectionStats collectGarbage() {
        GarbageCollectionStats stats;
        stats.pages_before = buffer_manager.getNumPages();
        if (stats.pages_before == 0) {
            return stats;
        }

        buffer_manager.flushAllPages("gc flush all");
        BootstrapPage old_bootstrap = getBootstrap();
        const auto& old_root = activeCatalogRoot(old_bootstrap);
        auto old_tables_pages = bootstrapPageIds(old_root.tables_page_count,
                                                 old_root.tables_pages);
        auto old_columns_pages = bootstrapPageIds(old_root.columns_page_count,
                                                  old_root.columns_pages);

        auto& tables_metadata = getTable(SYS_TABLES_ID);
        auto& columns_metadata = getTable(SYS_COLUMNS_ID);
        tables_metadata.page_ids = old_tables_pages;
        columns_metadata.page_ids = old_columns_pages;

        std::vector<TableMetadata> user_tables;
        for (const auto& table_name : listUserTableNames()) {
            user_tables.push_back(getTable(table_name));
        }

        // Compact ids are assigned only to pages reachable from the active root.
        std::map<PageID, PageID> page_remap;
        page_remap[0] = 0;
        PageID next_page_id = 1;
        auto remapPages = [&](const std::vector<PageID>& old_pages) {
            std::vector<PageID> new_pages;
            for (PageID old_page_id : old_pages) {
                auto it = page_remap.find(old_page_id);
                if (it == page_remap.end()) {
                    it = page_remap.emplace(old_page_id, next_page_id++).first;
                }
                new_pages.push_back(it->second);
            }
            return new_pages;
        };
        auto setMetadataPages = [](TableMetadata& metadata,
                                   const std::vector<PageID>& page_ids) {
            metadata.page_ids = page_ids;
            metadata.first_page = page_ids.empty() ? INVALID_PAGE_ID : page_ids.front();
            metadata.last_page = page_ids.empty() ? INVALID_PAGE_ID : page_ids.back();
        };

        TableMetadata remapped_tables_metadata = tables_metadata;
        TableMetadata remapped_columns_metadata = columns_metadata;
        setMetadataPages(remapped_tables_metadata, remapPages(old_tables_pages));
        setMetadataPages(remapped_columns_metadata, remapPages(old_columns_pages));

        std::vector<TableMetadata> remapped_user_tables = user_tables;
        for (size_t i = 0; i < user_tables.size(); i++) {
            setMetadataPages(
                remapped_user_tables[i],
                remapPages(user_tables[i].page_ids)
            );
        }

        // Unchanged pages are streamed from old ids instead of buffered here.
        std::vector<PageID> source_pages(next_page_id, INVALID_PAGE_ID);
        for (const auto& entry : page_remap) {
            source_pages[entry.second] = entry.first;
        }
        std::map<PageID, std::unique_ptr<SlottedPage>> replacement_pages;
        auto makeReplacementPage = [&](PageID page_id,
                                       TableId table_id) -> SlottedPage& {
            replacement_pages[page_id] = std::make_unique<SlottedPage>();
            replacement_pages[page_id]->setTableId(table_id);
            return *replacement_pages[page_id];
        };

        BootstrapPage new_bootstrap;
        new_bootstrap.next_table_id = old_bootstrap.next_table_id;
        auto& new_root = new_bootstrap.catalog_roots[0];
        new_root.catalog_version = old_root.catalog_version + 1;
        storeBootstrapPageIds(new_root.tables_page_count,
                              new_root.tables_pages,
                              remapped_tables_metadata.page_ids);
        storeBootstrapPageIds(new_root.columns_page_count,
                              new_root.columns_pages,
                              remapped_columns_metadata.page_ids);
        new_root.checksum = catalogRootChecksum(new_root);

        auto& bootstrap_page = makeReplacementPage(0, INVALID_TABLE_ID);
        std::memset(bootstrap_page.page_data.get(), 0, PAGE_SIZE);
        std::memcpy(
            bootstrap_page.page_data.get(),
            &new_bootstrap,
            sizeof(BootstrapPage)
        );
        for (PageID page_id : remapped_tables_metadata.page_ids) {
            // Page 0 and __tables are rebuilt because they contain page ids.
            makeReplacementPage(page_id, SYS_TABLES_ID);
        }

        TableHeap columns_heap(columns_metadata, buffer_manager);
        remapped_tables_metadata.row_count = 2 + remapped_user_tables.size();
        remapped_columns_metadata.row_count = columns_heap.readAllTuples().size();
        auto insertTableRecord = [&](const TableMetadata& metadata) {
            auto tuple = makeTableRecordTuple(metadata);
            for (PageID page_id : remapped_tables_metadata.page_ids) {
                if (replacement_pages[page_id]->addTuple(tuple->clone())) {
                    return;
                }
            }
            throw std::runtime_error("Compacted __tables pages are full.");
        };
        insertTableRecord(remapped_tables_metadata);
        insertTableRecord(remapped_columns_metadata);
        for (const auto& metadata : remapped_user_tables) {
            insertTableRecord(metadata);
        }

        for (PageID page_id = 0; page_id < source_pages.size(); page_id++) {
            if (source_pages[page_id] == INVALID_PAGE_ID &&
                replacement_pages.find(page_id) == replacement_pages.end()) {
                throw std::runtime_error("GC failed to map a compact page.");
            }
        }

        buffer_manager.rewriteDatabase(source_pages, replacement_pages);
        setMetadataPages(tables_metadata, remapped_tables_metadata.page_ids);
        setMetadataPages(columns_metadata, remapped_columns_metadata.page_ids);
        tables_metadata.row_count = remapped_tables_metadata.row_count;
        columns_metadata.row_count = remapped_columns_metadata.row_count;
        for (const auto& metadata : remapped_user_tables) {
            auto& cached_metadata = getTable(metadata.table_id);
            setMetadataPages(cached_metadata, metadata.page_ids);
        }

        stats.pages_after = source_pages.size();
        stats.pages_reclaimed = stats.pages_before > stats.pages_after
            ? stats.pages_before - stats.pages_after
            : 0;
        return stats;
    }

private:
    BootstrapPage getBootstrap() {
        if (buffer_manager.isEmptyDatabase()) {
            throw std::runtime_error("Bootstrap page is not initialized.");
        }

        // Page 0 points to catalog roots.
        BootstrapPage bootstrap;
        auto& page = buffer_manager.getPage(0);
        std::memcpy(&bootstrap, page->page_data.get(), sizeof(BootstrapPage));
        if (bootstrap.magic != BUZZDB_MAGIC || bootstrap.version != BUZZDB_VERSION) {
            throw std::runtime_error(
                database_filename +
                " was created by an older BuzzDB format. "
                "v63 uses ARIES checkpoint/pageLSN/CLR metadata; use a fresh directory or remove buzzdb.dat."
            );
        }
        return bootstrap;
    }

    void initializeBootstrap(const BootstrapPage& bootstrap) {
        if (!buffer_manager.isEmptyDatabase()) {
            throw std::runtime_error("Cannot initialize bootstrap page in a non-empty database.");
        }

        // Reserve page 0 before table heap pages.
        PageID bootstrap_page_id = buffer_manager.extend(INVALID_TABLE_ID, "bootstrap");
        assert(bootstrap_page_id == 0);
        auto& page = buffer_manager.getPage(0);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), &bootstrap, sizeof(BootstrapPage));
        buffer_manager.flushPage(0, "bootstrap");
    }

    void flushBootstrap(const BootstrapPage& bootstrap) {
        auto& page = buffer_manager.getPage(0);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), &bootstrap, sizeof(BootstrapPage));
        buffer_manager.flushPage(0, "bootstrap");
    }

    static void validateSchema(const std::string& table_name,
                               const TableSchema& existing_schema,
                               const TableSchema& requested_schema) {
        if (existing_schema.columns.size() != requested_schema.columns.size()) {
            throw std::runtime_error("Schema mismatch for table: " + table_name);
        }

        // Query column references are positional.
        for (size_t column_index = 0; column_index < existing_schema.columns.size(); column_index++) {
            const auto& existing_column = existing_schema.columns[column_index];
            const auto& requested_column = requested_schema.columns[column_index];
            if (existing_column.name != requested_column.name ||
                existing_column.type != requested_column.type) {
                throw std::runtime_error("Schema mismatch for table: " + table_name);
            }
        }
    }

    static std::string encodePageIds(const std::vector<PageID>& page_ids) {
        std::string encoded;
        for (PageID page_id : page_ids) {
            if (!encoded.empty()) encoded += ",";
            encoded += std::to_string(page_id);
        }
        return encoded;
    }

    static std::vector<PageID> decodePageIds(const std::string& encoded) {
        std::vector<PageID> page_ids;
        std::istringstream input(encoded);
        PageID page_id;
        char comma;
        while (input >> page_id) {
            page_ids.push_back(page_id);
            input >> comma;
        }
        return page_ids;
    }

    static std::vector<PageID> bootstrapPageIds(uint16_t page_count,
                                                const BootstrapPageID* pages) {
        std::vector<PageID> page_ids;
        for (uint16_t i = 0; i < page_count; i++) {
            page_ids.push_back(static_cast<PageID>(pages[i]));
        }
        return page_ids;
    }

    static void storeBootstrapPageIds(uint16_t& page_count,
                                      BootstrapPageID* pages,
                                      const std::vector<PageID>& page_ids) {
        if (page_ids.size() > MAX_SYSTEM_TABLE_PAGES) {
            throw std::runtime_error("System table page list is too large.");
        }
        page_count = static_cast<uint16_t>(page_ids.size());
        for (size_t i = 0; i < MAX_SYSTEM_TABLE_PAGES; i++) {
            if (i < page_ids.size() &&
                page_ids[i] > std::numeric_limits<BootstrapPageID>::max()) {
                throw std::runtime_error("System table page id is too large for bootstrap.");
            }
            pages[i] = i < page_ids.size()
                ? static_cast<BootstrapPageID>(page_ids[i])
                : std::numeric_limits<BootstrapPageID>::max();
        }
    }

    static TableSchema tablesTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"table_name", STRING},
            {"first_page", INT},
            {"last_page", INT},
            {"row_count", INT},
            {"page_ids", STRING}
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
        BootstrapPage bootstrap_page;
        initializeBootstrap(bootstrap_page);

        // Give system catalog tables their first heap pages.
        PageID tables_page = buffer_manager.extend(SYS_TABLES_ID, "system table create");
        PageID columns_page = buffer_manager.extend(SYS_COLUMNS_ID, "system table create");

        bootstrap_page = getBootstrap();
        auto& catalog_root = bootstrap_page.catalog_roots[0];
        catalog_root.catalog_version = 1;
        storeBootstrapPageIds(catalog_root.tables_page_count,
                              catalog_root.tables_pages,
                              std::vector<PageID>{tables_page});
        storeBootstrapPageIds(catalog_root.columns_page_count,
                              catalog_root.columns_pages,
                              std::vector<PageID>{columns_page});
        catalog_root.checksum = catalogRootChecksum(catalog_root);
        bootstrap_page.next_table_id = FIRST_USER_TABLE_ID;
        flushBootstrap(bootstrap_page);

        installSystemTables(bootstrap_page);

        auto& tables_metadata = getTable(SYS_TABLES_ID);
        auto& columns_metadata = getTable(SYS_COLUMNS_ID);
        persistTableRecord(tables_metadata);
        persistColumns(tables_metadata);
        persistTableRecord(columns_metadata);
        persistColumns(columns_metadata);
    }

    void installSystemTables(const BootstrapPage& bootstrap_page) {
        // Cache system tables before catalog lookups can work.
        const auto& catalog_root = activeCatalogRoot(bootstrap_page);
        auto tables_page_ids = bootstrapPageIds(catalog_root.tables_page_count,
                                                catalog_root.tables_pages);
        auto columns_page_ids = bootstrapPageIds(catalog_root.columns_page_count,
                                                 catalog_root.columns_pages);
        TableMetadata tables_metadata{
            SYS_TABLES_ID, "__tables", tablesTableSchema(),
            tables_page_ids,
            tables_page_ids.empty() ? INVALID_PAGE_ID : tables_page_ids.front(),
            tables_page_ids.empty() ? INVALID_PAGE_ID : tables_page_ids.back(),
            0, true
        };
        TableMetadata columns_metadata{
            SYS_COLUMNS_ID, "__columns", columnsTableSchema(),
            columns_page_ids,
            columns_page_ids.empty() ? INVALID_PAGE_ID : columns_page_ids.front(),
            columns_page_ids.empty() ? INVALID_PAGE_ID : columns_page_ids.back(),
            0, true
        };
        cacheTable(std::move(tables_metadata));
        cacheTable(std::move(columns_metadata));
    }

    TableMetadata& cacheTable(TableMetadata metadata) {
        TableId table_id = metadata.table_id;
        std::string table_name = metadata.name;
        auto result = tables_by_name.insert_or_assign(table_name, std::move(metadata));
        table_names_by_id[table_id] = table_name;
        return result.first->second;
    }

    void persistNextTableId() {
        BootstrapPage bootstrap_page = getBootstrap();
        bootstrap_page.next_table_id = next_table_id;
        flushBootstrap(bootstrap_page);
    }

    void syncSystemTableBootstrap(const TableMetadata& metadata) {
        BootstrapPage bootstrap_page = getBootstrap();
        const auto& active_root = activeCatalogRoot(bootstrap_page);
        auto& new_root = inactiveCatalogRoot(bootstrap_page);
        new_root = active_root;
        new_root.catalog_version = active_root.catalog_version + 1;

        // Keep page 0 in sync when a system table grows.
        if (metadata.table_id == SYS_TABLES_ID) {
            storeBootstrapPageIds(new_root.tables_page_count,
                                  new_root.tables_pages,
                                  metadata.page_ids);
        } else if (metadata.table_id == SYS_COLUMNS_ID) {
            storeBootstrapPageIds(new_root.columns_page_count,
                                  new_root.columns_pages,
                                  metadata.page_ids);
        } else {
            return;
        }

        new_root.checksum = catalogRootChecksum(new_root);
        flushBootstrap(bootstrap_page);
    }

    void insertSystemTuple(TableId system_table_id, std::unique_ptr<Tuple> tuple) {
        auto& metadata = getTable(system_table_id);
        PageID previous_last_page = metadata.last_page;

        // System tables share heap code; only page-0 roots need syncing.
        TableHeap table(metadata, buffer_manager);
        bool status = insertTupleIntoTable(table, std::move(tuple));
        assert(status == true);

        if (metadata.last_page != previous_last_page) {
            syncSystemTableBootstrap(metadata);
        }
    }

    std::unique_ptr<Tuple> makeTableRecordTuple(const TableMetadata& metadata) {
        // Match tuple layout to tablesTableSchema().
        auto tuple = std::make_unique<Tuple>();
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.table_id)));
        tuple->addField(std::make_unique<Field>(metadata.name));
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.first_page)));
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.last_page)));
        tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.row_count)));
        tuple->addField(std::make_unique<Field>(encodePageIds(metadata.page_ids)));
        return tuple;
    }

    PageID copyCatalogPage(PageID page_id) {
        auto& source_page = buffer_manager.getPage(page_id);
        PageID shadow_page_id = buffer_manager.extend(source_page->getTableId());
        auto& shadow_page = buffer_manager.getPage(shadow_page_id);
        std::memcpy(shadow_page->page_data.get(), source_page->page_data.get(), PAGE_SIZE);
        buffer_manager.flushPage(shadow_page_id, "shadow page copy");
        std::cout << "Copy page: catalog page " << page_id
                  << " -> " << shadow_page_id << std::endl;
        return shadow_page_id;
    }

    void insertTableRecordInHeap(TableMetadata& tables_metadata,
                                 std::unique_ptr<Tuple> tuple,
                                 bool sync_bootstrap) {
        PageID previous_last_page = tables_metadata.last_page;
        TableHeap table(tables_metadata, buffer_manager);
        bool status = insertTupleIntoTable(table, std::move(tuple));
        assert(status == true);
        if (sync_bootstrap && tables_metadata.last_page != previous_last_page) {
            syncSystemTableBootstrap(tables_metadata);
        }
    }

    void persistTableRecordInHeap(TableMetadata& tables_metadata,
                                  const TableMetadata& metadata,
                                  bool sync_bootstrap) {
        auto tuple = makeTableRecordTuple(metadata);
        TableHeap tables_heap(tables_metadata, buffer_manager);

        // Replace visible catalog row with current metadata.
        for (PageID page_id : tables_heap.getPageIds()) {
            auto& page = tables_heap.getPage(page_id);
            char* page_buffer = page->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_array[slot_itr].empty) {
                    continue;
                }

                const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                std::istringstream iss(
                    std::string(tuple_data, slot_array[slot_itr].length)
                );
                auto old_tuple = Tuple::deserialize(iss);
                TableId table_id = static_cast<TableId>(old_tuple->fields[0]->asInt());

                if (table_id == metadata.table_id) {
                    page->deleteTuple(slot_itr);
                    // Reuse the slot if the new row still fits.
                    if (page->addTuple(tuple->clone())) {
                        buffer_manager.flushPage(page_id, "catalog metadata");
                    } else {
                        insertTableRecordInHeap(
                            tables_metadata, std::move(tuple), sync_bootstrap
                        );
                    }
                    return;
                }
            }
        }

        insertTableRecordInHeap(tables_metadata, std::move(tuple), sync_bootstrap);
    }

    void persistTableRecord(const TableMetadata& metadata) {
        auto& tables_metadata = getTable(SYS_TABLES_ID);
        persistTableRecordInHeap(tables_metadata, metadata, true);
    }

    void persistColumns(const TableMetadata& metadata) {
        for (size_t column_id = 0; column_id < metadata.schema.columns.size(); column_id++) {
            const auto& column = metadata.schema.columns[column_id];

            auto tuple = std::make_unique<Tuple>();
            tuple->addField(std::make_unique<Field>(static_cast<int>(metadata.table_id)));
            tuple->addField(std::make_unique<Field>(static_cast<int>(column_id)));
            tuple->addField(std::make_unique<Field>(column.name));
            tuple->addField(std::make_unique<Field>(static_cast<int>(column.type)));
            insertSystemTuple(SYS_COLUMNS_ID, std::move(tuple));
        }
    }

    bool loadTableByName(const std::string& name) {
        auto metadata = findTableRecord([&](const TableMetadata& candidate) {
            return candidate.name == name;
        });
        if (!metadata) {
            return false;
        }

        loadColumns(*metadata);
        cacheTable(std::move(*metadata));
        return true;
    }

    bool loadTableById(TableId table_id) {
        auto metadata = findTableRecord([&](const TableMetadata& candidate) {
            return candidate.table_id == table_id;
        });
        if (!metadata) {
            return false;
        }

        loadColumns(*metadata);
        cacheTable(std::move(*metadata));
        return true;
    }

    template <typename Predicate>
    std::optional<TableMetadata> findTableRecord(Predicate predicate) {
        auto& tables_metadata = getTable(SYS_TABLES_ID);
        TableHeap tables_heap(tables_metadata, buffer_manager);
        std::optional<TableMetadata> found;

        // __tables holds table metadata; __columns holds schemas.
        for (auto& tuple : tables_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            if (table_id == SYS_TABLES_ID || table_id == SYS_COLUMNS_ID) {
                continue;
            }
            PageID first_page = static_cast<PageID>(tuple->fields[2]->asInt());
            auto page_ids = tuple->fields.size() > 5
                ? decodePageIds(tuple->fields[5]->asString())
                : std::vector<PageID>{};
            if (page_ids.empty() && first_page != INVALID_PAGE_ID) {
                page_ids.push_back(first_page);
            }

            TableMetadata candidate{
                table_id,
                tuple->fields[1]->asString(),
                TableSchema{},
                page_ids,
                first_page,
                static_cast<PageID>(tuple->fields[3]->asInt()),
                static_cast<size_t>(tuple->fields[4]->asInt()),
                false
            };

            if (predicate(candidate)) {
                found = std::move(candidate);
            }
        }

        return found;
    }

    void loadColumns(TableMetadata& metadata) {
        auto& columns_metadata = getTable(SYS_COLUMNS_ID);
        TableHeap columns_heap(columns_metadata, buffer_manager);
        std::map<int, ColumnSchema> columns_by_id;

        // Rebuild schema by column_id, not scan order.
        for (auto& tuple : columns_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            if (table_id != metadata.table_id) {
                continue;
            }

            int column_id = tuple->fields[1]->asInt();
            auto column_name = tuple->fields[2]->asString();
            auto column_type = static_cast<FieldType>(tuple->fields[3]->asInt());
            columns_by_id[column_id] = ColumnSchema{column_name, column_type};
        }

        metadata.schema.columns.clear();
        for (const auto& entry : columns_by_id) {
            metadata.schema.columns.push_back(entry.second);
        }
    }
};

void RecoveryManager::begin() {
    if (txn_active) {
        throw std::runtime_error("Transaction already active.");
    }
    txn_active = true;
    current_txn_id = next_txn_id++;
    current_txn_last_lsn = 0;
    page_update_log_records.clear();
    savepoints.clear();
    dirty_pages.clear();
    std::cout << "\nTXN " << current_txn_id << " BEGIN" << std::endl;
    LSN begin_prev_lsn = 0;
    LSN begin_lsn = appendTxnRecord("BEGIN", TxnStatus::RUNNING, &begin_prev_lsn);
    std::cout << "  log: BEGIN txn " << current_txn_id
              << " LSN " << begin_lsn
              << " prevLSN " << begin_prev_lsn << std::endl;
    txn_logged = true;
}

void RecoveryManager::commit() {
    if (!txn_active) {
        throw std::runtime_error("COMMIT without BEGIN.");
    }

    std::cout << "TXN " << current_txn_id << " COMMIT" << std::endl;
    if (!page_update_log_records.empty()) {
        LSN commit_prev_lsn = 0;
        LSN commit_lsn = appendTxnRecord(
            "COMMIT", TxnStatus::COMMITTING, &commit_prev_lsn
        );
        std::cout << "  log: COMMIT txn " << current_txn_id
                  << " LSN " << commit_lsn
                  << " prevLSN " << commit_prev_lsn << std::endl;
        forceLogUpTo(commit_lsn);
        commit_log_forces++;
        committed_update_records += page_update_log_records.size();

        if (crash_after_commit_before_flush) {
            std::cout << "  recovery: forced COMMIT log, forced 0 data page(s)" << std::endl;
            std::cout << "  crash: after COMMIT log, before dirty page flush" << std::endl;
            std::exit(2);
        }

        std::cout << "  recovery: forced COMMIT log through LSN "
                  << commit_lsn << ", forced 0 data page(s), redo may be needed" << std::endl;
    } else {
        std::cout << "  recovery: read-only transaction, no COMMIT log needed" << std::endl;
    }

    LSN end_prev_lsn = current_txn_last_lsn;
    LSN end_lsn = appendTxnRecord(current_txn_id, "END", end_prev_lsn);
    std::cout << "  log: END txn " << current_txn_id
              << " LSN " << end_lsn
              << " prevLSN " << end_prev_lsn << std::endl;
    forceLogUpTo(end_lsn);
    active_transaction_table.erase(current_txn_id);
    std::cout << "  recovery: transaction cleanup complete" << std::endl;

    page_update_log_records.clear();
    savepoints.clear();
    dirty_pages.clear();
    txn_logged = false;
    crash_after_commit_before_flush = false;
    crash_after_steal_before_commit = false;
    current_txn_last_lsn = 0;
    current_txn_id = 0;
    txn_active = false;
}

void RecoveryManager::savepoint(const std::string& name) {
    if (!txn_active) {
        throw std::runtime_error("SAVEPOINT without BEGIN.");
    }

    savepoints[name] = current_txn_last_lsn;
    std::cout << "TXN " << current_txn_id
              << " SAVEPOINT " << name
              << " at LSN " << current_txn_last_lsn << std::endl;
}

void RecoveryManager::rollbackTo(const std::string& name) {
    if (!txn_active) {
        throw std::runtime_error("ROLLBACK TO without BEGIN.");
    }
    auto savepoint = savepoints.find(name);
    if (savepoint == savepoints.end()) {
        throw std::runtime_error("Unknown SAVEPOINT: " + name);
    }

    LSN target_lsn = savepoint->second;
    size_t undone = 0;
    std::cout << "TXN " << current_txn_id
              << " ROLLBACK TO " << name
              << " targetLSN " << target_lsn << std::endl;

    while (!page_update_log_records.empty() && page_update_log_records.back().lsn > target_lsn) {
        auto& update = page_update_log_records.back();
        auto& metadata = catalog.getTable(update.table_id);
        TableHeap table(metadata, buffer_manager);
        LSN clr_prev_lsn = current_txn_last_lsn;
        LSN clr_lsn = appendClrRecord(
            current_txn_id,
            clr_prev_lsn,
            update.prev_lsn,
            update.table_id,
            update.page_id,
            update.slot_id,
            *update.before_tuple
        );
        current_txn_last_lsn = clr_lsn;
        active_transaction_table[current_txn_id] = {TxnStatus::RUNNING, clr_lsn};
        std::cout << "  log: CLR txn " << current_txn_id
                  << " LSN " << clr_lsn
                  << " prevLSN " << clr_prev_lsn
                  << " undoNextLSN " << update.prev_lsn
                  << " for rollback to " << name << std::endl;
        table.applyPhysiologicalUpdate(
            update.page_id, update.slot_id, update.before_tuple->clone(),
            true, "partial rollback undo", clr_lsn
        );
        page_update_log_records.pop_back();
        runtime_undo_records++;
        partial_rollback_records++;
        undone++;
    }

    for (auto it = savepoints.begin(); it != savepoints.end(); ) {
        if (it->second > target_lsn) {
            it = savepoints.erase(it);
        } else {
            ++it;
        }
    }

    std::cout << "  recovery: partial rollback restored "
              << undone << " update record(s); transaction remains active"
              << std::endl;
}

void RecoveryManager::abort() {
    if (!txn_active) {
        throw std::runtime_error("ABORT without BEGIN.");
    }
    std::cout << "TXN " << current_txn_id << " ABORT" << std::endl;
    if (!page_update_log_records.empty()) {
        aborted_update_records += page_update_log_records.size();
        for (auto it = page_update_log_records.rbegin(); it != page_update_log_records.rend(); ++it) {
            auto& metadata = catalog.getTable(it->table_id);
            TableHeap table(metadata, buffer_manager);
            LSN clr_prev_lsn = current_txn_last_lsn;
            LSN clr_lsn = appendClrRecord(
                current_txn_id,
                clr_prev_lsn,
                it->prev_lsn,
                it->table_id,
                it->page_id,
                it->slot_id,
                *it->before_tuple
            );
            current_txn_last_lsn = clr_lsn;
            active_transaction_table[current_txn_id] = {TxnStatus::ABORTING, clr_lsn};
            std::cout << "  log: CLR txn " << current_txn_id
                      << " LSN " << clr_lsn
                      << " prevLSN " << clr_prev_lsn
                      << " undoNextLSN " << it->prev_lsn << std::endl;
            table.applyPhysiologicalUpdate(
                it->page_id, it->slot_id, it->before_tuple->clone(),
                true, "runtime abort undo", clr_lsn
            );
            runtime_undo_records++;
        }
        LSN abort_prev_lsn = 0;
        LSN abort_lsn = appendTxnRecord(
            "ABORT", TxnStatus::ABORTING, &abort_prev_lsn
        );
        std::cout << "  log: ABORT txn " << current_txn_id
                  << " LSN " << abort_lsn
                  << " prevLSN " << abort_prev_lsn << std::endl;
        forceLogUpTo(abort_lsn);
        abort_log_records++;
        std::cout << "  recovery: restored "
                  << page_update_log_records.size()
                  << " before-image record(s), forced restored page(s), wrote CLR+ABORT logs" << std::endl;
    } else {
        std::cout << "  recovery: no updates to discard" << std::endl;
    }
    LSN end_prev_lsn = current_txn_last_lsn;
    LSN end_lsn = appendTxnRecord(current_txn_id, "END", end_prev_lsn);
    std::cout << "  log: END txn " << current_txn_id
              << " LSN " << end_lsn
              << " prevLSN " << end_prev_lsn << std::endl;
    forceLogUpTo(end_lsn);
    active_transaction_table.erase(current_txn_id);
    std::cout << "  recovery: transaction cleanup complete" << std::endl;

    page_update_log_records.clear();
    savepoints.clear();
    dirty_pages.clear();
    txn_logged = false;
    crash_after_commit_before_flush = false;
    crash_after_steal_before_commit = false;
    current_txn_last_lsn = 0;
    current_txn_id = 0;
    txn_active = false;
}

void RecoveryManager::beginCheckpoint() {
    if (checkpoint_active) {
        throw std::runtime_error("BEGIN CHECKPOINT while another checkpoint is active.");
    }
    LSN begin_lsn = appendTxnRecord(0, "BEGIN_CHECKPOINT", 0);
    checkpoint_active = true;
    active_checkpoint_begin_lsn = begin_lsn;
    std::cout << "  log: BEGIN_CHECKPOINT LSN " << begin_lsn << std::endl;
}

void RecoveryManager::endCheckpoint() {
    if (!checkpoint_active) {
        throw std::runtime_error("END CHECKPOINT without BEGIN CHECKPOINT.");
    }
    std::stringstream record;
    record << "END_CHECKPOINT 0 0 ATT " << active_transaction_table.size();
    for (const auto& entry : active_transaction_table) {
        record << " " << entry.first
               << " " << txnStatusName(entry.second.status)
               << " " << entry.second.last_lsn;
    }
    record << " DPT " << dirty_page_table.size();
    for (const auto& entry : dirty_page_table) {
        record << " " << entry.first << " " << entry.second;
    }

    LSN end_lsn = log_manager.append(record.str());
    forceLogUpTo(end_lsn);
    log_manager.writeMasterRecord(active_checkpoint_begin_lsn);
    checkpoints_written++;
    checkpoint_analysis_start_lsn = active_checkpoint_begin_lsn;
    checkpoint_active = false;
    std::cout << "  log: END_CHECKPOINT LSN " << end_lsn
              << " saved ATT=" << active_transaction_table.size()
              << " DPT=" << dirty_page_table.size() << std::endl;
    std::cout << "  master: checkpoint starts at LSN "
              << active_checkpoint_begin_lsn
              << " offset "
              << log_manager.getLogOffset(active_checkpoint_begin_lsn)
              << std::endl;
    std::cout << "  recovery: fuzzy checkpoint forced log only; data pages remain no-force" << std::endl;
    active_checkpoint_begin_lsn = 0;
}

void RecoveryManager::checkpoint() {
    beginCheckpoint();
    endCheckpoint();
}

void RecoveryManager::recover() {
    struct WalRecord {
        LSN lsn;
        LSN prev_lsn;
        int txn_id;
        TableId table_id;
        PageID page_id;
        size_t slot_id;
        bool is_clr = false;
        LSN undo_next_lsn = 0;
        std::unique_ptr<Tuple> before_tuple;
        std::unique_ptr<Tuple> after_tuple;
    };

    std::map<int, TxnTableEntry> analysis_table;
    std::map<PageID, LSN> restart_dirty_page_table;
    std::map<LSN, LSN> prev_lsn_by_lsn;
    std::vector<WalRecord> wal_records;
    MasterRecord master_record = log_manager.readMasterRecord();
    LSN master_checkpoint_begin_lsn = master_record.checkpoint_begin_lsn;
    LSN analysis_start_lsn = master_checkpoint_begin_lsn == 0 ? 1 : master_checkpoint_begin_lsn;
    auto log_records = log_manager.readFromOffset(master_record.checkpoint_begin_offset);
    LSN checkpoint_end_lsn = 0;
    std::map<int, TxnTableEntry> checkpoint_table;
    std::map<PageID, LSN> checkpoint_dirty_page_table;

    auto readCheckpoint = [&](std::istringstream& input) {
        std::string marker;
        size_t entry_count = 0;
        checkpoint_table.clear();
        checkpoint_dirty_page_table.clear();
        if (!(input >> marker >> entry_count) || marker != "ATT") {
            throw std::runtime_error("Malformed END_CHECKPOINT active transaction table.");
        }
        for (size_t i = 0; i < entry_count; i++) {
            int txn_id;
            std::string status;
            LSN last_lsn;
            input >> txn_id >> status >> last_lsn;
            checkpoint_table[txn_id] = {parseTxnStatusName(status), last_lsn};
        }

        if (!(input >> marker >> entry_count) || marker != "DPT") {
            throw std::runtime_error("Malformed END_CHECKPOINT dirty page table.");
        }
        for (size_t i = 0; i < entry_count; i++) {
            int page_id;
            LSN rec_lsn;
            input >> page_id >> rec_lsn;
            checkpoint_dirty_page_table[static_cast<PageID>(page_id)] = rec_lsn;
        }
    };

    auto collectWalRecord = [&](const std::string& record) {
        std::istringstream input(record);
        LSN record_lsn;
        LSN prev_lsn;
        std::string type;
        int txn_id;
        input >> record_lsn >> type >> txn_id;
        if (!(input >> prev_lsn)) {
            throw std::runtime_error(
                "buzzdb.log was written by an older WAL format; remove it before running v63."
            );
        }
        if (txn_id > 0) {
            next_txn_id = std::max(next_txn_id, txn_id + 1);
        }
        prev_lsn_by_lsn[record_lsn] = prev_lsn;
        if (type == "UPDATE") {
            int table_id;
            int page_id;
            size_t slot_id;
            input >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                record_lsn,
                prev_lsn,
                txn_id,
                static_cast<TableId>(table_id),
                static_cast<PageID>(page_id),
                slot_id,
                false,
                0,
                Tuple::deserialize(input),
                Tuple::deserialize(input)
            });
        } else if (type == "CLR") {
            LSN undo_next_lsn;
            int table_id;
            int page_id;
            size_t slot_id;
            input >> undo_next_lsn >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                record_lsn,
                prev_lsn,
                txn_id,
                static_cast<TableId>(table_id),
                static_cast<PageID>(page_id),
                slot_id,
                true,
                undo_next_lsn,
                nullptr,
                Tuple::deserialize(input)
            });
        }
    };

    if (master_checkpoint_begin_lsn != 0) {
        if (log_records.empty()) {
            throw std::runtime_error("Master record points past the end of buzzdb.log.");
        }
        std::istringstream input(log_records.front());
        LSN record_lsn;
        std::string type;
        input >> record_lsn >> type;
        if (record_lsn != master_checkpoint_begin_lsn || type != "BEGIN_CHECKPOINT") {
            throw std::runtime_error("Master record does not point at BEGIN_CHECKPOINT.");
        }
    }

    checkpoint_analysis_start_lsn = master_checkpoint_begin_lsn;
    restart_analysis_records_total = log_records.size();
    restart_analysis_records_replayed = log_records.size();
    restart_analysis_bytes_skipped_by_checkpoint =
        master_record.checkpoint_begin_offset;

    for (size_t record_index = 0; record_index < log_records.size(); record_index++) {
        const auto& record = log_records[record_index];
        std::istringstream input(record);
        LSN record_lsn;
        LSN prev_lsn;
        std::string type;
        int txn_id;
        input >> record_lsn >> type >> txn_id;
        if (!(input >> prev_lsn)) {
            throw std::runtime_error(
                "buzzdb.log was written by an older WAL format; remove it before running v63."
            );
        }
        if (txn_id > 0) {
            next_txn_id = std::max(next_txn_id, txn_id + 1);
        }
        prev_lsn_by_lsn[record_lsn] = prev_lsn;
        bool in_analysis_scan = record_lsn >= analysis_start_lsn;
        if (type == "UPDATE" || type == "CLR") {
            collectWalRecord(record);
        }

        if (type == "BEGIN") {
            if (in_analysis_scan) {
                analysis_table[txn_id] = {TxnStatus::RUNNING, record_lsn};
            }
        } else if (type == "COMMIT") {
            if (in_analysis_scan) {
                analysis_table[txn_id] = {TxnStatus::COMMITTING, record_lsn};
            }
        } else if (type == "ABORT") {
            if (in_analysis_scan) {
                analysis_table[txn_id] = {TxnStatus::ABORTING, record_lsn};
            }
        } else if (type == "END") {
            if (in_analysis_scan) {
                analysis_table.erase(txn_id);
            }
        } else if (type == "UPDATE") {
            int table_id;
            int page_id;
            size_t slot_id;
            input >> table_id >> page_id >> slot_id;
            PageID dirty_page_id = static_cast<PageID>(page_id);
            if (in_analysis_scan) {
                if (restart_dirty_page_table.find(dirty_page_id) == restart_dirty_page_table.end()) {
                    restart_dirty_page_table[dirty_page_id] = record_lsn;
                }
                analysis_table[txn_id] = {TxnStatus::RUNNING, record_lsn};
            }
        } else if (type == "CLR") {
            LSN undo_next_lsn;
            int table_id;
            int page_id;
            size_t slot_id;
            input >> undo_next_lsn >> table_id >> page_id >> slot_id;
            PageID dirty_page_id = static_cast<PageID>(page_id);
            if (in_analysis_scan) {
                if (restart_dirty_page_table.find(dirty_page_id) == restart_dirty_page_table.end()) {
                    restart_dirty_page_table[dirty_page_id] = record_lsn;
                }
                auto status = TxnStatus::RUNNING;
                auto txn_entry = analysis_table.find(txn_id);
                if (txn_entry != analysis_table.end()) {
                    status = txn_entry->second.status;
                }
                analysis_table[txn_id] = {status, record_lsn};
            }
        } else if (type == "END_CHECKPOINT" && in_analysis_scan) {
            readCheckpoint(input);
            for (const auto& entry : checkpoint_table) {
                auto current = analysis_table.find(entry.first);
                if (current == analysis_table.end() ||
                    current->second.last_lsn < entry.second.last_lsn) {
                    analysis_table[entry.first] = entry.second;
                }
            }
            for (const auto& entry : checkpoint_dirty_page_table) {
                auto current = restart_dirty_page_table.find(entry.first);
                if (current == restart_dirty_page_table.end() ||
                    entry.second < current->second) {
                    restart_dirty_page_table[entry.first] = entry.second;
                }
            }
            checkpoint_end_lsn = record_lsn;
        }
    }

    if (master_checkpoint_begin_lsn != 0 && checkpoint_end_lsn == 0) {
        throw std::runtime_error("Master record checkpoint has no END_CHECKPOINT record.");
    }

    if (!log_records.empty()) {
        if (master_checkpoint_begin_lsn != 0) {
            std::cout << "ARIES analysis: master record starts at LSN "
                      << master_checkpoint_begin_lsn
                      << " (log offset "
                      << master_record.checkpoint_begin_offset << ")" << std::endl;
            std::cout << "ARIES analysis: loaded checkpoint ending at LSN "
                      << checkpoint_end_lsn
                      << " with ATT=" << checkpoint_table.size()
                      << " DPT=" << checkpoint_dirty_page_table.size()
                      << std::endl;
        } else {
            std::cout << "ARIES analysis: no checkpoint found; scanning from log start" << std::endl;
        }
        std::cout << "ARIES analysis: active transaction table after checkpointed log scan" << std::endl;
        if (analysis_table.empty()) {
            std::cout << "  empty; every logged transaction reached END" << std::endl;
        } else {
            for (const auto& entry : analysis_table) {
                std::cout << "  txn " << entry.first
                          << " " << txnStatusName(entry.second.status)
                          << " lastLSN " << entry.second.last_lsn << std::endl;
            }
        }

        std::cout << "ARIES analysis: dirty page table" << std::endl;
        if (restart_dirty_page_table.empty()) {
            std::cout << "  empty; no dirty pages need redo" << std::endl;
        } else {
            for (const auto& entry : restart_dirty_page_table) {
                std::cout << "  page " << entry.first
                          << " recLSN " << entry.second << std::endl;
            }
        }
    }

    size_t redone = 0;
    LSN redo_start_lsn = 0;
    for (const auto& entry : restart_dirty_page_table) {
        if (redo_start_lsn == 0 || entry.second < redo_start_lsn) {
            redo_start_lsn = entry.second;
        }
    }
    if (redo_start_lsn != 0) {
        std::cout << "ARIES redo: start from recLSN "
                  << redo_start_lsn << std::endl;
    }
    if (redo_start_lsn != 0 && redo_start_lsn < analysis_start_lsn) {
        wal_records.clear();
        prev_lsn_by_lsn.clear();
        auto redo_log_records = log_manager.readFromOffset(0);
        for (const auto& record : redo_log_records) {
            collectWalRecord(record);
        }
    }

    for (const auto& record : wal_records) {
        if (redo_start_lsn != 0 && record.lsn < redo_start_lsn) {
            restart_redo_records_skipped_by_dpt++;
            continue;
        }
        restart_redo_records_examined++;
        auto dirty_page = restart_dirty_page_table.find(record.page_id);
        if (dirty_page == restart_dirty_page_table.end() ||
            record.lsn < dirty_page->second) {
            restart_redo_records_skipped_by_dpt++;
            continue;
        }
        auto& metadata = catalog.getTable(record.table_id);
        TableHeap table(metadata, buffer_manager);
        auto& page = table.getPage(record.page_id);
        if (page->getPageLSN() >= record.lsn) {
            restart_redo_records_skipped_by_page_lsn++;
            continue;
        }
        table.applyPhysiologicalUpdate(
            record.page_id, record.slot_id, record.after_tuple->clone(),
            true, "restart redo", record.lsn
        );
        redone++;
    }

    size_t undone = 0;
    std::map<int, TxnTableEntry> losers;
    std::map<LSN, const WalRecord*> update_by_lsn;
    for (const auto& record : wal_records) {
        update_by_lsn[record.lsn] = &record;
    }
    std::map<LSN, int> to_undo;
    for (const auto& txn_entry : analysis_table) {
        if (txn_entry.second.status == TxnStatus::COMMITTING) {
            continue;
        }
        losers[txn_entry.first] = txn_entry.second;
        if (txn_entry.second.last_lsn != 0) {
            to_undo[txn_entry.second.last_lsn] = txn_entry.first;
        }
    }

    while (!to_undo.empty()) {
        auto undo_entry = std::prev(to_undo.end());
        LSN next_lsn = undo_entry->first;
        int txn_id = undo_entry->second;
        to_undo.erase(undo_entry);
        auto loser = losers.find(txn_id);
        if (loser == losers.end()) {
            continue;
        }

        auto record = update_by_lsn.find(next_lsn);
        if (record != update_by_lsn.end()) {
            if (record->second->is_clr) {
                if (record->second->undo_next_lsn != 0) {
                    to_undo[record->second->undo_next_lsn] = txn_id;
                }
                continue;
            }
            auto& metadata = catalog.getTable(record->second->table_id);
            TableHeap table(metadata, buffer_manager);
            LSN clr_lsn = appendClrRecord(
                txn_id,
                loser->second.last_lsn,
                record->second->prev_lsn,
                record->second->table_id,
                record->second->page_id,
                record->second->slot_id,
                *record->second->before_tuple
            );
            std::cout << "  log: CLR txn " << txn_id
                      << " LSN " << clr_lsn
                      << " prevLSN " << loser->second.last_lsn
                      << " undoNextLSN " << record->second->prev_lsn
                      << " during restart" << std::endl;
            loser->second.last_lsn = clr_lsn;
            table.applyPhysiologicalUpdate(
                record->second->page_id,
                record->second->slot_id,
                record->second->before_tuple->clone(),
                true, "restart undo", clr_lsn
            );
            undone++;
            if (record->second->prev_lsn != 0) {
                to_undo[record->second->prev_lsn] = txn_id;
            }
            continue;
        }

        auto prev_lsn = prev_lsn_by_lsn.find(next_lsn);
        if (prev_lsn != prev_lsn_by_lsn.end() && prev_lsn->second != 0) {
            to_undo[prev_lsn->second] = txn_id;
        }
    }

    for (const auto& loser : losers) {
        LSN last_lsn = loser.second.last_lsn;
        if (loser.second.status != TxnStatus::ABORTING) {
            last_lsn = appendTxnRecord(loser.first, "ABORT", last_lsn);
            forceLogUpTo(last_lsn);
            abort_log_records++;
            std::cout << "  log: ABORT txn " << loser.first
                      << " LSN " << last_lsn
                      << " prevLSN " << loser.second.last_lsn
                      << " during restart" << std::endl;
        }
        LSN end_lsn = appendTxnRecord(loser.first, "END", last_lsn);
        forceLogUpTo(end_lsn);
        std::cout << "  log: END txn " << loser.first
                  << " LSN " << end_lsn
                  << " prevLSN " << last_lsn
                  << " during restart" << std::endl;
    }

    for (const auto& entry : analysis_table) {
        if (entry.second.status != TxnStatus::COMMITTING) {
            continue;
        }
        LSN end_lsn = appendTxnRecord(entry.first, "END", entry.second.last_lsn);
        forceLogUpTo(end_lsn);
        std::cout << "  log: END txn " << entry.first
                  << " LSN " << end_lsn
                  << " prevLSN " << entry.second.last_lsn
                  << " during restart" << std::endl;
    }

    restart_redo_records += redone;
    restart_undo_records += undone;
    dirty_page_table.clear();
    if (redone != 0 || undone != 0) {
        std::cout << "Restart recovery: redid " << redone
                  << " history record(s), undid " << undone
                  << " loser update record(s)." << std::endl;
    }
}

LSN RecoveryManager::logUpdate(TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> before_tuple,
                                std::unique_ptr<Tuple> after_tuple) {
    if (!txn_active) {
        throw std::runtime_error("WAL recovery update requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    if (!txn_logged) {
        throw std::runtime_error("WAL recovery update requested without BEGIN log record.");
    }

    bool page_seen = false;
    for (PageID dirty_page : dirty_pages) {
        if (dirty_page == page_id) {
            page_seen = true;
            break;
        }
    }
    if (!page_seen) {
        dirty_pages.push_back(page_id);
    }

    auto before_image = before_tuple->serialize();
    auto after_image = after_tuple->serialize();
    before_image_bytes_logged += before_image.size();
    after_image_bytes_logged += after_image.size();
    before_image_records_logged++;
    after_image_records_logged++;
    LSN update_prev_lsn = current_txn_last_lsn;
    LSN update_lsn = log_manager.append(
        "UPDATE " + std::to_string(current_txn_id) + " " +
        std::to_string(update_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        before_image + after_image
    );
    current_txn_last_lsn = update_lsn;
    active_transaction_table[current_txn_id] = {TxnStatus::RUNNING, update_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = update_lsn;
    }
    std::cout << "  log: UPDATE txn " << current_txn_id
              << " page " << page_id
              << " slot " << slot_id
              << " LSN " << update_lsn
              << " prevLSN " << update_prev_lsn << std::endl;

    PageUpdateLogRecord update;
    update.lsn = update_lsn;
    update.prev_lsn = update_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.before_tuple = std::move(before_tuple);
    update.after_tuple = std::move(after_tuple);
    page_update_log_records.push_back(std::move(update));
    std::cout << "  recovery: update " << metadata.name
              << " page " << page_id
              << " slot " << slot_id
              << " in place; WAL logged before+after image at LSN "
              << update_lsn << std::endl;
    return update_lsn;
}

bool RecoveryManager::forceLogUpTo(LSN lsn) {
    log_force_requests++;
    bool wrote = log_manager.forceUpTo(lsn);
    if (wrote) {
        log_force_writes++;
    } else {
        log_force_skips++;
    }
    return wrote;
}

void RecoveryManager::notePageFlushed(PageID page_id,
                                      LSN page_lsn,
                                      const std::string& tag) {
    (void)tag;
    // A durable page flush makes this page's current recLSN unnecessary.
    auto dirty_page = dirty_page_table.find(page_id);
    if (dirty_page != dirty_page_table.end() && dirty_page->second <= page_lsn) {
        dirty_page_table.erase(dirty_page);
    }
}

void RecoveryManager::maybeCrashAfterSteal(PageID page_id) {
    if (!crash_after_steal_before_commit) {
        return;
    }

    buffer_manager.flushPage(page_id, "uncommitted flush");
    std::cout << "  recovery: stole dirty page " << page_id
              << " before COMMIT" << std::endl;
    std::cout << "  crash: after uncommitted page flush, before COMMIT log" << std::endl;
    std::exit(2);
}

void RecoveryManager::printPolicy() const {
    std::cout << "Recovery policy: Undo/Redo WAL + ARIES checkpoints/DPT/pageLSN + CLRs (Steal + No-Force)" << std::endl;
    std::cout << "  Steal: Yes; dirty uncommitted pages may reach disk." << std::endl;
    std::cout << "  Force: No; commit forces the log, not dirty data pages." << std::endl;
    std::cout << "  Stable storage: log and page flushes use fsync." << std::endl;
    std::cout << "  WAL: durable page flushes force the log through the page's LSN." << std::endl;
    std::cout << "  Checkpoint: fuzzy snapshots log ATT+DPT without forcing data pages." << std::endl;
    std::cout << "  Analysis: master record points to the latest checkpoint begin LSN." << std::endl;
    std::cout << "  Redo: repeat history from the smallest recLSN, using pageLSN skips." << std::endl;
    std::cout << "  Undo: loser transactions write CLRs and follow undoNextLSN." << std::endl;
}

void RecoveryManager::printSummary() {
    auto pagesForBytes = [](size_t bytes) {
        return bytes == 0 ? 0 : (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    };

    std::cout << "Recovery policy summary:" << std::endl;
    std::cout << "  Policy: Undo/Redo WAL + ARIES checkpoints/DPT/pageLSN + CLRs (Steal + No-Force)" << std::endl;
    std::cout << "  Log pages written this run: " << pagesForBytes(log_manager.getBytesWritten()) << std::endl;
    std::cout << "  Log pages on disk: " << pagesForBytes(log_manager.getBytesOnDisk()) << std::endl;
    std::cout << "  Log force requests: " << log_force_requests << std::endl;
    std::cout << "  Log force writes: " << log_force_writes << std::endl;
    std::cout << "  Log force skips: " << log_force_skips << std::endl;
    std::cout << "  Durable log fsyncs: " << log_manager.getStableLogForces() << std::endl;
    std::cout << "  Durable master-record fsyncs: "
              << log_manager.getStableMasterForces() << std::endl;
    std::cout << "  Checkpoints written this run: " << checkpoints_written << std::endl;
    if (checkpoint_analysis_start_lsn != 0) {
        std::cout << "  Master checkpoint BEGIN LSN: "
                  << checkpoint_analysis_start_lsn << std::endl;
    }
    std::cout << "  Before-image records logged: " << before_image_records_logged << std::endl;
    std::cout << "  After-image records logged: " << after_image_records_logged << std::endl;
    std::cout << "  CLR records logged: " << clr_records_logged << std::endl;
    std::cout << "  Runtime undo records applied: " << runtime_undo_records << std::endl;
    std::cout << "  Partial rollback undo records applied: " << partial_rollback_records << std::endl;
    std::cout << "  Restart analysis log records read: " << restart_analysis_records_total << std::endl;
    std::cout << "  Restart analysis log records replayed: " << restart_analysis_records_replayed << std::endl;
    std::cout << "  Restart analysis log bytes skipped by checkpoint seek: "
              << restart_analysis_bytes_skipped_by_checkpoint << std::endl;
    std::cout << "  Restart undo records applied: " << restart_undo_records << std::endl;
    std::cout << "  Restart redo records examined: " << restart_redo_records_examined << std::endl;
    std::cout << "  Restart redo skipped by DPT/recLSN: " << restart_redo_records_skipped_by_dpt << std::endl;
    std::cout << "  Restart redo skipped by pageLSN: " << restart_redo_records_skipped_by_page_lsn << std::endl;
    std::cout << "  Restart redo records applied: " << restart_redo_records << std::endl;
    std::cout << "  Data pages forced at commit: " << data_pages_forced_at_commit << std::endl;
}

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
        currentTuple.reset(); // Ensure currentTuple is reset
    }

    bool next() override {
        loadNextTuple();
        return currentTuple != nullptr;
    }

    void close() override {
        currentPageIndex = 0;
        currentSlotIndex = 0;
        currentTuple.reset();
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        if (currentTuple) {
            return std::move(currentTuple->fields);
        }
        return {}; // Return an empty vector if no tuple is available
    }

private:
    void loadNextTuple() {
        const auto& page_ids = tableHeap.getPageIds();
        while (currentPageIndex < page_ids.size()) {
            auto& currentPage = tableHeap.getPage(page_ids[currentPageIndex]);
            if (!currentPage || currentSlotIndex >= MAX_SLOTS) {
                currentSlotIndex = 0; // Reset slot index when moving to a new page
            }

            char* page_buffer = currentPage->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);

            while (currentSlotIndex < MAX_SLOTS) {
                if (!slot_array[currentSlotIndex].empty) {
                    assert(slot_array[currentSlotIndex].offset != INVALID_VALUE);
                    const char* tuple_data = page_buffer + slot_array[currentSlotIndex].offset;
                    std::istringstream iss(std::string(tuple_data, slot_array[currentSlotIndex].length));
                    currentTuple = Tuple::deserialize(iss);
                    currentSlotIndex++; // Move to the next slot for the next call
                    tuple_count++;
                    return; // Tuple loaded successfully
                }
                currentSlotIndex++;
            }

            // Increment page index after exhausting current page
            currentPageIndex++;
        }

        // No more tuples are available
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

class ProjectionOperator : public UnaryOperator {
private:
    std::vector<size_t> projected_attrs;
    std::vector<std::unique_ptr<Field>> currentOutput;
    bool has_next = false;

public:
    ProjectionOperator(Operator& input, std::vector<size_t> projected_attrs)
        : UnaryOperator(input), projected_attrs(std::move(projected_attrs)) {}

    void open() override {
        input->setTxnContext(txn_);
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
        std::vector<std::unique_ptr<Field>> outputCopy;
        if (!has_next) {
            return outputCopy;
        }
        for (const auto& field : currentOutput) {
            outputCopy.push_back(field->clone());
        }
        return outputCopy;
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
    > hashTable;
    std::vector<std::unique_ptr<Field>> currentLeftTuple;
    std::vector<std::unique_ptr<Field>> currentOutput;
    const std::vector<std::vector<std::unique_ptr<Field>>>* matchingRightTuples = nullptr;
    size_t matchingRightTupleIndex = 0;
    bool has_left_tuple = false;
    bool has_next = false;

public:
    HashJoinOperator(Operator& left, Operator& right,
                     size_t left_attr_index, size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->setTxnContext(txn_);
        input_right->setTxnContext(txn_);
        input_left->open();
        input_right->open();
        hashTable.clear();
        while (input_right->next()) {
            auto right_tuple = input_right->getOutput();
            if (right_attr_index >= right_tuple.size()) {
                throw std::runtime_error("Hash join right attribute index out of range.");
            }
            hashTable[HashJoinKey(*right_tuple[right_attr_index])].push_back(
                cloneFields(right_tuple)
            );
        }
        input_right->close();
        currentLeftTuple.clear();
        currentOutput.clear();
        matchingRightTuples = nullptr;
        matchingRightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    bool next() override {
        currentOutput.clear();
        has_next = false;
        while (true) {
            while (!has_left_tuple ||
                   matchingRightTuples == nullptr ||
                   matchingRightTupleIndex >= matchingRightTuples->size()) {
                if (!input_left->next()) {
                    return false;
                }

                currentLeftTuple = input_left->getOutput();
                if (left_attr_index >= currentLeftTuple.size()) {
                    throw std::runtime_error("Hash join left attribute index out of range.");
                }

                auto matches = hashTable.find(HashJoinKey(*currentLeftTuple[left_attr_index]));
                if (matches == hashTable.end()) {
                    has_left_tuple = false;
                    matchingRightTuples = nullptr;
                    matchingRightTupleIndex = 0;
                    continue;
                }

                matchingRightTuples = &matches->second;
                matchingRightTupleIndex = 0;
                has_left_tuple = true;
            }

            const auto& right_tuple = (*matchingRightTuples)[matchingRightTupleIndex++];
            currentOutput = cloneFields(currentLeftTuple);
            for (const auto& field : right_tuple) {
                currentOutput.push_back(field->clone());
            }
            has_next = true;
            return true;
        }
    }

    void close() override {
        input_left->close();
        hashTable.clear();
        currentLeftTuple.clear();
        currentOutput.clear();
        matchingRightTuples = nullptr;
        matchingRightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        std::vector<std::unique_ptr<Field>> outputCopy;
        if (!has_next) {
            return outputCopy;
        }
        for (const auto& field : currentOutput) {
            outputCopy.push_back(field->clone());
        }
        return outputCopy;
    }

private:
    static std::vector<std::unique_ptr<Field>> cloneFields(
        const std::vector<std::unique_ptr<Field>>& fields) {
        std::vector<std::unique_ptr<Field>> cloned;
        for (const auto& field : fields) {
            cloned.push_back(field->clone());
        }
        return cloned;
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
                // Simplified update logic for this toy engine.
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

struct ColumnRef {
    std::string tableName;
    std::string columnName;
    int attributeIndex = -1;
};

struct JoinClause {
    std::string tableName;
    ColumnRef left;
    ColumnRef right;
};

struct QueryComponents {
    std::string tableName;
    std::vector<ColumnRef> selectColumns;
    std::vector<JoinClause> joins;
    bool sumOperation = false;
    std::string sumAttributeName;
    int sumAttributeIndex = -1;
    bool groupBy = false;
    std::string groupByAttributeName;
    int groupByAttributeIndex = -1;
    bool whereCondition = false;
    std::string whereAttributeName;
    int whereAttributeIndex = -1;
    int lowerBound = std::numeric_limits<int>::min();
    int upperBound = std::numeric_limits<int>::max();
};

enum class StatementType {
    BEGIN,
    COMMIT,
    ABORT,
    SAVEPOINT,
    ROLLBACK_TO,
    CHECKPOINT,
    BEGIN_CHECKPOINT,
    END_CHECKPOINT,
    INSERT,
    UPDATE
};

struct StatementComponents {
    StatementType type = StatementType::INSERT;
    std::string tableName;
    std::vector<std::string> values;
    std::vector<std::pair<std::string, std::string>> assignments;
    std::string whereColumn;
    std::string whereValue;
    std::string savepointName;
    size_t lineNumber = 0;
};

bool isNumberToken(const std::string& token) {
    return !token.empty() &&
           std::all_of(token.begin(), token.end(), [](unsigned char c) {
               return std::isdigit(c);
           });
}

ColumnRef parseColumnRef(const std::string& table_name,
                         const std::string& column_token) {
    ColumnRef column;
    column.tableName = table_name;
    if (isNumberToken(column_token)) {
        column.attributeIndex = std::stoi(column_token) - 1;
    } else {
        column.columnName = column_token;
    }
    return column;
}

void parseAttributeToken(const std::string& token,
                         int& attribute_index,
                         std::string& attribute_name) {
    if (isNumberToken(token)) {
        attribute_index = std::stoi(token) - 1;
        attribute_name.clear();
    } else {
        attribute_index = -1;
        attribute_name = token;
    }
}

QueryComponents parseQuery(const std::string& query) {
    QueryComponents components;
    if (!std::regex_search(query, std::regex("^\\s*PROJECT\\s+"))) {
        throw std::runtime_error("Query must start with PROJECT.");
    }

    // Parse the query target: PROJECT {*} FROM table_name, or PROJECT SUM{column}.
    std::regex tableRegex(
        "^\\s*PROJECT\\s+(?:\\{\\*\\}|SUM\\{([A-Za-z_][A-Za-z0-9_]*|\\d+)\\})\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)");
    std::smatch tableMatches;
    if (std::regex_search(query, tableMatches, tableRegex)) {
        if (tableMatches[1].matched) {
            components.sumOperation = true;
            parseAttributeToken(
                tableMatches[1],
                components.sumAttributeIndex,
                components.sumAttributeName
            );
        }
        components.tableName = tableMatches[2];
    }

    if (components.tableName.empty()) {
        std::regex fromRegex("\\bFROM\\s+([A-Za-z_][A-Za-z0-9_]*)");
        std::smatch fromMatches;
        if (std::regex_search(query, fromMatches, fromRegex)) {
            components.tableName = fromMatches[1];
        }
    }

    auto from_pos = query.find(" FROM ");
    if (from_pos != std::string::npos) {
        std::string select_part = query.substr(0, from_pos);
        std::regex columnRefRegex(
            "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
        std::smatch columnMatches;
        auto columnStart = select_part.cbegin();
        while (std::regex_search(columnStart, select_part.cend(), columnMatches, columnRefRegex)) {
            components.selectColumns.push_back(
                parseColumnRef(columnMatches[1], columnMatches[2])
            );
            columnStart = columnMatches.suffix().first;
        }
    }

    std::regex joinRegex(
        "JOIN\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+ON\\s+"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}\\s*=\\s*"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
    std::smatch joinMatches;
    auto joinStart = query.cbegin();
    while (std::regex_search(joinStart, query.cend(), joinMatches, joinRegex)) {
        components.joins.push_back({
            joinMatches[1],
            parseColumnRef(joinMatches[2], joinMatches[3]),
            parseColumnRef(joinMatches[4], joinMatches[5])
        });
        joinStart = joinMatches.suffix().first;
    }

    // Check for GROUP BY clause
    std::regex groupByRegex("GROUP BY \\{([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
    std::smatch groupByMatches;
    if (std::regex_search(query, groupByMatches, groupByRegex)) {
        components.groupBy = true;
        parseAttributeToken(
            groupByMatches[1],
            components.groupByAttributeIndex,
            components.groupByAttributeName
        );
    }

    // Extract WHERE conditions more accurately
    std::regex whereRegex(
        "\\{([A-Za-z_][A-Za-z0-9_]*|\\d+)\\} > (\\d+) and "
        "\\{([A-Za-z_][A-Za-z0-9_]*|\\d+)\\} < (\\d+)");
    std::smatch whereMatches;
    if (std::regex_search(query, whereMatches, whereRegex)) {
        components.whereCondition = true;
        // Correctly identify the attribute index for the WHERE condition
        parseAttributeToken(
            whereMatches[1],
            components.whereAttributeIndex,
            components.whereAttributeName
        );
        components.lowerBound = std::stoi(whereMatches[2]);
        // Ensure the same attribute is used for both conditions
        int upper_attribute_index = -1;
        std::string upper_attribute_name;
        parseAttributeToken(whereMatches[3], upper_attribute_index, upper_attribute_name);
        if ((components.whereAttributeIndex >= 0 &&
             upper_attribute_index == components.whereAttributeIndex) ||
            (!components.whereAttributeName.empty() &&
             upper_attribute_name == components.whereAttributeName)) {
            components.upperBound = std::stoi(whereMatches[4]);
        } else {
            std::cerr << "Error: WHERE clause conditions apply to different attributes." << std::endl;
            // Handle error or set components.whereCondition = false;
        }
    }

    if (components.tableName.empty()) {
        throw std::runtime_error("Query must name a table.");
    }

    return components;
}

int findColumnIndex(const TableMetadata& metadata,
                    const std::string& column_name) {
    for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
        if (metadata.schema.columns[i].name == column_name) {
            return static_cast<int>(i);
        }
    }

    throw std::runtime_error(
        "Unknown column " + column_name + " in table " + metadata.name
    );
}

void resolveColumnRef(ColumnRef& column, Catalog& catalog) {
    auto& metadata = catalog.getTable(column.tableName);

    if (!column.columnName.empty()) {
        column.attributeIndex = findColumnIndex(metadata, column.columnName);
        return;
    }

    if (column.attributeIndex < 0 ||
        static_cast<size_t>(column.attributeIndex) >= metadata.schema.columns.size()) {
        throw std::runtime_error(
            "Column index out of range for table " + column.tableName
        );
    }

    column.columnName = metadata.schema.columns[column.attributeIndex].name;
}

void resolveBaseAttribute(int& attribute_index,
                          std::string& attribute_name,
                          const TableMetadata& metadata) {
    if (!attribute_name.empty()) {
        attribute_index = findColumnIndex(metadata, attribute_name);
        return;
    }

    if (attribute_index >= 0) {
        if (static_cast<size_t>(attribute_index) >= metadata.schema.columns.size()) {
            throw std::runtime_error(
                "Column index out of range for table " + metadata.name
            );
        }
        attribute_name = metadata.schema.columns[attribute_index].name;
    }
}

void resolveQueryColumns(QueryComponents& components, Catalog& catalog) {
    auto& base_metadata = catalog.getTable(components.tableName);

    for (auto& column : components.selectColumns) {
        resolveColumnRef(column, catalog);
    }

    for (auto& join : components.joins) {
        resolveColumnRef(join.left, catalog);
        resolveColumnRef(join.right, catalog);
    }

    resolveBaseAttribute(
        components.sumAttributeIndex,
        components.sumAttributeName,
        base_metadata
    );
    resolveBaseAttribute(
        components.groupByAttributeIndex,
        components.groupByAttributeName,
        base_metadata
    );
    resolveBaseAttribute(
        components.whereAttributeIndex,
        components.whereAttributeName,
        base_metadata
    );
}

void addColumnIfMissing(std::vector<int>& columns, int column) {
    if (column < 0) {
        return;
    }

    for (auto existing : columns) {
        if (existing == column) {
            return;
        }
    }
    columns.push_back(column);
}

std::vector<int> deriveQueryColumns(const QueryComponents& components,
                                    const TableMetadata& metadata) {
    std::vector<int> columns;

    if (!components.sumOperation && !components.groupBy) {
        for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
            columns.push_back(static_cast<int>(i));
        }
        return columns;
    }

    addColumnIfMissing(columns, components.groupByAttributeIndex);
    addColumnIfMissing(columns, components.sumAttributeIndex);
    addColumnIfMissing(columns, components.whereAttributeIndex);
    return columns;
}

std::string operatorTreeString(const QueryComponents& components) {
    std::string tree = "Scan(" + components.tableName + ")";
    for (const auto& join : components.joins) {
        tree = "HashJoin(" + tree + ", Scan(" + join.tableName + "))";
    }
    if (components.whereCondition) {
        tree = "Select(" + tree + ")";
    }
    if (components.sumOperation || components.groupBy) {
        tree = "HashAggregate(" + tree + ")";
    }
    return "Project(" + tree + ")";
}

std::string attributeLabel(const std::string& attribute_name,
                           int attribute_index) {
    if (!attribute_name.empty()) {
        return attribute_name;
    }

    return std::to_string(attribute_index + 1);
}

std::string columnLabel(const ColumnRef& column) {
    return column.tableName + "." +
           attributeLabel(column.columnName, column.attributeIndex);
}

template <typename Components>
void prettyPrint(const Components& components,
                 const std::vector<int>& queryColumns = {}) {
    if constexpr (std::is_same<Components, QueryComponents>::value) {
        std::cout << "Query Components for table " << components.tableName << ":\n";
        std::cout << "  Columns: ";
        if (!components.selectColumns.empty()) {
            for (const auto& column : components.selectColumns) {
                std::cout << "{" << columnLabel(column) << "} ";
            }
        } else {
            for (auto attr : queryColumns) {
                std::cout << "{" << attr + 1 << "} "; // Convert back to 1-based indexing for display
            }
        }
        std::cout << "\n  SUM Operation: " << (components.sumOperation ? "Yes" : "No");
        if (components.sumOperation) {
            std::cout << " on {"
                      << attributeLabel(
                             components.sumAttributeName,
                             components.sumAttributeIndex
                         )
                      << "}";
        }
        std::cout << "\n  GROUP BY: " << (components.groupBy ? "Yes" : "No");
        if (components.groupBy) {
            std::cout << " on {"
                      << attributeLabel(
                             components.groupByAttributeName,
                             components.groupByAttributeIndex
                         )
                      << "}";
        }
        std::cout << "\n  WHERE Condition: " << (components.whereCondition ? "Yes" : "No");
        if (components.whereCondition) {
            std::cout << " on {"
                      << attributeLabel(
                             components.whereAttributeName,
                             components.whereAttributeIndex
                         )
                      << "} > " << components.lowerBound << " and < " << components.upperBound;
        }
        if (!components.joins.empty()) {
            std::cout << "\n  JOIN(s): ";
            for (const auto& join : components.joins) {
                std::cout << join.tableName << " ON {"
                          << columnLabel(join.left)
                          << "} = {" << columnLabel(join.right) << "} ";
            }
        }
    } else if constexpr (std::is_same<Components, StatementComponents>::value) {
        (void)queryColumns;
        if (components.type == StatementType::BEGIN ||
            components.type == StatementType::COMMIT ||
            components.type == StatementType::ABORT) {
            return;
        } else if (components.type == StatementType::SAVEPOINT) {
            std::cout << "SAVEPOINT " << components.savepointName;
        } else if (components.type == StatementType::ROLLBACK_TO) {
            std::cout << "ROLLBACK TO " << components.savepointName;
        } else if (components.type == StatementType::CHECKPOINT) {
            std::cout << "CHECKPOINT";
        } else if (components.type == StatementType::BEGIN_CHECKPOINT) {
            std::cout << "BEGIN CHECKPOINT";
        } else if (components.type == StatementType::END_CHECKPOINT) {
            std::cout << "END CHECKPOINT";
        } else if (components.type == StatementType::INSERT) {
            std::cout << "INSERT " << components.tableName << " ";
            for (size_t i = 0; i < components.values.size(); i++) {
                if (i != 0) std::cout << "|";
                std::cout << components.values[i];
            }
        } else {
            std::cout << "UPDATE " << components.tableName << " SET ";
            for (const auto& assignment : components.assignments) {
                std::cout << assignment.first << "=" << assignment.second << " ";
            }
            std::cout << "WHERE "
                      << components.whereColumn << "=" << components.whereValue;
        }
    }
    std::cout << std::endl;
}

void executeQuery(const QueryComponents& components,
                  Catalog& catalog,
                  BufferManager& buffer_manager,
                  const TxnPtr& txn = nullptr,
                  bool print_tuples = true) {
    std::map<std::string, size_t> table_offsets;
    std::map<std::string, size_t> table_widths;
    std::vector<std::unique_ptr<TableHeap>> heaps;
    std::vector<std::unique_ptr<ScanOperator>> scans;
    std::vector<std::unique_ptr<HashJoinOperator>> joinOpBuffers;

    auto addScan = [&](const std::string& table_name) -> ScanOperator& {
        auto& metadata = catalog.getTable(table_name);
        table_widths[table_name] = metadata.schema.columns.size();
        heaps.push_back(std::make_unique<TableHeap>(metadata, buffer_manager));
        scans.push_back(std::make_unique<ScanOperator>(*heaps.back()));
        return *scans.back();
    };

    Operator* rootOp = &addScan(components.tableName);
    table_offsets[components.tableName] = 0;
    size_t output_width = table_widths[components.tableName];

    for (const auto& join : components.joins) {
        auto& right_scan = addScan(join.tableName);
        size_t left_attr_index;
        size_t right_attr_index;
        if (table_offsets.find(join.left.tableName) != table_offsets.end() &&
            join.right.tableName == join.tableName) {
            left_attr_index = table_offsets[join.left.tableName] + join.left.attributeIndex;
            right_attr_index = join.right.attributeIndex;
        } else if (table_offsets.find(join.right.tableName) != table_offsets.end() &&
                   join.left.tableName == join.tableName) {
            left_attr_index = table_offsets[join.right.tableName] + join.right.attributeIndex;
            right_attr_index = join.left.attributeIndex;
        } else {
            throw std::runtime_error("JOIN must connect a new table to an earlier table.");
        }
        joinOpBuffers.push_back(std::make_unique<HashJoinOperator>(
            *rootOp, right_scan, left_attr_index, right_attr_index));
        rootOp = joinOpBuffers.back().get();
        table_offsets[join.tableName] = output_width;
        output_width += table_widths[join.tableName];
    }

    // Buffer for optional operators to ensure lifetime
    std::optional<SelectOperator> selectOpBuffer;
    std::optional<HashAggregationOperator> hashAggOpBuffer;
    std::optional<ProjectionOperator> projectionOpBuffer;
    std::vector<size_t> projected_columns;

    for (const auto& column : components.selectColumns) {
        auto offset_it = table_offsets.find(column.tableName);
        auto width_it = table_widths.find(column.tableName);
        if (offset_it == table_offsets.end() ||
            width_it == table_widths.end() ||
            column.attributeIndex < 0 ||
            static_cast<size_t>(column.attributeIndex) >= width_it->second) {
            throw std::runtime_error("Projected column is not in the query output.");
        }
        projected_columns.push_back(
            offset_it->second + static_cast<size_t>(column.attributeIndex)
        );
    }

    if (projected_columns.empty() && !components.sumOperation && !components.groupBy) {
        for (size_t attr_index = 0; attr_index < output_width; attr_index++) {
            projected_columns.push_back(attr_index);
        }
    }

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
        if (projected_columns.empty()) {
            for (size_t attr_index = 0; attr_index < groupByAttrs.size() + aggrFuncs.size(); attr_index++) {
                projected_columns.push_back(attr_index);
            }
        }
    }

    if (!projected_columns.empty()) {
        projectionOpBuffer.emplace(*rootOp, projected_columns);
        rootOp = &*projectionOpBuffer;
    }

    // Execute the Root Operator
    rootOp->setTxnContext(txn);
    rootOp->open();
    size_t output_tuple_count = 0;
    while (rootOp->next()) {
        output_tuple_count++;
        if (print_tuples) {
            const auto& output = rootOp->getOutput();
            for (const auto& field : output) {
                field->print();
                std::cout << " ";
            }
            std::cout << std::endl;
        }
    }
    if (!print_tuples) {
        std::cout << "Output tuple_count: " << output_tuple_count << std::endl;
    }
    rootOp->close();
}

class InsertOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<Tuple> tupleToInsert;

public:
    InsertOperator(TableHeap& tableHeap) : tableHeap(tableHeap) {}

    // Set the tuple to be inserted by this operator.
    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {
        // Not used in this context
    }

    bool next() override {
        if (!tupleToInsert) return false; // No tuple to insert

        for (PageID pageId : tableHeap.getPageIds()) {
            auto& page = tableHeap.getPage(pageId);
            // Attempt to insert the tuple
            if (page->addTuple(tupleToInsert->clone())) {
                // Flush the page to disk after insertion
                tableHeap.flushInsertedPage(pageId);
                tableHeap.recordInsertedTuple();
                return true; // Insertion successful
            }
        }

        // If insertion failed in all table pages, allocate one and try again.
        PageID pageId = tableHeap.allocatePage();
        auto& newPage = tableHeap.getPage(pageId);
        if (newPage->addTuple(std::move(tupleToInsert))) {
            tableHeap.flushInsertedPage(pageId);
            tableHeap.recordInsertedTuple();
            return true; // Insertion successful after extending the database
        }

        return false; // Insertion failed even after extending the database
    }

    void close() override {
        // Not used in this context
    }

    std::vector<std::unique_ptr<Field>> getOutput() override {
        return {}; // Return empty vector
    }
};

class UpdateOperator : public Operator {
private:
    TableHeap& tableHeap;
    size_t whereColumn;
    Field whereValue;
    std::vector<std::pair<size_t, Field>> assignments;
    RecoveryManager* recoveryManager = nullptr;
    bool executed = false;
    size_t updatedCount = 0;

public:
    UpdateOperator(TableHeap& tableHeap,
                   size_t whereColumn,
                   const Field& whereValue,
                   std::vector<std::pair<size_t, Field>> assignments,
                   RecoveryManager* recoveryManager = nullptr)
        : tableHeap(tableHeap),
          whereColumn(whereColumn),
          whereValue(whereValue),
          assignments(std::move(assignments)),
          recoveryManager(recoveryManager) {}

    void open() override {
        executed = false;
        updatedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        updatedCount = tableHeap.updateTuples(
            whereColumn, whereValue, assignments, recoveryManager
        );
        executed = true;
        return updatedCount > 0;
    }

    void close() override {}

    std::vector<std::unique_ptr<Field>> getOutput() override {
        return {};
    }
};

class DeleteOperator : public Operator {
private:
    TableHeap& tableHeap;
    size_t whereColumn;
    Field whereValue;
    bool executed = false;
    size_t deletedCount = 0;

public:
    DeleteOperator(TableHeap& tableHeap,
                   size_t whereColumn,
                   const Field& whereValue)
        : tableHeap(tableHeap),
          whereColumn(whereColumn),
          whereValue(whereValue) {}

    void open() override {
        executed = false;
        deletedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        deletedCount = tableHeap.deleteTuples(whereColumn, whereValue);
        executed = true;
        return deletedCount > 0;
    }

    void close() override {}

    std::vector<std::unique_ptr<Field>> getOutput() override {
        return {};
    }
};


class BuzzDB {
public:
    HashIndex hash_index;
    BufferManager buffer_manager;
    Catalog catalog;
    RecoveryManager recovery_manager;
    TransactionManager txn_manager;

    BuzzDB()
        : buffer_manager(),
          catalog(buffer_manager),
          recovery_manager(buffer_manager, catalog) {
        catalog.bootstrap();
        if (catalog.isNewDatabase()) {
            recovery_manager.resetLog();
        }
    }

    bool isNewDatabase() const { return catalog.isNewDatabase(); }

    std::vector<std::string> userTableNames() {
        return catalog.listUserTableNames();
    }

    void printBufferPoolSummary() const {
        buffer_manager.printBufferPoolSummary();
    }

    // NEW: helpers
    TxnPtr begin() { return txn_manager.begin(); }
    void commit(const TxnPtr& tx) { txn_manager.commit(*tx); }
    void abort (const TxnPtr& tx) { txn_manager.abort(*tx);  }

    CreateTableResult createTable(const std::string& name,
                                  std::initializer_list<ColumnSchema> columns) {
        TableSchema schema;
        for (const auto& column : columns) {
            schema.columns.push_back(column);
        }

        auto result = catalog.createTable(name, std::move(schema));
        std::cout << (result.created ? "Created table " : "Loaded table ")
                  << name << " with id " << result.table_id << "\n";
        return result;
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

    static std::string lineContext(size_t line_number) {
        if (line_number == 0) {
            return "";
        }

        return " at line " + std::to_string(line_number);
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
                lineContext(line_number)
            );
        }

        throw std::runtime_error("Unknown field type.");
    }

    static StatementComponents parseStatement(const std::string& statement,
                                              size_t line_number = 0) {
        std::regex begin_regex("^\\s*BEGIN\\s*$");
        std::regex commit_regex("^\\s*COMMIT\\s*$");
        std::regex abort_regex("^\\s*ABORT\\s*$");
        std::regex checkpoint_regex("^\\s*CHECKPOINT\\s*$");
        std::regex begin_checkpoint_regex("^\\s*BEGIN\\s+CHECKPOINT\\s*$");
        std::regex end_checkpoint_regex("^\\s*END\\s+CHECKPOINT\\s*$");
        std::regex savepoint_regex(
            "^\\s*SAVEPOINT\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*$");
        std::regex rollback_to_regex(
            "^\\s*ROLLBACK\\s+TO\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*$");
        std::regex insert_regex(
            "^\\s*INSERT\\s+([A-Za-z_][A-Za-z0-9_]*)\\|(.*)$");
        std::regex update_regex(
            "^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+(.+)\\s+"
            "WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$");
        std::smatch matches;

        StatementComponents components;
        components.lineNumber = line_number;
        if (std::regex_match(statement, begin_regex)) {
            components.type = StatementType::BEGIN;
            return components;
        }

        if (std::regex_match(statement, commit_regex)) {
            components.type = StatementType::COMMIT;
            return components;
        }

        if (std::regex_match(statement, abort_regex)) {
            components.type = StatementType::ABORT;
            return components;
        }

        if (std::regex_match(statement, checkpoint_regex)) {
            components.type = StatementType::CHECKPOINT;
            return components;
        }

        if (std::regex_match(statement, begin_checkpoint_regex)) {
            components.type = StatementType::BEGIN_CHECKPOINT;
            return components;
        }

        if (std::regex_match(statement, end_checkpoint_regex)) {
            components.type = StatementType::END_CHECKPOINT;
            return components;
        }

        if (std::regex_match(statement, matches, savepoint_regex)) {
            components.type = StatementType::SAVEPOINT;
            components.savepointName = matches[1];
            return components;
        }

        if (std::regex_match(statement, matches, rollback_to_regex)) {
            components.type = StatementType::ROLLBACK_TO;
            components.savepointName = matches[1];
            return components;
        }

        if (std::regex_match(statement, matches, insert_regex)) {
            components.type = StatementType::INSERT;
            components.tableName = matches[1];
            components.values = splitPipeLine(matches[2]);
            return components;
        }

        if (std::regex_match(statement, matches, update_regex)) {
            components.type = StatementType::UPDATE;
            components.tableName = matches[1];
            std::stringstream assignment_stream(matches[2]);
            std::string assignment;
            while (std::getline(assignment_stream, assignment, ',')) {
                auto separator = assignment.find('=');
                if (separator == std::string::npos) {
                    throw std::runtime_error("Malformed UPDATE assignment: " + assignment);
                }
                components.assignments.push_back({
                    trim(assignment.substr(0, separator)),
                    trim(assignment.substr(separator + 1))
                });
            }
            components.whereColumn = matches[3];
            components.whereValue = matches[4];
            return components;
        }

        throw std::runtime_error("Unsupported statement: " + statement);
    }

    // Data-changing statements are separate from read queries.
    void executeStatement(const std::string& statement,
                          const TxnPtr& txn = nullptr,
                          size_t line_number = 0,
                          bool print_statement = true,
                          bool persist_metadata = true,
                          TableHeap* bulk_table = nullptr) {
        auto components = parseStatement(statement, line_number);
        if (print_statement) {
            prettyPrint(components);
        }

        if (components.type == StatementType::BEGIN) {
            recovery_manager.begin();
            return;
        }
        if (components.type == StatementType::COMMIT) {
            recovery_manager.commit();
            return;
        }
        if (components.type == StatementType::ABORT) {
            recovery_manager.abort();
            return;
        }
        if (components.type == StatementType::CHECKPOINT) {
            recovery_manager.checkpoint();
            return;
        }
        if (components.type == StatementType::BEGIN_CHECKPOINT) {
            recovery_manager.beginCheckpoint();
            return;
        }
        if (components.type == StatementType::END_CHECKPOINT) {
            recovery_manager.endCheckpoint();
            return;
        }
        if (components.type == StatementType::SAVEPOINT) {
            recovery_manager.savepoint(components.savepointName);
            return;
        }
        if (components.type == StatementType::ROLLBACK_TO) {
            recovery_manager.rollbackTo(components.savepointName);
            return;
        }

        auto& metadata = catalog.getTable(components.tableName);
        if (bulk_table != nullptr && bulk_table->getTableId() != metadata.table_id) {
            throw std::runtime_error("Bulk insert table mismatch: " + components.tableName);
        }

        if (components.type == StatementType::UPDATE) {
            if (bulk_table != nullptr) {
                throw std::runtime_error("Bulk loading only supports INSERT.");
            }
            if (components.assignments.empty()) {
                throw std::runtime_error("UPDATE requires at least one assignment.");
            }

            std::vector<std::pair<size_t, Field>> assignments;
            for (const auto& assignment : components.assignments) {
                int column_index = findColumnIndex(metadata, assignment.first);
                FieldType expected_type = metadata.schema.columns[column_index].type;
                auto field = parseFieldValue(
                    expected_type, assignment.second,
                    components.tableName, components.lineNumber
                );
                assignments.push_back({static_cast<size_t>(column_index), field});
            }

            int where_column = findColumnIndex(metadata, components.whereColumn);
            auto where_value = parseFieldValue(
                metadata.schema.columns[where_column].type,
                components.whereValue,
                components.tableName,
                components.lineNumber
            );

            TableHeap table(metadata, buffer_manager);
            UpdateOperator updateOp(
                table,
                static_cast<size_t>(where_column),
                where_value,
                std::move(assignments),
                recovery_manager.isActive() ? &recovery_manager : nullptr
            );
            updateOp.setTxnContext(txn);
            updateOp.open();
            updateOp.next();
            updateOp.close();
            return;
        }

        if (components.values.size() != metadata.schema.columns.size()) {
            throw std::runtime_error(
                "Wrong field count for table " + components.tableName +
                lineContext(line_number)
            );
        }
        if (recovery_manager.isActive()) {
            throw std::runtime_error("INSERT inside an undo/redo WAL update transaction is not supported in v63.");
        }

        auto tuple = std::make_unique<Tuple>();

        for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
            FieldType expected_type = metadata.schema.columns[i].type;
            auto field = parseFieldValue(
                expected_type, components.values[i],
                components.tableName, components.lineNumber
            );
            tuple->addField(field.clone());
        }

        std::unique_ptr<TableHeap> local_table;
        TableHeap* table = bulk_table;
        if (table == nullptr) {
            local_table = std::make_unique<TableHeap>(metadata, buffer_manager);
            table = local_table.get();
        }

        InsertOperator insertOp(*table);
        insertOp.setTxnContext(txn);
        insertOp.setTupleToInsert(std::move(tuple));
        insertOp.open();
        bool status = insertOp.next();
        insertOp.close();
        assert(status == true);
        if (persist_metadata) {
            catalog.persistTableMetadata(metadata);
        }
    }

    void loadDataFile(const std::string& filename,
                      const TxnPtr& txn = nullptr,
                      bool print_statements = false) {
        std::ifstream inputFile(filename);

        if (!inputFile) {
            throw std::runtime_error("Unable to open file: " + filename);
        }

        std::string line;
        size_t line_number = 0;
        std::unordered_map<std::string, std::unique_ptr<TableHeap>> bulk_tables;
        std::vector<std::string> touched_tables;

        while (std::getline(inputFile, line)) {
            line_number++;

            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            auto separator = line.find('|');
            if (separator == std::string::npos) {
                throw std::runtime_error("Malformed data row" + lineContext(line_number));
            }

            std::string table_name = line.substr(0, separator);
            auto bulk_table = bulk_tables.find(table_name);
            if (bulk_table == bulk_tables.end()) {
                auto& metadata = catalog.getTable(table_name);
                auto result = bulk_tables.emplace(
                    table_name,
                    std::make_unique<TableHeap>(metadata, buffer_manager, false)
                );
                bulk_table = result.first;
                touched_tables.push_back(table_name);
            }

            executeStatement(
                "INSERT " + line,
                txn,
                line_number,
                print_statements,
                false,
                bulk_table->second.get()
            );
        }

        buffer_manager.flushAllPages("bulk load");

        for (const auto& table_name : touched_tables) {
            catalog.persistTableMetadata(catalog.getTable(table_name));
        }
    }

    void executeQuery(const std::string& query,
                      const TxnPtr& txn = nullptr,
                      bool print_tuples = true) {
        auto components = parseQuery(query);
        resolveQueryColumns(components, catalog);
        auto& metadata = catalog.getTable(components.tableName);
        auto queryColumns = deriveQueryColumns(components, metadata);
        (void)queryColumns;
        std::cout << "QUERY " << operatorTreeString(components) << std::endl;
        ::executeQuery(
            components,
            catalog,
            buffer_manager,
            txn,
            print_tuples
        );
    }

    void execute(const std::string& command,
                 const TxnPtr& txn = nullptr,
                 bool print_output = true) {
        std::stringstream command_stream(command);
        std::string command_part;
        while (std::getline(command_stream, command_part, ';')) {
            auto trimmed_command = trim(command_part);
            if (trimmed_command.empty()) {
                continue;
            }

            if (std::regex_search(trimmed_command, std::regex("^PROJECT\\s+"))) {
                executeQuery(trimmed_command, txn, print_output);
            } else {
                executeStatement(trimmed_command, txn, 0, print_output);
            }
        }
    }

    void executeQueries(const TxnPtr& txn = nullptr) {
        executeQuery(
            "PROJECT {seats.seat_no}, {seats.status}, {seats.customer} "
            "FROM seats",
            txn
        );

        executeQuery(
            "PROJECT {holds.customer}, {seats.seat_no}, {holds.status}, "
            "{flights.flight_no}, {flights.origin}, {flights.destination} "
            "FROM holds "
            "JOIN seats ON {holds.seat_id} = {seats.id} "
            "JOIN flights ON {holds.flight_id} = {flights.id}",
            txn
        );
    }
    
};

bool isBookingTableName(const std::string& table_name) {
    return table_name == "flights" ||
           table_name == "seats" ||
           table_name == "holds";
}

int main(int argc, char* argv[]) {

    BuzzDB db;
    std::string mode = argc > 1 && std::string(argv[1]).rfind("--", 0) == 0
        ? argv[1] : "";
    bool crash_after_uncommitted_flush =
        mode == "--crash-after-uncommitted-flush";
    bool crash_after_commit_record =
        mode == "--crash-after-commit-record";
    int data_arg = mode.empty() ? 1 : 2;
    std::string data_filename = argc > data_arg ? argv[data_arg] : "booking.txt";

    auto existing_tables = db.userTableNames();
    for (const auto& table_name : existing_tables) {
        if (!isBookingTableName(table_name)) {
            throw std::runtime_error(
                "This v63 booking workload cannot open a buzzdb.dat containing "
                "older tables such as '" + table_name + "'. "
                "Use a fresh directory or remove the old buzzdb.dat."
            );
        }
    }

    auto flights_table = db.createTable("flights", {
        {"id", INT},
        {"flight_no", STRING},
        {"origin", STRING},
        {"destination", STRING},
        {"flight_date", STRING}
    });

    auto seats_table = db.createTable("seats", {
        {"id", INT},
        {"flight_id", INT},
        {"seat_no", STRING},
        {"status", STRING},
        {"customer", STRING}
    });

    auto holds_table = db.createTable("holds", {
        {"id", INT},
        {"flight_id", INT},
        {"seat_id", INT},
        {"customer", STRING},
        {"status", STRING}
    });

    bool seed_booking_tables =
        flights_table.created || seats_table.created || holds_table.created;
    if (seed_booking_tables) {
        db.recovery_manager.resetLog();
    }
    db.recovery_manager.recover();

    std::string script;
    if (crash_after_uncommitted_flush || crash_after_commit_record) {
        if (!seed_booking_tables) {
            throw std::runtime_error(mode + " expects a fresh buzzdb.dat.");
        }

        db.loadDataFile(data_filename);
        if (crash_after_uncommitted_flush) {
            db.recovery_manager.simulateCrashAfterStealBeforeCommit();
        } else {
            db.recovery_manager.simulateCrashAfterCommitBeforeFlush();
        }
        script += R"(
BEGIN;
UPDATE seats SET status=sold, customer=alice WHERE seat_no=1A;
)";
        if (!crash_after_uncommitted_flush) {
            script += R"(
COMMIT;
)";
        }
        db.recovery_manager.printPolicy();
        db.execute(script);
        return 0;
    }

    if (seed_booking_tables) {
        db.loadDataFile(data_filename);
        script += R"(
BEGIN;
UPDATE seats SET status=held, customer=garcia_family WHERE seat_no=1A;
UPDATE seats SET status=held, customer=garcia_family WHERE seat_no=1B;
BEGIN CHECKPOINT;
SAVEPOINT garcia_two;
UPDATE seats SET status=held, customer=garcia_family WHERE seat_no=1C;
SAVEPOINT garcia_three;
UPDATE seats SET status=held, customer=garcia_family WHERE seat_no=1D;
END CHECKPOINT;
ROLLBACK TO garcia_three;
ROLLBACK TO garcia_two;
UPDATE seats SET status=held, customer=garcia_family WHERE seat_no=1C;
COMMIT;
BEGIN;
UPDATE seats SET status=held, customer=patel WHERE seat_no=1B;
ABORT;
BEGIN;
UPDATE seats SET status=held, customer=lee WHERE seat_no=2A;
COMMIT;
BEGIN;
UPDATE seats SET status=held, customer=chen WHERE seat_no=2A;
UPDATE seats SET status=held, customer=chen WHERE seat_no=2B;
ABORT;
CHECKPOINT;
BEGIN;
UPDATE seats SET status=held, customer=nguyen WHERE seat_no=2B;
PROJECT {seats.seat_no}, {seats.status}, {seats.customer} FROM seats;
COMMIT;
)";
    } else {
        script += R"(
BEGIN;
PROJECT {seats.seat_no}, {seats.status}, {seats.customer} FROM seats;
COMMIT;
)";
    }

    db.recovery_manager.printPolicy();
    db.execute(script);
    db.recovery_manager.printSummary();
    db.printBufferPoolSummary();
    
    return 0;
}
