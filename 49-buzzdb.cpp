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
#include <cctype>
#include <utility>
#include <initializer_list>
#include <cstring>
#include <type_traits>
#include <cstdint>

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

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

using PageID = uint16_t;
using TableId = uint16_t;

constexpr PageID INVALID_PAGE_ID = std::numeric_limits<PageID>::max();
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId SYS_TABLES_ID = 1;
constexpr TableId SYS_COLUMNS_ID = 2;
constexpr TableId FIRST_USER_TABLE_ID = 100;
constexpr uint32_t BUZZDB_MAGIC = 0x425A4442;
constexpr uint16_t BUZZDB_VERSION = 48;

// Page 0 root used to find the catalog after restart.
struct BootstrapPage {
    uint32_t magic = BUZZDB_MAGIC;
    uint16_t version = BUZZDB_VERSION;
    TableId next_table_id = FIRST_USER_TABLE_ID;
    PageID tables_first_page = INVALID_PAGE_ID;
    PageID tables_last_page = INVALID_PAGE_ID;
    PageID columns_first_page = INVALID_PAGE_ID;
    PageID columns_last_page = INVALID_PAGE_ID;
};

// Heap pages carry table ownership and table-local links.
struct PageHeader {
    TableId table_id = INVALID_TABLE_ID;
    PageID prev_page = INVALID_PAGE_ID;
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
        auto* header = getHeader();
        header->table_id = INVALID_TABLE_ID;
        header->prev_page = INVALID_PAGE_ID;
        header->next_page = INVALID_PAGE_ID;

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

    PageID getPrevPage() {
        return getHeader()->prev_page;
    }

    void setPrevPage(PageID page_id) {
        getHeader()->prev_page = page_id;
    }

    PageID getNextPage() {
        return getHeader()->next_page;
    }

    void setNextPage(PageID page_id) {
        getHeader()->next_page = page_id;
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

    PageID extend(TableId table_id = INVALID_TABLE_ID){
        storage_manager.extend();
        PageID page_id = static_cast<PageID>(storage_manager.num_pages - 1);
        auto& page = getPage(page_id);
        page->setTableId(table_id);
        page->setPrevPage(INVALID_PAGE_ID);
        page->setNextPage(INVALID_PAGE_ID);
        flushPage(page_id);
        return page_id;
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

    bool isEmptyDatabase() const {
        return storage_manager.num_pages == 0;
    }

    BootstrapPage getBootstrap() {
        if (storage_manager.num_pages == 0) {
            throw std::runtime_error("Bootstrap page is not initialized.");
        }

        // Page 0 points to catalog roots.
        BootstrapPage bootstrap;
        auto& page = getPage(0);
        std::memcpy(&bootstrap, page->page_data.get(), sizeof(BootstrapPage));
        if (bootstrap.magic != BUZZDB_MAGIC || bootstrap.version != BUZZDB_VERSION) {
            throw std::runtime_error(database_filename + " is not a v48 BuzzDB file.");
        }
        return bootstrap;
    }

    void initializeBootstrap(const BootstrapPage& bootstrap) {
        if (storage_manager.num_pages != 0) {
            throw std::runtime_error("Cannot initialize bootstrap page in a non-empty database.");
        }

        // Reserve page 0 before table heap pages.
        storage_manager.extend();
        auto& page = getPage(0);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), &bootstrap, sizeof(BootstrapPage));
        flushPage(0);
    }

    void flushBootstrap(const BootstrapPage& bootstrap) {
        auto& page = getPage(0);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), &bootstrap, sizeof(BootstrapPage));
        flushPage(0);
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

// Heap storage for one table: unordered tuple pages tracked by TableMetadata.
class TableHeap {
private:
    TableMetadata& metadata;
    BufferManager& buffer_manager;

public:
    TableHeap(TableMetadata& metadata, BufferManager& buffer_manager)
        : metadata(metadata), buffer_manager(buffer_manager) {
        // page_ids is rebuilt from first_page/next_page.
        loadPageIds();
    }

    bool insertTuple(std::unique_ptr<Tuple> tuple) {
        for (PageID page_id : metadata.page_ids) {
            auto& page = getPage(page_id);
            if (page->addTuple(tuple->clone())) {
                buffer_manager.flushPage(page_id);
                metadata.row_count++;
                return true;
            }
        }

        PageID previous_last_page = metadata.last_page;
        PageID page_id = allocatePage();
        metadata.page_ids.push_back(page_id);
        if (metadata.first_page == INVALID_PAGE_ID) {
            metadata.first_page = page_id;
        }
        metadata.last_page = page_id;

        // Link the new page so the table can be reopened.
        if (previous_last_page != INVALID_PAGE_ID) {
            auto& previous_last = getPage(previous_last_page);
            previous_last->setNextPage(page_id);
            buffer_manager.flushPage(previous_last_page);

            auto& page = getPage(page_id);
            page->setPrevPage(previous_last_page);
        }

        auto& page = getPage(page_id);
        if (page->addTuple(std::move(tuple))) {
            buffer_manager.flushPage(page_id);
            metadata.row_count++;
            return true;
        }

        return false;
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
    PageID allocatePage() {
        PageID page_id = buffer_manager.extend(metadata.table_id);
        auto& page = buffer_manager.getPage(page_id);
        page->setTableId(metadata.table_id);
        page->setPrevPage(INVALID_PAGE_ID);
        page->setNextPage(INVALID_PAGE_ID);
        buffer_manager.flushPage(page_id);
        return page_id;
    }

    void loadPageIds() {
        metadata.page_ids.clear();
        PageID page_id = metadata.first_page;

        // Rebuild v47-style page_ids from persisted links.
        while (page_id != INVALID_PAGE_ID) {
            metadata.page_ids.push_back(page_id);
            auto& page = getPage(page_id);
            page_id = page->getNextPage();
        }
    }
};

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

        const auto& bootstrap_page = buffer_manager.getBootstrap();
        installSystemTables(bootstrap_page);
        next_table_id = bootstrap_page.next_table_id;
    }

    bool isNewDatabase() const {
        return initialized_new_database;
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
        page->setPrevPage(INVALID_PAGE_ID);
        page->setNextPage(INVALID_PAGE_ID);
        buffer_manager.flushPage(first_page);

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

private:
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

    static TableSchema tablesTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"table_name", STRING},
            {"first_page", INT},
            {"last_page", INT},
            {"row_count", INT}
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
        buffer_manager.initializeBootstrap(bootstrap_page);

        // Give system catalog tables their first heap pages.
        PageID tables_page = buffer_manager.extend(SYS_TABLES_ID);
        PageID columns_page = buffer_manager.extend(SYS_COLUMNS_ID);

        bootstrap_page = buffer_manager.getBootstrap();
        bootstrap_page.tables_first_page = tables_page;
        bootstrap_page.tables_last_page = tables_page;
        bootstrap_page.columns_first_page = columns_page;
        bootstrap_page.columns_last_page = columns_page;
        bootstrap_page.next_table_id = FIRST_USER_TABLE_ID;
        buffer_manager.flushBootstrap(bootstrap_page);

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
        TableMetadata tables_metadata{
            SYS_TABLES_ID, "__tables", tablesTableSchema(),
            {bootstrap_page.tables_first_page},
            bootstrap_page.tables_first_page,
            bootstrap_page.tables_last_page,
            0, true
        };
        TableMetadata columns_metadata{
            SYS_COLUMNS_ID, "__columns", columnsTableSchema(),
            {bootstrap_page.columns_first_page},
            bootstrap_page.columns_first_page,
            bootstrap_page.columns_last_page,
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
        BootstrapPage bootstrap_page = buffer_manager.getBootstrap();
        bootstrap_page.next_table_id = next_table_id;
        buffer_manager.flushBootstrap(bootstrap_page);
    }

    void syncSystemTableBootstrap(const TableMetadata& metadata) {
        BootstrapPage bootstrap_page = buffer_manager.getBootstrap();

        // Keep page 0 in sync when a system table grows.
        if (metadata.table_id == SYS_TABLES_ID) {
            bootstrap_page.tables_first_page = metadata.first_page;
            bootstrap_page.tables_last_page = metadata.last_page;
        } else if (metadata.table_id == SYS_COLUMNS_ID) {
            bootstrap_page.columns_first_page = metadata.first_page;
            bootstrap_page.columns_last_page = metadata.last_page;
        } else {
            return;
        }

        buffer_manager.flushBootstrap(bootstrap_page);
    }

    void insertSystemTuple(TableId system_table_id, std::unique_ptr<Tuple> tuple) {
        auto& metadata = getTable(system_table_id);
        PageID previous_last_page = metadata.last_page;

        // System tables share heap code; only page-0 roots need syncing.
        TableHeap table(metadata, buffer_manager);
        bool status = table.insertTuple(std::move(tuple));
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
        return tuple;
    }

    void persistTableRecord(const TableMetadata& metadata) {
        auto tuple = makeTableRecordTuple(metadata);
        auto& tables_metadata = getTable(SYS_TABLES_ID);
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
                        buffer_manager.flushPage(page_id);
                    } else {
                        insertSystemTuple(SYS_TABLES_ID, std::move(tuple));
                    }
                    return;
                }
            }
        }

        insertSystemTuple(SYS_TABLES_ID, std::move(tuple));
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

        // __tables holds table metadata; __columns holds schemas.
        for (auto& tuple : tables_heap.readAllTuples()) {
            TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
            if (table_id == SYS_TABLES_ID || table_id == SYS_COLUMNS_ID) {
                continue;
            }

            TableMetadata candidate{
                table_id,
                tuple->fields[1]->asString(),
                TableSchema{},
                {},
                static_cast<PageID>(tuple->fields[2]->asInt()),
                static_cast<PageID>(tuple->fields[3]->asInt()),
                static_cast<size_t>(tuple->fields[4]->asInt()),
                false
            };

            if (predicate(candidate)) {
                return candidate;
            }
        }

        return std::nullopt;
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

class NestedLoopJoinOperator : public BinaryOperator {
private:
    std::unique_ptr<IPredicate> predicate;
    std::vector<std::vector<std::unique_ptr<Field>>> rightTuples;
    std::vector<std::unique_ptr<Field>> currentLeftTuple;
    std::vector<std::unique_ptr<Field>> currentOutput;
    size_t rightTupleIndex = 0;
    bool has_left_tuple = false;
    bool has_next = false;

public:
    NestedLoopJoinOperator(Operator& left, Operator& right,
                           std::unique_ptr<IPredicate> predicate)
        : BinaryOperator(left, right), predicate(std::move(predicate)) {}

    void open() override {
        input_left->setTxnContext(txn_);
        input_right->setTxnContext(txn_);
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
                if (!input_left->next()) return false;
                currentLeftTuple = input_left->getOutput();
                rightTupleIndex = 0;
                has_left_tuple = true;
            }
            while (rightTupleIndex < rightTuples.size()) {
                const auto& right_tuple = rightTuples[rightTupleIndex++];
                std::vector<std::unique_ptr<Field>> candidate;
                for (const auto& field : currentLeftTuple) {
                    candidate.push_back(field->clone());
                }
                for (const auto& field : right_tuple) {
                    candidate.push_back(field->clone());
                }

                if (predicate->check(candidate)) {
                    currentOutput = std::move(candidate);
                    has_next = true;
                    return true;
                }
            }
            has_left_tuple = false;
        }
    }

    void close() override {
        input_left->close();
        rightTuples.clear();
        currentLeftTuple.clear();
        currentOutput.clear();
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

struct ColumnRef {
    std::string tableName;
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
    int sumAttributeIndex = -1;
    bool groupBy = false;
    int groupByAttributeIndex = -1;
    bool whereCondition = false;
    int whereAttributeIndex = -1;
    int lowerBound = std::numeric_limits<int>::min();
    int upperBound = std::numeric_limits<int>::max();
};

enum class StatementType {
    INSERT
};

struct StatementComponents {
    StatementType type = StatementType::INSERT;
    std::string tableName;
    std::vector<std::string> values;
    size_t lineNumber = 0;
};

QueryComponents parseQuery(const std::string& query) {
    QueryComponents components;
    if (!std::regex_search(query, std::regex("^\\s*PROJECT\\s+"))) {
        throw std::runtime_error("Query must start with PROJECT.");
    }

    // Parse the query target: PROJECT {*} FROM table_name, or PROJECT SUM{n}.
    std::regex tableRegex(
        "^\\s*PROJECT\\s+(?:\\{\\*\\}|SUM\\{(\\d+)\\})\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)");
    std::smatch tableMatches;
    if (std::regex_search(query, tableMatches, tableRegex)) {
        if (tableMatches[1].matched) {
            components.sumOperation = true;
            components.sumAttributeIndex = std::stoi(tableMatches[1]) - 1;
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
        std::regex columnRefRegex("\\{([A-Za-z_][A-Za-z0-9_]*)\\.(\\d+)\\}");
        std::smatch columnMatches;
        auto columnStart = select_part.cbegin();
        while (std::regex_search(columnStart, select_part.cend(), columnMatches, columnRefRegex)) {
            components.selectColumns.push_back({
                columnMatches[1], std::stoi(columnMatches[2]) - 1
            });
            columnStart = columnMatches.suffix().first;
        }
    }

    std::regex joinRegex(
        "JOIN\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+ON\\s+"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.(\\d+)\\}\\s*=\\s*"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.(\\d+)\\}");
    std::smatch joinMatches;
    auto joinStart = query.cbegin();
    while (std::regex_search(joinStart, query.cend(), joinMatches, joinRegex)) {
        components.joins.push_back({
            joinMatches[1],
            {joinMatches[2], std::stoi(joinMatches[3]) - 1},
            {joinMatches[4], std::stoi(joinMatches[5]) - 1}
        });
        joinStart = joinMatches.suffix().first;
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

    if (components.tableName.empty()) {
        throw std::runtime_error("Query must name a table.");
    }

    return components;
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
        tree = "NestedLoopJoin(" + tree + ", Scan(" + join.tableName + "))";
    }
    if (components.whereCondition) {
        tree = "Select(" + tree + ")";
    }
    if (components.sumOperation || components.groupBy) {
        tree = "HashAggregate(" + tree + ")";
    }
    return "Project(" + tree + ")";
}

template <typename Components>
void prettyPrint(const Components& components,
                 const std::vector<int>& queryColumns = {}) {
    if constexpr (std::is_same<Components, QueryComponents>::value) {
        std::cout << "Query Components for table " << components.tableName << ":\n";
        std::cout << "  Columns: ";
        if (!components.selectColumns.empty()) {
            for (const auto& column : components.selectColumns) {
                std::cout << "{" << column.tableName << "." << column.attributeIndex + 1 << "} ";
            }
        } else {
            for (auto attr : queryColumns) {
                std::cout << "{" << attr + 1 << "} "; // Convert back to 1-based indexing for display
            }
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
        if (!components.joins.empty()) {
            std::cout << "\n  JOIN(s): ";
            for (const auto& join : components.joins) {
                std::cout << join.tableName << " ON {"
                          << join.left.tableName << "." << join.left.attributeIndex + 1
                          << "} = {" << join.right.tableName << "."
                          << join.right.attributeIndex + 1 << "} ";
            }
        }
    } else if constexpr (std::is_same<Components, StatementComponents>::value) {
        (void)queryColumns;
        std::cout << "Statement Components for table " << components.tableName << ":\n";
        std::cout << "  Operation: ";
        switch (components.type) {
            case StatementType::INSERT:
                std::cout << "INSERT";
                break;
        }
        std::cout << "\n  Values: ";
        for (size_t i = 0; i < components.values.size(); i++) {
            std::cout << "{" << i + 1 << "}=" << components.values[i] << " ";
        }
    }
    std::cout << std::endl;
}

void executeQuery(const QueryComponents& components,
                  Catalog& catalog,
                  BufferManager& buffer_manager,
                  const TxnPtr& txn = nullptr) {
    std::map<std::string, size_t> table_offsets;
    std::map<std::string, size_t> table_widths;
    std::vector<std::unique_ptr<TableHeap>> heaps;
    std::vector<std::unique_ptr<ScanOperator>> scans;
    std::vector<std::unique_ptr<NestedLoopJoinOperator>> joinOpBuffers;

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
            right_attr_index = output_width + join.right.attributeIndex;
        } else if (table_offsets.find(join.right.tableName) != table_offsets.end() &&
                   join.left.tableName == join.tableName) {
            left_attr_index = table_offsets[join.right.tableName] + join.right.attributeIndex;
            right_attr_index = output_width + join.left.attributeIndex;
        } else {
            throw std::runtime_error("JOIN must connect a new table to an earlier table.");
        }
        auto predicate = std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(left_attr_index),
            SimplePredicate::Operand(right_attr_index),
            SimplePredicate::ComparisonOperator::EQ
        );
        joinOpBuffers.push_back(std::make_unique<NestedLoopJoinOperator>(
            *rootOp, right_scan, std::move(predicate)));
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
    BufferManager& bufferManager;
    std::unique_ptr<Tuple> tupleToInsert;

public:
    InsertOperator(BufferManager& manager) : bufferManager(manager) {}

    // Set the tuple to be inserted by this operator.
    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {
        // Not used in this context
    }

    bool next() override {
        if (!tupleToInsert) return false; // No tuple to insert

        for (size_t pageId = 0; pageId < bufferManager.getNumPages(); ++pageId) {
            auto& page = bufferManager.getPage(pageId);
            // Attempt to insert the tuple
            if (page->addTuple(tupleToInsert->clone())) { 
                // Flush the page to disk after insertion
                bufferManager.flushPage(pageId); 
                return true; // Insertion successful
            }
        }

        // If insertion failed in all existing pages, extend the database and try again
        bufferManager.extend();
        auto& newPage = bufferManager.getPage(bufferManager.getNumPages() - 1);
        if (newPage->addTuple(tupleToInsert->clone())) {
            bufferManager.flushPage(bufferManager.getNumPages() - 1);
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
    HashIndex hash_index;
    BufferManager buffer_manager;
    Catalog catalog;
    TransactionManager txn_manager;

    BuzzDB() : buffer_manager(), catalog(buffer_manager) {
        catalog.bootstrap();
    }

    bool isNewDatabase() const { return catalog.isNewDatabase(); }

    // NEW: helpers
    TxnPtr begin() { return txn_manager.begin(); }
    void commit(const TxnPtr& tx) { txn_manager.commit(*tx); }
    void abort (const TxnPtr& tx) { txn_manager.abort(*tx);  }

    TableId createTable(const std::string& name,
                        std::initializer_list<ColumnSchema> columns) {
        TableSchema schema;
        for (const auto& column : columns) {
            schema.columns.push_back(column);
        }

        auto result = catalog.createTable(name, std::move(schema));
        std::cout << (result.created ? "Created table " : "Loaded table ")
                  << name << " with id " << result.table_id << "\n";
        return result.table_id;
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
        std::regex insert_regex(
            "^\\s*INSERT\\s+([A-Za-z_][A-Za-z0-9_]*)\\|(.*)$");
        std::smatch matches;

        if (!std::regex_match(statement, matches, insert_regex)) {
            throw std::runtime_error("Unsupported statement: " + statement);
        }

        StatementComponents components;
        components.type = StatementType::INSERT;
        components.tableName = matches[1];
        components.values = splitPipeLine(matches[2]);
        components.lineNumber = line_number;
        return components;
    }

    // Data-changing statements are separate from read queries.
    void executeStatement(const std::string& statement,
                          const TxnPtr& txn = nullptr,
                          size_t line_number = 0,
                          bool print_statement = true) {
        (void)txn;
        auto components = parseStatement(statement, line_number);
        if (print_statement) {
            prettyPrint(components);
        }

        auto& metadata = catalog.getTable(components.tableName);
        if (components.values.size() != metadata.schema.columns.size()) {
            throw std::runtime_error(
                "Wrong field count for table " + components.tableName +
                lineContext(line_number)
            );
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

        TableHeap table(metadata, buffer_manager);
        bool status = table.insertTuple(std::move(tuple));
        assert(status == true);
        catalog.persistTableMetadata(metadata);
    }

    void loadImdbFile(const std::string& filename,
                      const TxnPtr& txn = nullptr,
                      bool print_statements = false) {
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

            executeStatement(
                "INSERT " + line, txn, line_number, print_statements
            );
        }
    }

    void executeQuery(const std::string& query,
                      const TxnPtr& txn = nullptr) {
        auto components = parseQuery(query);
        auto& metadata = catalog.getTable(components.tableName);
        auto queryColumns = deriveQueryColumns(components, metadata);
        std::cout << "Operator Tree: " << operatorTreeString(components) << std::endl;
        prettyPrint(components, queryColumns);
        ::executeQuery(components, catalog, buffer_manager, txn);
    }

    void executeQueries(const TxnPtr& txn = nullptr) {
        std::vector<std::string> test_queries = {
            "PROJECT {*} FROM title",
            "PROJECT SUM{3} FROM title GROUP BY {4} WHERE {4} > 1980 and {4} < 2013",
            // Join query
            "PROJECT {title.2}, {title.4}, {company_name.2}, {company_type.2}, "
            "{movie_companies.5} FROM title "
            "JOIN movie_companies ON {title.1} = {movie_companies.2} "
            "JOIN company_name ON {movie_companies.3} = {company_name.1} "
            "JOIN company_type ON {movie_companies.4} = {company_type.1}",
        };

        for (const auto& query : test_queries) {
            executeQuery(query, txn);
        }

    }
    
};

int main() {

    BuzzDB db;

    // --- Transaction 1: perform inserts ---
    TxnPtr txn1 = db.begin();
    bool seed_database = db.isNewDatabase();

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

    if (seed_database) {
        db.loadImdbFile("imdb.txt", txn1);
    }

    db.commit(txn1); // commit once for the whole batch

    // --- Transaction 2: perform queries ---
    TxnPtr txn2 = db.begin(); 
    auto start = std::chrono::high_resolution_clock::now();

    db.executeQueries(txn2);

    auto end = std::chrono::high_resolution_clock::now();

    db.commit(txn2); // Also commit this txn, even if read-only
    
    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << 
    std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() 
          << " microseconds" << std::endl;

    
    return 0;
}
