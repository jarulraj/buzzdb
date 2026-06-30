#include <iostream>
#include <map>
#include <vector>
#include <fstream>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <regex>
#include <stdexcept>
#include <exception>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <iterator>
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
#include <system_error>
#include <variant>
#include <set>
#include <random>
#include <deque>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>

std::mutex output_latch;

void printThreadSafe(const std::string& line) {
    std::lock_guard<std::mutex> guard(output_latch);
    std::cout << line << std::endl;
}

static constexpr int RECOVERY_TXN_ID = -1;

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
        data = std::make_unique<char[]>(data_length);
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

struct TupleId {
    uint16_t table_id;
    uint16_t page_id;
    size_t slot_id;

    bool operator<(const TupleId& other) const {
        if (table_id != other.table_id) {
            return table_id < other.table_id;
        }
        if (page_id != other.page_id) {
            return page_id < other.page_id;
        }
        return slot_id < other.slot_id;
    }
};

struct QueryRow {
    std::vector<std::unique_ptr<Field>> fields;
    std::optional<TupleId> tuple_id;
};

using QueryTable = std::vector<QueryRow>;

std::string fieldToString(const Field& field) {
    switch (field.getType()) {
        case INT:
            return std::to_string(field.asInt());
        case FLOAT:
            return std::to_string(field.asFloat());
        case STRING:
            return field.asString();
    }
    throw std::runtime_error("Unknown field type.");
}

Field parseLiteralField(FieldType type, const std::string& token) {
    switch (type) {
        case INT:
            return Field(std::stoi(token));
        case FLOAT:
            return Field(std::stof(token));
        case STRING:
            return Field(token);
    }
    throw std::runtime_error("Unknown field type.");
}

void printQueryTable(const QueryTable& rows) {
    for (const auto& row : rows) {
        for (const auto& field : row.fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << std::endl;
    }
}

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    Tuple() = default;
    Tuple(Tuple&& other) noexcept = default;
    Tuple& operator=(Tuple&& other) noexcept = default;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
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

bool hasReadableFields(const std::unique_ptr<Tuple>& tuple, size_t count) {
    if (tuple->fields.size() < count) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (!tuple->fields[i]) {
            return false;
        }
    }
    return true;
}

static constexpr size_t PAGE_SIZE = 4096;  // Regular pages for JOB/query workloads
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

using PageID = uint16_t;
using BootstrapPageID = uint16_t;
using TableId = uint16_t;
using LSN = uint64_t;

constexpr PageID INVALID_PAGE_ID = std::numeric_limits<PageID>::max();
constexpr TableId INVALID_TABLE_ID = 0;
constexpr TableId SYS_TABLES_ID = 1;
constexpr TableId SYS_COLUMNS_ID = 2;
constexpr TableId SYS_STATS_ID = 3;
constexpr TableId SYS_STAT_VALUES_ID = 4;
constexpr TableId FIRST_USER_TABLE_ID = 100;
constexpr int STAT_KIND_MCV = 1;
constexpr int STAT_KIND_EQUI_WIDTH = 2;
constexpr int STAT_KIND_EQUI_DEPTH = 3;
constexpr uint32_t BUZZDB_MAGIC = 0x425A4442;
constexpr uint16_t BUZZDB_VERSION = 72;
constexpr uint16_t BUZZDB_MIN_COMPATIBLE_VERSION = 59;
constexpr size_t MAX_SYSTEM_TABLE_PAGES = 16;
std::string log_filename = "buzzdb.log";
std::string master_record_filename = "buzzdb.master";
std::string image_copy_filename = "buzzdb.image.dat";
std::string image_copy_metadata_filename = "buzzdb.image.meta";

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

void copyFileDurably(const std::string& source_filename,
                     const std::string& target_filename,
                     const std::string& description) {
    std::ifstream input(source_filename, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open source file for " + description + ".");
    }
    std::ofstream output(target_filename, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to create target file for " + description + ".");
    }
    output << input.rdbuf();
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write target file for " + description + ".");
    }
    output.close();
    forceFileToStableStorage(target_filename, description);
}

void writeDurableTextFile(const std::string& filename,
                          const std::string& contents,
                          const std::string& description) {
    std::ofstream output(filename, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to create " + description + ".");
    }
    output << contents;
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write " + description + ".");
    }
    output.close();
    forceFileToStableStorage(filename, description);
}

struct CatalogRoot {
    uint64_t catalog_version = 0;
    uint16_t tables_page_count = 0;
    BootstrapPageID tables_pages[MAX_SYSTEM_TABLE_PAGES] = {};
    uint16_t columns_page_count = 0;
    BootstrapPageID columns_pages[MAX_SYSTEM_TABLE_PAGES] = {};
    uint32_t checksum = 0;
};

struct ImageCopyMetadata {
    LSN master_lsn = 0;
    size_t master_offset = 0;
    LSN image_copy_lsn = 0;
    size_t page_count = 0;
};

void writeImageCopyMetadata(const ImageCopyMetadata& metadata) {
    std::stringstream output;
    output << "master_lsn " << metadata.master_lsn << "\n"
           << "master_offset " << metadata.master_offset << "\n"
           << "image_copy_lsn " << metadata.image_copy_lsn << "\n"
           << "page_count " << metadata.page_count << "\n";
    writeDurableTextFile(
        image_copy_metadata_filename,
        output.str(),
        "database image copy metadata"
    );
}

// Page 0 stores two catalog roots; the highest valid version wins.
struct BootstrapPage {
    uint32_t magic = BUZZDB_MAGIC;
    uint16_t version = BUZZDB_VERSION;
    TableId next_table_id = FIRST_USER_TABLE_ID;
    CatalogRoot catalog_roots[2];
};

static_assert(sizeof(BootstrapPage) <= PAGE_SIZE,
              "BootstrapPage must fit in page 0.");

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

    // Add a tuple, returning the slot it occupies when it fits.
    std::optional<size_t> addTupleAndReturnSlot(
        std::unique_ptr<Tuple> tuple) {

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
            return std::nullopt;
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
            return std::nullopt;
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

        return slot_itr;
    }

    bool addTuple(std::unique_ptr<Tuple> tuple) {
        return addTupleAndReturnSlot(std::move(tuple)).has_value();
    }

    bool insertTupleAtSlot(size_t index, std::unique_ptr<Tuple> tuple) {
        if (index >= MAX_SLOTS) {
            return false;
        }

        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        Slot& slot = slot_array[index];

        if (slot.offset == INVALID_VALUE) {
            size_t offset = metadata_size;
            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_itr == index) continue;
                if (slot_array[slot_itr].offset == INVALID_VALUE ||
                    slot_array[slot_itr].length == INVALID_VALUE) {
                    continue;
                }
                offset = std::max(
                    offset,
                    static_cast<size_t>(slot_array[slot_itr].offset) +
                    static_cast<size_t>(slot_array[slot_itr].length)
                );
            }
            slot.offset = static_cast<uint16_t>(offset);
        }

        if (slot.length != INVALID_VALUE && tuple_size > slot.length) {
            throw std::runtime_error(
                "Recovered tuple is too large to fit in existing slot."
            );
        }

        if (slot.offset + tuple_size >= PAGE_SIZE) {
            if (slot.empty) {
                slot.offset = INVALID_VALUE;
            }
            return false;
        }

        if (slot.length == INVALID_VALUE) {
            slot.length = static_cast<uint16_t>(tuple_size);
        }
        slot.empty = false;
        std::memset(page_data.get() + slot.offset, 0, slot.length);
        std::memcpy(page_data.get() + slot.offset,
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

std::string database_filename = "buzzdb.dat";
constexpr bool TRACE_STORAGE = false;

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;
    size_t stable_storage_forces = 0;
    size_t deferred_stable_storage_requests = 0;
    size_t total_deferred_stable_storage_requests = 0;
    bool defer_stable_storage_forces = false;

    void forceDatabaseFileToStableStorage() {
        fileStream.flush();
        if (!fileStream) {
            throw std::runtime_error("Unable to flush database file stream.");
        }
        if (defer_stable_storage_forces) {
            deferred_stable_storage_requests++;
            total_deferred_stable_storage_requests++;
            return;
        }
        forceFileToStableStorage(database_filename, "database file");
        stable_storage_forces++;
    }

    void setDeferStableStorageForces(bool defer) {
        defer_stable_storage_forces = defer;
    }

    void forceDeferredDatabaseFileToStableStorage() {
        if (deferred_stable_storage_requests == 0) {
            return;
        }
        fileStream.flush();
        if (!fileStream) {
            throw std::runtime_error("Unable to flush database file stream.");
        }
        forceFileToStableStorage(database_filename, "database file");
        stable_storage_forces++;
        deferred_stable_storage_requests = 0;
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

        // Extend the file; callers decide when the initialized page is durable.
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();
        if (!fileStream) {
            throw std::runtime_error("Unable to extend database file.");
        }

        // Update number of pages
        num_pages += 1;
    }

    void createImageCopy() {
        /* Fuzzy copy: force existing file writes, but do not flush buffer pages. */
        forceDatabaseFileToStableStorage();
        copyFileDurably(database_filename, image_copy_filename, "database image copy");
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

constexpr size_t MAX_PAGES_IN_MEMORY = 512;

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
    std::set<PageID> dirty_pages;
    BufferPoolStats stats;
    std::function<bool(LSN)> wal_force_callback;
    std::function<void(PageID, LSN, const std::string&)> page_flush_callback;

    std::string evictionWriteTag(PageID page_id) {
        if (page_id == 0) {
            return "catalog eviction";
        }
        TableId table_id = pageMap[page_id]->getTableId();
        if (table_id == SYS_TABLES_ID ||
            table_id == SYS_COLUMNS_ID ||
            table_id == SYS_STATS_ID ||
            table_id == SYS_STAT_VALUES_ID) {
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
        if (dirty_pages.find(page_id) == dirty_pages.end()) {
            return;
        }
        forceLogBeforeFlush(page_id, tag);
        LSN page_lsn = pageMap[page_id]->getPageLSN();
        storage_manager.flush(page_id, pageMap[page_id]);
        dirty_pages.erase(page_id);
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
            if (TRACE_STORAGE) {
                std::cout << "Evicting page " << evictedPageId << "\n";
            }
            if (dirty_pages.find(evictedPageId) != dirty_pages.end()) {
                flushPageToDisk(evictedPageId, evictionWriteTag(evictedPageId));
            }
            stats.evictions++;
            stats.page_flushes_from_eviction++;
            pageMap.erase(evictedPageId);
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

    void markDirty(PageID page_id) {
        dirty_pages.insert(page_id);
    }

    void flushAllPages(const std::string& tag = "flush all") {
        for (auto& entry : pageMap) {
            if (pin_count.find(entry.first) != pin_count.end()) {
                continue;
            }
            flushPageToDisk(entry.first, tag);
        }
    }

    void clearBufferPool() {
        if (!pin_count.empty()) {
            throw std::runtime_error("Cannot clear buffer pool while pages are pinned.");
        }
        flushAllPages("clear buffer pool");
        pageMap.clear();
        dirty_pages.clear();
        policy = std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY);
    }

    void setDeferStableStorageForces(bool defer) {
        storage_manager.setDeferStableStorageForces(defer);
    }

    void forceDeferredDatabaseFileToStableStorage() {
        storage_manager.forceDeferredDatabaseFileToStableStorage();
    }

    PageID extend(TableId table_id = INVALID_TABLE_ID,
                  const std::string& tag = "new page initialization",
                  bool flush_page = true){
        storage_manager.extend();
        PageID page_id = static_cast<PageID>(storage_manager.num_pages - 1);
        auto& page = getPage(page_id);
        page->setTableId(table_id);
        markDirty(page_id);
        if (flush_page) {
            flushPage(page_id, tag);
        }
        return page_id;
    }

    size_t getNumPages(){
        return storage_manager.num_pages;
    }

    void createImageCopy() {
        storage_manager.createImageCopy();
    }

    void printBufferPoolSummary() const {
        std::cout << "Durable database page write summary:" << std::endl;
        std::cout << "  Total durable page writes: "
                  << stats.data_page_writes << std::endl;
        if (!stats.data_page_writes_by_tag.empty()) {
            std::cout << "  Durable writes by tag:" << std::endl;
            for (const auto& [tag, count] : stats.data_page_writes_by_tag) {
                std::cout << "    " << tag << ": " << count << std::endl;
            }
        }
        std::cout << "  Page loads: " << stats.page_loads << std::endl;
        std::cout << "  Cache hits: " << stats.cache_hits << std::endl;
        std::cout << "  Evictions: " << stats.evictions << std::endl;
        std::cout << "  Database fsyncs: "
                  << storage_manager.stable_storage_forces << std::endl;
        std::cout << "  Deferred database fsync requests coalesced: "
                  << storage_manager.total_deferred_stable_storage_requests << std::endl;
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
        LSN lsn = 0;
        size_t offset = 0;
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

    LSN getFlushedLSN() const {
        return flushed_lsn;
    }

    size_t getStableLogForces() const {
        return stable_log_forces;
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

enum class PageUpdateKind {
    Update,
    Insert,
    Delete
};

// In-memory copy of one ARIES page-change record for runtime undo.
struct PageUpdateLogRecord {
    PageUpdateKind kind = PageUpdateKind::Update;
    LSN lsn = 0;
    LSN prev_lsn = 0;
    TableId table_id = 0;
    PageID page_id = INVALID_VALUE;
    size_t slot_id = INVALID_VALUE;
    std::unique_ptr<Tuple> before_tuple;
    std::unique_ptr<Tuple> after_tuple;
};

class TableHeap;

// Recovery infrastructure carried forward from v64.
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

    struct TxnRecoveryState {
        LSN last_lsn = 0;
        std::vector<PageUpdateLogRecord> page_update_log_records;
    };

    // Shared storage, catalog, and WAL components used by recovery.
    BufferManager& buffer_manager;
    Catalog& catalog;
    LogManager log_manager;
    std::mutex recovery_latch;
    // Current transaction state and ARIES lastLSN chain.
    bool txn_active = false;
    bool txn_logged = false;
    LSN current_txn_last_lsn = 0;
    int next_txn_id = 1;
    int current_txn_id = 0;
    // Crash knobs for the recovery workloads in main().
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
    size_t abort_log_records = 0;
    size_t log_force_requests = 0;
    size_t log_force_writes = 0;
    size_t log_force_skips = 0;
    size_t checkpoints_written = 0;
    size_t checkpoint_analysis_start_lsn = 0;
    size_t restart_undo_locks_reacquired = 0;
    size_t restart_undo_deadlocks_resolved = 0;
    std::map<int, TxnTableEntry> active_transaction_table;
    std::map<int, TxnRecoveryState> txn_states;
    std::function<bool(TableId, PageID, size_t)> restart_undo_lock_callback;
    std::function<void()> restart_undo_release_callback;

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

    LSN appendInsertUndoClrRecord(int txn_id,
                                  LSN prev_lsn,
                                  LSN undo_next_lsn,
                                  TableId table_id,
                                  PageID page_id,
                                  size_t slot_id) {
        LSN clr_lsn = log_manager.append(
            "CLR_INSERT " + std::to_string(txn_id) + " " +
            std::to_string(prev_lsn) + " " +
            std::to_string(undo_next_lsn) + " " +
            std::to_string(table_id) + " " +
            std::to_string(page_id) + " " +
            std::to_string(slot_id)
        );
        clr_records_logged++;
        if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
            dirty_page_table[page_id] = clr_lsn;
        }
        return clr_lsn;
    }

    LSN appendDeleteUndoClrRecord(int txn_id,
                                  LSN prev_lsn,
                                  LSN undo_next_lsn,
                                  TableId table_id,
                                  PageID page_id,
                                  size_t slot_id,
                                  Tuple& after_undo_tuple) {
        auto after_undo_image = after_undo_tuple.serialize();
        LSN clr_lsn = log_manager.append(
            "CLR_DELETE " + std::to_string(txn_id) + " " +
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

    LSN appendUndoClrRecord(int txn_id,
                            LSN prev_lsn,
                            const PageUpdateLogRecord& record);

    static void applyUndoRecord(TableHeap& table,
                                const PageUpdateLogRecord& record,
                                LSN page_lsn,
                                const std::string& flush_tag);

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
        log_manager.readFromOffset(0);
    }
    bool isActive() const { return txn_active; }
    void resetLog() {
        log_manager.reset();
        dirty_page_table.clear();
    }
    void begin();
    void commit();
    void abort();
    void savepoint(const std::string& name);
    void rollbackTo(const std::string& name);
    void beginCheckpoint();
    void endCheckpoint();
    void checkpoint();
    void createFuzzyImageCopy();
    void recover();
    void setRestartUndoLockCallback(
        std::function<bool(TableId, PageID, size_t)> callback) {
        restart_undo_lock_callback = std::move(callback);
    }
    void setRestartUndoReleaseCallback(std::function<void()> callback) {
        restart_undo_release_callback = std::move(callback);
    }
    LSN logUpdate(TableId table_id,
                   PageID page_id,
                   size_t slot_id,
                   std::unique_ptr<Tuple> before_tuple,
                   std::unique_ptr<Tuple> after_tuple);
    LSN logUpdate(int txn_id,
                   TableId table_id,
                   PageID page_id,
                   size_t slot_id,
                   std::unique_ptr<Tuple> before_tuple,
                   std::unique_ptr<Tuple> after_tuple);
    LSN logInsert(TableId table_id,
                  PageID page_id,
                  size_t slot_id,
                  std::unique_ptr<Tuple> after_tuple);
    LSN logInsert(int txn_id,
                  TableId table_id,
                  PageID page_id,
                  size_t slot_id,
                  std::unique_ptr<Tuple> after_tuple);
    LSN logDelete(TableId table_id,
                  PageID page_id,
                  size_t slot_id,
                  std::unique_ptr<Tuple> before_tuple);
    LSN logDelete(int txn_id,
                  TableId table_id,
                  PageID page_id,
                  size_t slot_id,
                  std::unique_ptr<Tuple> before_tuple);
    bool forceLogUpTo(LSN lsn);
    void notePageFlushed(PageID page_id, LSN page_lsn, const std::string& tag);
    void maybeCrashAfterSteal(PageID page_id);
    void beginTxn(int txn_id);
    bool hasTxn(int txn_id);
    LSN queueCommit(int txn_id);
    void abortTxn(int txn_id);
    size_t forceCommitGroupUpTo(LSN max_commit_lsn);
    void finishTxn(int txn_id);
    size_t getStableLogForces() const {
        return log_manager.getStableLogForces();
    }
    LSN getFlushedLSN() const {
        return log_manager.getFlushedLSN();
    }
};


// One column in a table schema.
struct ColumnSchema {
    std::string name;
    FieldType type = INT;
};

// Ordered list of columns for a table.
struct TableSchema {
    std::vector<ColumnSchema> columns;
};

// In-memory metadata for one table and the pages owned by it.
struct TableMetadata {
    TableId table_id = 0;
    std::string name;
    TableSchema schema;
    std::vector<PageID> page_ids;
    PageID first_page = INVALID_VALUE;
    PageID last_page = INVALID_VALUE;
    size_t row_count = 0;
    bool system_table = false;
};

struct CreateTableResult {
    TableId table_id;
    bool created;
};

struct HistogramBucket {
    int lower = 0;
    int upper = 0;
    size_t count = 0;
};

struct PersistedColumnStats {
    TableId table_id = 0;
    int column_id = 0;
    size_t row_count = 0;
    size_t page_count = 0;
    size_t ndv = 0;
    bool has_int_range = false;
    int min_int = 0;
    int max_int = 0;
    std::map<std::string, size_t> mcv_counts;
    std::vector<HistogramBucket> equi_width_buckets;
    std::vector<HistogramBucket> equi_depth_buckets;
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

    void markDirty(PageID page_id) {
        buffer_manager.markDirty(page_id);
    }

    void recordInsertedTuple() {
        metadata.row_count++;
    }

    size_t updateTuples(size_t where_column,
                        const Field& where_value,
                        const std::vector<std::pair<size_t, Field>>& assignments,
                        RecoveryManager* recovery_manager = nullptr,
                        int txn_id = 0) {
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
                    if (txn_id != 0) {
                        update_lsn = recovery_manager->logUpdate(
                            txn_id,
                            metadata.table_id,
                            page_id,
                            slot_itr,
                            old_tuple->clone(),
                            new_tuple->clone()
                        );
                    } else {
                        update_lsn = recovery_manager->logUpdate(
                            metadata.table_id,
                            page_id,
                            slot_itr,
                            old_tuple->clone(),
                            new_tuple->clone()
                        );
                    }
                }

                bool status = (*page)->updateTuple(slot_itr, std::move(new_tuple));
                assert(status == true);
                markDirty(page_id);
                if (recovery_manager != nullptr) {
                    (*page)->setPageLSN(update_lsn);
                    printThreadSafe(
                        "  pageLSN: page " + std::to_string(page_id) +
                        " = " + std::to_string(update_lsn)
                    );
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
        markDirty(page_id);
        if (page_lsn != 0) {
            page->setPageLSN(page_lsn);
        }
        if (flush_page) {
            buffer_manager.flushPage(page_id, flush_tag);
        }
    }

    size_t deleteTuples(size_t where_column,
                        const Field& where_value,
                        RecoveryManager* recovery_manager = nullptr,
                        int txn_id = 0) {
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

                LSN delete_lsn = 0;
                if (recovery_manager != nullptr) {
                    if (txn_id != 0) {
                        delete_lsn = recovery_manager->logDelete(
                            txn_id,
                            metadata.table_id,
                            page_id,
                            slot_itr,
                            tuple->clone()
                        );
                    } else {
                        delete_lsn = recovery_manager->logDelete(
                            metadata.table_id,
                            page_id,
                            slot_itr,
                            tuple->clone()
                        );
                    }
                }

                page->deleteTuple(slot_itr);
                markDirty(page_id);
                if (recovery_manager != nullptr) {
                    page->setPageLSN(delete_lsn);
                    printThreadSafe(
                        "  pageLSN: page " + std::to_string(page_id) +
                        " = " + std::to_string(delete_lsn)
                    );
                    recovery_manager->maybeCrashAfterSteal(page_id);
                }
                page_updated = true;
                deleted_count++;
            }

            if (page_updated && recovery_manager == nullptr) {
                buffer_manager.flushPage(page_id, "delete");
            }
        }

        metadata.row_count -= deleted_count;
        return deleted_count;
    }

    bool deletePhysicalTuple(PageID page_id,
                             size_t slot_id,
                             const std::string& flush_tag,
                             LSN page_lsn = 0) {
        if (slot_id >= MAX_SLOTS) return false;
        auto& page = getPage(page_id);
        Slot* slot_array =
            reinterpret_cast<Slot*>(page->page_data.get());
        if (slot_array[slot_id].empty) return false;

        page->deleteTuple(slot_id);
        markDirty(page_id);
        if (page_lsn != 0) {
            page->setPageLSN(page_lsn);
        }
        buffer_manager.flushPage(page_id, flush_tag);
        if (metadata.row_count > 0) {
            metadata.row_count--;
        }
        return true;
    }

    bool applyPhysiologicalInsert(PageID page_id,
                                  size_t slot_id,
                                  std::unique_ptr<Tuple> tuple,
                                  bool flush_page = true,
                                  const std::string& flush_tag = "recovery insert",
                                  LSN page_lsn = 0) {
        auto& page = getPage(page_id);
        Slot* slot_array =
            reinterpret_cast<Slot*>(page->page_data.get());
        bool was_empty = slot_id < MAX_SLOTS && slot_array[slot_id].empty;
        bool status = page->insertTupleAtSlot(slot_id, std::move(tuple));
        if (!status) return false;
        markDirty(page_id);
        if (page_lsn != 0) {
            page->setPageLSN(page_lsn);
        }
        if (flush_page) {
            buffer_manager.flushPage(page_id, flush_tag);
        }
        if (was_empty) {
            metadata.row_count++;
        }
        return true;
    }

    bool applyPhysiologicalDelete(PageID page_id,
                                  size_t slot_id,
                                  bool flush_page = true,
                                  const std::string& flush_tag = "recovery delete",
                                  LSN page_lsn = 0) {
        if (slot_id >= MAX_SLOTS) return false;
        auto& page = getPage(page_id);
        Slot* slot_array =
            reinterpret_cast<Slot*>(page->page_data.get());
        if (slot_array[slot_id].empty) return false;

        page->deleteTuple(slot_id);
        markDirty(page_id);
        if (page_lsn != 0) {
            page->setPageLSN(page_lsn);
        }
        if (flush_page) {
            buffer_manager.flushPage(page_id, flush_tag);
        }
        if (metadata.row_count > 0) {
            metadata.row_count--;
        }
        return true;
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
        PageID page_id = buffer_manager.extend(
            metadata.table_id,
            "page allocation",
            flush_on_insert
        );
        auto& page = buffer_manager.getPage(page_id);
        page->setTableId(metadata.table_id);
        return page_id;
    }
};

std::optional<TupleId> insertTupleIntoTableWithId(
    TableHeap& table,
    std::unique_ptr<Tuple> tuple,
    RecoveryManager* recovery_manager = nullptr,
    int txn_id = 0) {
    auto insertIntoPage = [&](PageID page_id, const Tuple& tuple_to_insert) {
        auto& page = table.getPage(page_id);
        auto slot_id = page->addTupleAndReturnSlot(tuple_to_insert.clone());
        if (slot_id.has_value()) {
            LSN insert_lsn = 0;
            if (recovery_manager != nullptr) {
                if (txn_id != 0) {
                    insert_lsn = recovery_manager->logInsert(
                        txn_id,
                        table.getTableId(),
                        page_id,
                        *slot_id,
                        tuple_to_insert.clone()
                    );
                } else {
                    insert_lsn = recovery_manager->logInsert(
                        table.getTableId(),
                        page_id,
                        *slot_id,
                        tuple_to_insert.clone()
                    );
                }
            }
            table.markDirty(page_id);
            if (recovery_manager != nullptr) {
                page->setPageLSN(insert_lsn);
                printThreadSafe(
                    "  pageLSN: page " + std::to_string(page_id) +
                    " = " + std::to_string(insert_lsn)
                );
                recovery_manager->maybeCrashAfterSteal(page_id);
            }
            table.flushInsertedPage(page_id);
            table.recordInsertedTuple();
            return std::optional<TupleId>{
                TupleId{table.getTableId(), page_id, *slot_id}};
        }
        return std::optional<TupleId>{};
    };

    PageID last_page = table.getLastPage();
    if (last_page != INVALID_PAGE_ID) {
        auto inserted = insertIntoPage(last_page, *tuple);
        if (inserted.has_value()) return inserted;
    }

    PageID page_id = table.allocatePage();
    return insertIntoPage(page_id, *tuple);
}

bool insertTupleIntoTable(TableHeap& table, std::unique_ptr<Tuple> tuple) {
    return insertTupleIntoTableWithId(table, std::move(tuple)).has_value();
}

LSN RecoveryManager::appendUndoClrRecord(
    int txn_id,
    LSN prev_lsn,
    const PageUpdateLogRecord& record) {
    switch (record.kind) {
        case PageUpdateKind::Update:
            return appendClrRecord(
                txn_id,
                prev_lsn,
                record.prev_lsn,
                record.table_id,
                record.page_id,
                record.slot_id,
                *record.before_tuple
            );
        case PageUpdateKind::Insert:
            return appendInsertUndoClrRecord(
                txn_id,
                prev_lsn,
                record.prev_lsn,
                record.table_id,
                record.page_id,
                record.slot_id
            );
        case PageUpdateKind::Delete:
            return appendDeleteUndoClrRecord(
                txn_id,
                prev_lsn,
                record.prev_lsn,
                record.table_id,
                record.page_id,
                record.slot_id,
                *record.before_tuple
            );
    }
    throw std::runtime_error("Unknown page update kind during undo.");
}

void RecoveryManager::applyUndoRecord(
    TableHeap& table,
    const PageUpdateLogRecord& record,
    LSN page_lsn,
    const std::string& flush_tag) {
    switch (record.kind) {
        case PageUpdateKind::Update:
            table.applyPhysiologicalUpdate(
                record.page_id,
                record.slot_id,
                record.before_tuple->clone(),
                true,
                flush_tag,
                page_lsn
            );
            return;
        case PageUpdateKind::Insert:
            table.applyPhysiologicalDelete(
                record.page_id,
                record.slot_id,
                true,
                flush_tag,
                page_lsn
            );
            return;
        case PageUpdateKind::Delete:
            table.applyPhysiologicalInsert(
                record.page_id,
                record.slot_id,
                record.before_tuple->clone(),
                true,
                flush_tag,
                page_lsn
            );
            return;
    }
    throw std::runtime_error("Unknown page update kind during undo.");
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
        ensureStatsTable();
        ensureStatValuesTable();
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
            if (table_id == SYS_TABLES_ID ||
                table_id == SYS_COLUMNS_ID ||
                table_id == SYS_STATS_ID ||
                table_id == SYS_STAT_VALUES_ID) {
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
        PageID first_page = buffer_manager.extend(table_id, "table create");

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

    void persistColumnStats(const std::vector<PersistedColumnStats>& records) {
        if (records.empty()) {
            return;
        }
        ensureStatsTable();
        ensureStatValuesTable();

        std::set<TableId> table_ids;
        for (const auto& record : records) {
            table_ids.insert(record.table_id);
        }
        deleteStatsForTables(table_ids);
        deleteStatValuesForTables(table_ids);

        for (const auto& record : records) {
            auto tuple = std::make_unique<Tuple>();
            tuple->addField(std::make_unique<Field>(static_cast<int>(record.table_id)));
            tuple->addField(std::make_unique<Field>(record.column_id));
            tuple->addField(std::make_unique<Field>(static_cast<int>(record.row_count)));
            tuple->addField(std::make_unique<Field>(static_cast<int>(record.page_count)));
            tuple->addField(std::make_unique<Field>(static_cast<int>(record.ndv)));
            tuple->addField(std::make_unique<Field>(record.has_int_range ? 1 : 0));
            tuple->addField(std::make_unique<Field>(record.min_int));
            tuple->addField(std::make_unique<Field>(record.max_int));
            insertSystemTuple(SYS_STATS_ID, std::move(tuple));
            persistStatValueRows(record);
        }

        persistTableRecord(getTable(SYS_STATS_ID));
        persistTableRecord(getTable(SYS_STAT_VALUES_ID));
    }

    std::vector<PersistedColumnStats> loadColumnStats(TableId table_id) {
        ensureStatsTable();
        ensureStatValuesTable();
        auto& stats_metadata = getTable(SYS_STATS_ID);
        TableHeap stats_heap(stats_metadata, buffer_manager);
        std::vector<PersistedColumnStats> records;

        for (auto& tuple : stats_heap.readAllTuples()) {
            if (!hasReadableFields(tuple, 8)) {
                continue;
            }
            if (static_cast<TableId>(tuple->fields[0]->asInt()) != table_id) {
                continue;
            }
            records.push_back({
                table_id,
                tuple->fields[1]->asInt(),
                static_cast<size_t>(tuple->fields[2]->asInt()),
                static_cast<size_t>(tuple->fields[3]->asInt()),
                static_cast<size_t>(tuple->fields[4]->asInt()),
                tuple->fields[5]->asInt() != 0,
                tuple->fields[6]->asInt(),
                tuple->fields[7]->asInt(),
                {},
                {},
                {}
            });
        }

        loadStatValueRows(table_id, records);
        return records;
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
        if (bootstrap.magic != BUZZDB_MAGIC ||
            bootstrap.version < BUZZDB_MIN_COMPATIBLE_VERSION ||
            bootstrap.version > BUZZDB_VERSION) {
            throw std::runtime_error(
                database_filename +
                " was created by an incompatible BuzzDB storage format."
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
        buffer_manager.markDirty(0);
        buffer_manager.flushPage(0, "bootstrap");
    }

    void flushBootstrap(const BootstrapPage& bootstrap) {
        auto& page = buffer_manager.getPage(0);
        std::memset(page->page_data.get(), 0, PAGE_SIZE);
        std::memcpy(page->page_data.get(), &bootstrap, sizeof(BootstrapPage));
        buffer_manager.markDirty(0);
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
        for (size_t i = 0; i < page_ids.size(); i++) {
            if (!encoded.empty()) encoded += ",";
            PageID start = page_ids[i];
            PageID end = start;
            while (i + 1 < page_ids.size() && page_ids[i + 1] == end + 1) {
                end = page_ids[++i];
            }
            encoded += std::to_string(start);
            if (end != start) {
                encoded += "-" + std::to_string(end);
            }
        }
        return encoded;
    }

    static std::vector<PageID> decodePageIds(const std::string& encoded) {
        std::vector<PageID> page_ids;
        std::istringstream input(encoded);
        std::string token;
        while (std::getline(input, token, ',')) {
            auto dash = token.find('-');
            if (dash == std::string::npos) {
                page_ids.push_back(static_cast<PageID>(std::stoi(token)));
                continue;
            }
            PageID start = static_cast<PageID>(std::stoi(token.substr(0, dash)));
            PageID end = static_cast<PageID>(std::stoi(token.substr(dash + 1)));
            for (PageID page_id = start; page_id <= end; page_id++) {
                page_ids.push_back(page_id);
            }
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

    static TableSchema statsTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"column_id", INT},
            {"row_count", INT},
            {"page_count", INT},
            {"ndv", INT},
            {"has_int_range", INT},
            {"min_int", INT},
            {"max_int", INT}
        }};
    }

    static TableSchema statValuesTableSchema() {
        return TableSchema{{
            {"table_id", INT},
            {"column_id", INT},
            {"stat_kind", INT},
            {"value_text", STRING},
            {"lower_int", INT},
            {"upper_int", INT},
            {"count", INT}
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
        ensureStatsTable();
        ensureStatValuesTable();
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

    void ensureStatsTable() {
        auto cached = table_names_by_id.find(SYS_STATS_ID);
        if (cached != table_names_by_id.end()) {
            tables_by_name.at(cached->second).system_table = true;
            validateSchema("__stats", tables_by_name.at(cached->second).schema, statsTableSchema());
            return;
        }

        if (loadTableById(SYS_STATS_ID)) {
            auto& metadata = getTable(SYS_STATS_ID);
            metadata.system_table = true;
            validateSchema("__stats", metadata.schema, statsTableSchema());
            return;
        }

        PageID stats_page = buffer_manager.extend(SYS_STATS_ID, "system table create");
        TableMetadata stats_metadata{
            SYS_STATS_ID, "__stats", statsTableSchema(),
            {stats_page}, stats_page, stats_page, 0, true
        };
        auto& cached_metadata = cacheTable(std::move(stats_metadata));
        persistTableRecord(cached_metadata);
        persistColumns(cached_metadata);
    }

    void ensureStatValuesTable() {
        auto cached = table_names_by_id.find(SYS_STAT_VALUES_ID);
        if (cached != table_names_by_id.end()) {
            tables_by_name.at(cached->second).system_table = true;
            validateSchema(
                "__stat_values",
                tables_by_name.at(cached->second).schema,
                statValuesTableSchema()
            );
            return;
        }

        if (loadTableById(SYS_STAT_VALUES_ID)) {
            auto& metadata = getTable(SYS_STAT_VALUES_ID);
            metadata.system_table = true;
            validateSchema("__stat_values", metadata.schema, statValuesTableSchema());
            return;
        }

        PageID stats_page = buffer_manager.extend(SYS_STAT_VALUES_ID, "system table create");
        TableMetadata stats_metadata{
            SYS_STAT_VALUES_ID, "__stat_values", statValuesTableSchema(),
            {stats_page}, stats_page, stats_page, 0, true
        };
        auto& cached_metadata = cacheTable(std::move(stats_metadata));
        persistTableRecord(cached_metadata);
        persistColumns(cached_metadata);
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
                        buffer_manager.markDirty(page_id);
                        buffer_manager.flushPage(page_id, "catalog metadata");
                    } else {
                        buffer_manager.markDirty(page_id);
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

    void deleteRowsForTables(TableId system_table_id,
                             const std::set<TableId>& table_ids,
                             const std::string& tag) {
        auto& metadata = getTable(system_table_id);
        TableHeap heap(metadata, buffer_manager);
        size_t deleted = 0;

        for (PageID page_id : heap.getPageIds()) {
            auto& page = heap.getPage(page_id);
            char* page_buffer = page->page_data.get();
            Slot* slot_array = reinterpret_cast<Slot*>(page_buffer);
            bool page_changed = false;

            for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
                if (slot_array[slot_itr].empty) {
                    continue;
                }
                const char* tuple_data = page_buffer + slot_array[slot_itr].offset;
                std::istringstream input(
                std::string(tuple_data, slot_array[slot_itr].length)
                );
                auto tuple = Tuple::deserialize(input);
                if (!hasReadableFields(tuple, 1)) {
                    continue;
                }
                TableId table_id = static_cast<TableId>(tuple->fields[0]->asInt());
                if (table_ids.find(table_id) == table_ids.end()) {
                    continue;
                }
                page->deleteTuple(slot_itr);
                page_changed = true;
                deleted++;
            }

            if (page_changed) {
                buffer_manager.markDirty(page_id);
                buffer_manager.flushPage(page_id, tag);
            }
        }

        metadata.row_count = deleted > metadata.row_count
            ? 0
            : metadata.row_count - deleted;
    }

    void deleteStatsForTables(const std::set<TableId>& table_ids) {
        deleteRowsForTables(SYS_STATS_ID, table_ids, "catalog stats");
    }

    void deleteStatValuesForTables(const std::set<TableId>& table_ids) {
        deleteRowsForTables(SYS_STAT_VALUES_ID, table_ids, "catalog stat values");
    }

    void insertStatValue(TableId table_id,
                         int column_id,
                         int stat_kind,
                         const std::string& value_text,
                         int lower,
                         int upper,
                         size_t count) {
        auto tuple = std::make_unique<Tuple>();
        tuple->addField(std::make_unique<Field>(static_cast<int>(table_id)));
        tuple->addField(std::make_unique<Field>(column_id));
        tuple->addField(std::make_unique<Field>(stat_kind));
        tuple->addField(std::make_unique<Field>(
            value_text.empty() ? "__bucket__" : value_text
        ));
        tuple->addField(std::make_unique<Field>(lower));
        tuple->addField(std::make_unique<Field>(upper));
        tuple->addField(std::make_unique<Field>(static_cast<int>(count)));
        insertSystemTuple(SYS_STAT_VALUES_ID, std::move(tuple));
    }

    void persistStatValueRows(const PersistedColumnStats& record) {
        for (const auto& value : record.mcv_counts) {
            insertStatValue(
                record.table_id,
                record.column_id,
                STAT_KIND_MCV,
                value.first,
                0,
                0,
                value.second
            );
        }
        for (const auto& bucket : record.equi_width_buckets) {
            insertStatValue(
                record.table_id,
                record.column_id,
                STAT_KIND_EQUI_WIDTH,
                "",
                bucket.lower,
                bucket.upper,
                bucket.count
            );
        }
        for (const auto& bucket : record.equi_depth_buckets) {
            insertStatValue(
                record.table_id,
                record.column_id,
                STAT_KIND_EQUI_DEPTH,
                "",
                bucket.lower,
                bucket.upper,
                bucket.count
            );
        }
    }

    void loadStatValueRows(TableId table_id,
                           std::vector<PersistedColumnStats>& records) {
        std::map<int, PersistedColumnStats*> records_by_column;
        for (auto& record : records) {
            records_by_column[record.column_id] = &record;
        }

        auto& values_metadata = getTable(SYS_STAT_VALUES_ID);
        TableHeap values_heap(values_metadata, buffer_manager);
        for (auto& tuple : values_heap.readAllTuples()) {
            if (!hasReadableFields(tuple, 7)) {
                continue;
            }
            if (static_cast<TableId>(tuple->fields[0]->asInt()) != table_id) {
                continue;
            }
            int column_id = tuple->fields[1]->asInt();
            auto record_it = records_by_column.find(column_id);
            if (record_it == records_by_column.end()) {
                continue;
            }

            int stat_kind = tuple->fields[2]->asInt();
            auto* record = record_it->second;
            if (stat_kind == STAT_KIND_MCV) {
                record->mcv_counts[tuple->fields[3]->asString()] =
                    static_cast<size_t>(tuple->fields[6]->asInt());
            } else if (stat_kind == STAT_KIND_EQUI_WIDTH) {
                record->equi_width_buckets.push_back({
                    tuple->fields[4]->asInt(),
                    tuple->fields[5]->asInt(),
                    static_cast<size_t>(tuple->fields[6]->asInt())
                });
            } else if (stat_kind == STAT_KIND_EQUI_DEPTH) {
                record->equi_depth_buckets.push_back({
                    tuple->fields[4]->asInt(),
                    tuple->fields[5]->asInt(),
                    static_cast<size_t>(tuple->fields[6]->asInt())
                });
            }
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
        LSN clr_lsn = appendUndoClrRecord(
            current_txn_id,
            clr_prev_lsn,
            update
        );
        current_txn_last_lsn = clr_lsn;
        active_transaction_table[current_txn_id] = {TxnStatus::RUNNING, clr_lsn};
        std::cout << "  log: CLR txn " << current_txn_id
                  << " LSN " << clr_lsn
                  << " prevLSN " << clr_prev_lsn
                  << " undoNextLSN " << update.prev_lsn
                  << " for rollback to " << name << std::endl;
        applyUndoRecord(table, update, clr_lsn, "partial rollback undo");
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
              << undone << " page-change record(s); transaction remains active"
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
            LSN clr_lsn = appendUndoClrRecord(
                current_txn_id,
                clr_prev_lsn,
                *it
            );
            current_txn_last_lsn = clr_lsn;
            active_transaction_table[current_txn_id] = {TxnStatus::ABORTING, clr_lsn};
            std::cout << "  log: CLR txn " << current_txn_id
                      << " LSN " << clr_lsn
                      << " prevLSN " << clr_prev_lsn
                      << " undoNextLSN " << it->prev_lsn << std::endl;
            applyUndoRecord(table, *it, clr_lsn, "runtime abort undo");
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
                  << " page-change record(s), forced restored page(s), wrote CLR+ABORT logs" << std::endl;
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

void RecoveryManager::createFuzzyImageCopy() {
    MasterRecord master_record = log_manager.readMasterRecord();
    if (master_record.checkpoint_begin_lsn == 0) {
        throw std::runtime_error("IMAGE COPY requires an earlier checkpoint.");
    }

    LSN image_copy_lsn = txn_active ? current_txn_last_lsn : log_manager.getFlushedLSN();
    if (image_copy_lsn != 0) {
        forceLogUpTo(image_copy_lsn);
    }

    buffer_manager.createImageCopy();
    ImageCopyMetadata metadata{
        master_record.checkpoint_begin_lsn,
        master_record.checkpoint_begin_offset,
        image_copy_lsn,
        buffer_manager.getNumPages()
    };
    writeImageCopyMetadata(metadata);
    std::cout << "  media: fuzzy image copy saved "
              << metadata.page_count << " page(s)" << std::endl;
    std::cout << "  media: restore will start analysis from checkpoint LSN "
              << metadata.master_lsn
              << " offset " << metadata.master_offset
              << " and redo using WAL after image-copy LSN "
              << metadata.image_copy_lsn << std::endl;
}

void RecoveryManager::recover() {
    struct WalRecord {
        PageUpdateKind kind = PageUpdateKind::Update;
        LSN lsn = 0;
        LSN prev_lsn = 0;
        int txn_id = 0;
        TableId table_id = 0;
        PageID page_id = INVALID_VALUE;
        size_t slot_id = INVALID_VALUE;
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
                "buzzdb.log was written by an older WAL format; remove it before running v75."
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
                PageUpdateKind::Update,
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
        } else if (type == "INSERT") {
            int table_id;
            int page_id;
            size_t slot_id;
            input >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                PageUpdateKind::Insert,
                record_lsn,
                prev_lsn,
                txn_id,
                static_cast<TableId>(table_id),
                static_cast<PageID>(page_id),
                slot_id,
                false,
                0,
                nullptr,
                Tuple::deserialize(input)
            });
        } else if (type == "DELETE") {
            int table_id;
            int page_id;
            size_t slot_id;
            input >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                PageUpdateKind::Delete,
                record_lsn,
                prev_lsn,
                txn_id,
                static_cast<TableId>(table_id),
                static_cast<PageID>(page_id),
                slot_id,
                false,
                0,
                Tuple::deserialize(input),
                nullptr
            });
        } else if (type == "CLR") {
            LSN undo_next_lsn;
            int table_id;
            int page_id;
            size_t slot_id;
            input >> undo_next_lsn >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                PageUpdateKind::Update,
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
        } else if (type == "CLR_INSERT") {
            LSN undo_next_lsn;
            int table_id;
            int page_id;
            size_t slot_id;
            input >> undo_next_lsn >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                PageUpdateKind::Delete,
                record_lsn,
                prev_lsn,
                txn_id,
                static_cast<TableId>(table_id),
                static_cast<PageID>(page_id),
                slot_id,
                true,
                undo_next_lsn,
                nullptr,
                nullptr
            });
        } else if (type == "CLR_DELETE") {
            LSN undo_next_lsn;
            int table_id;
            int page_id;
            size_t slot_id;
            input >> undo_next_lsn >> table_id >> page_id >> slot_id;
            wal_records.push_back({
                PageUpdateKind::Insert,
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

    auto isPageChangeRecord = [](const std::string& type) {
        return type == "UPDATE" ||
               type == "INSERT" ||
               type == "DELETE" ||
               type == "CLR" ||
               type == "CLR_INSERT" ||
               type == "CLR_DELETE";
    };
    auto isClrRecord = [](const std::string& type) {
        return type == "CLR" ||
               type == "CLR_INSERT" ||
               type == "CLR_DELETE";
    };

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
                "buzzdb.log was written by an older WAL format; remove it before running v75."
            );
        }
        if (txn_id > 0) {
            next_txn_id = std::max(next_txn_id, txn_id + 1);
        }
        prev_lsn_by_lsn[record_lsn] = prev_lsn;
        bool in_analysis_scan = record_lsn >= analysis_start_lsn;
        if (isPageChangeRecord(type)) {
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
        } else if (isPageChangeRecord(type)) {
            int table_id;
            int page_id;
            size_t slot_id;
            if (isClrRecord(type)) {
                LSN undo_next_lsn = 0;
                input >> undo_next_lsn;
            }
            input >> table_id >> page_id >> slot_id;
            PageID dirty_page_id = static_cast<PageID>(page_id);
            if (in_analysis_scan) {
                if (restart_dirty_page_table.find(dirty_page_id) == restart_dirty_page_table.end()) {
                    restart_dirty_page_table[dirty_page_id] = record_lsn;
                }
                if (isClrRecord(type)) {
                    auto status = TxnStatus::RUNNING;
                    auto txn_entry = analysis_table.find(txn_id);
                    if (txn_entry != analysis_table.end()) {
                        status = txn_entry->second.status;
                    }
                    analysis_table[txn_id] = {status, record_lsn};
                } else {
                    analysis_table[txn_id] = {TxnStatus::RUNNING, record_lsn};
                }
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
        switch (record.kind) {
            case PageUpdateKind::Update:
                table.applyPhysiologicalUpdate(
                    record.page_id, record.slot_id,
                    record.after_tuple->clone(),
                    true, "restart redo", record.lsn
                );
                break;
            case PageUpdateKind::Insert:
                table.applyPhysiologicalInsert(
                    record.page_id, record.slot_id,
                    record.after_tuple->clone(),
                    true, "restart redo insert", record.lsn
                );
                break;
            case PageUpdateKind::Delete:
                table.applyPhysiologicalDelete(
                    record.page_id, record.slot_id,
                    true, "restart redo delete", record.lsn
                );
                break;
        }
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
            if (restart_undo_lock_callback) {
                // Early availability: recovery must lock rows before undoing them.
                bool resolved_deadlock = restart_undo_lock_callback(
                    record->second->table_id,
                    record->second->page_id,
                    record->second->slot_id
                );
                restart_undo_locks_reacquired++;
                if (resolved_deadlock) {
                    restart_undo_deadlocks_resolved++;
                }
            }
            PageUpdateLogRecord undo_record;
            undo_record.kind = record->second->kind;
            undo_record.lsn = record->second->lsn;
            undo_record.prev_lsn = record->second->prev_lsn;
            undo_record.table_id = record->second->table_id;
            undo_record.page_id = record->second->page_id;
            undo_record.slot_id = record->second->slot_id;
            if (record->second->before_tuple) {
                undo_record.before_tuple =
                    record->second->before_tuple->clone();
            }
            if (record->second->after_tuple) {
                undo_record.after_tuple =
                    record->second->after_tuple->clone();
            }
            LSN clr_lsn = appendUndoClrRecord(
                txn_id,
                loser->second.last_lsn,
                undo_record
            );
            std::cout << "  log: CLR txn " << txn_id
                      << " LSN " << clr_lsn
                      << " prevLSN " << loser->second.last_lsn
                      << " undoNextLSN " << record->second->prev_lsn
                      << " during restart" << std::endl;
            loser->second.last_lsn = clr_lsn;
            applyUndoRecord(table, undo_record, clr_lsn, "restart undo");
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

    if (restart_undo_release_callback) {
        restart_undo_release_callback();
    }

    restart_redo_records += redone;
    restart_undo_records += undone;
    dirty_page_table.clear();
    if (redone != 0 || undone != 0) {
        std::cout << "Restart recovery: redid " << redone
                  << " history record(s), undid " << undone
                  << " loser page-change record(s)." << std::endl;
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

LSN RecoveryManager::logInsert(TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> after_tuple) {
    if (!txn_active) {
        throw std::runtime_error("WAL recovery insert requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    if (!txn_logged) {
        throw std::runtime_error("WAL recovery insert requested without BEGIN log record.");
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

    auto after_image = after_tuple->serialize();
    after_image_bytes_logged += after_image.size();
    after_image_records_logged++;
    LSN insert_prev_lsn = current_txn_last_lsn;
    LSN insert_lsn = log_manager.append(
        "INSERT " + std::to_string(current_txn_id) + " " +
        std::to_string(insert_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        after_image
    );
    current_txn_last_lsn = insert_lsn;
    active_transaction_table[current_txn_id] = {TxnStatus::RUNNING, insert_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = insert_lsn;
    }
    std::cout << "  log: INSERT txn " << current_txn_id
              << " page " << page_id
              << " slot " << slot_id
              << " LSN " << insert_lsn
              << " prevLSN " << insert_prev_lsn << std::endl;

    PageUpdateLogRecord update;
    update.kind = PageUpdateKind::Insert;
    update.lsn = insert_lsn;
    update.prev_lsn = insert_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.after_tuple = std::move(after_tuple);
    page_update_log_records.push_back(std::move(update));
    std::cout << "  recovery: insert " << metadata.name
              << " page " << page_id
              << " slot " << slot_id
              << "; WAL logged after image at LSN "
              << insert_lsn << std::endl;
    return insert_lsn;
}

LSN RecoveryManager::logDelete(TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> before_tuple) {
    if (!txn_active) {
        throw std::runtime_error("WAL recovery delete requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    if (!txn_logged) {
        throw std::runtime_error("WAL recovery delete requested without BEGIN log record.");
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
    before_image_bytes_logged += before_image.size();
    before_image_records_logged++;
    LSN delete_prev_lsn = current_txn_last_lsn;
    LSN delete_lsn = log_manager.append(
        "DELETE " + std::to_string(current_txn_id) + " " +
        std::to_string(delete_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        before_image
    );
    current_txn_last_lsn = delete_lsn;
    active_transaction_table[current_txn_id] = {TxnStatus::RUNNING, delete_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = delete_lsn;
    }
    std::cout << "  log: DELETE txn " << current_txn_id
              << " page " << page_id
              << " slot " << slot_id
              << " LSN " << delete_lsn
              << " prevLSN " << delete_prev_lsn << std::endl;

    PageUpdateLogRecord update;
    update.kind = PageUpdateKind::Delete;
    update.lsn = delete_lsn;
    update.prev_lsn = delete_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.before_tuple = std::move(before_tuple);
    page_update_log_records.push_back(std::move(update));
    std::cout << "  recovery: delete " << metadata.name
              << " page " << page_id
              << " slot " << slot_id
              << "; WAL logged before image at LSN "
              << delete_lsn << std::endl;
    return delete_lsn;
}

void RecoveryManager::beginTxn(int txn_id) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    if (txn_states.find(txn_id) != txn_states.end()) {
        throw std::runtime_error("Recovery transaction already active.");
    }
    next_txn_id = std::max(next_txn_id, txn_id + 1);
    LSN begin_lsn = appendTxnRecord(txn_id, "BEGIN", 0);
    txn_states[txn_id].last_lsn = begin_lsn;
    active_transaction_table[txn_id] = {TxnStatus::RUNNING, begin_lsn};
    printThreadSafe(
        "  log: BEGIN txn " + std::to_string(txn_id) +
        " LSN " + std::to_string(begin_lsn) + " prevLSN 0"
    );
}

bool RecoveryManager::hasTxn(int txn_id) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    return txn_states.find(txn_id) != txn_states.end();
}

LSN RecoveryManager::logUpdate(int txn_id,
                                TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> before_tuple,
                                std::unique_ptr<Tuple> after_tuple) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        throw std::runtime_error("WAL update requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    auto before_image = before_tuple->serialize();
    auto after_image = after_tuple->serialize();
    before_image_bytes_logged += before_image.size();
    after_image_bytes_logged += after_image.size();
    before_image_records_logged++;
    after_image_records_logged++;
    LSN update_prev_lsn = txn_state->second.last_lsn;
    LSN update_lsn = log_manager.append(
        "UPDATE " + std::to_string(txn_id) + " " +
        std::to_string(update_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        before_image + after_image
    );
    txn_state->second.last_lsn = update_lsn;
    active_transaction_table[txn_id] = {TxnStatus::RUNNING, update_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = update_lsn;
    }

    PageUpdateLogRecord update;
    update.lsn = update_lsn;
    update.prev_lsn = update_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.before_tuple = std::move(before_tuple);
    update.after_tuple = std::move(after_tuple);
    txn_state->second.page_update_log_records.push_back(std::move(update));

    printThreadSafe(
        "  log: UPDATE txn " + std::to_string(txn_id) +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        " LSN " + std::to_string(update_lsn) +
        " prevLSN " + std::to_string(update_prev_lsn)
    );
    printThreadSafe(
        "  recovery: update " + metadata.name +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        " in place; WAL logged before+after image at LSN " +
        std::to_string(update_lsn)
    );
    return update_lsn;
}

LSN RecoveryManager::logInsert(int txn_id,
                                TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> after_tuple) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        throw std::runtime_error("WAL insert requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    auto after_image = after_tuple->serialize();
    after_image_bytes_logged += after_image.size();
    after_image_records_logged++;
    LSN insert_prev_lsn = txn_state->second.last_lsn;
    LSN insert_lsn = log_manager.append(
        "INSERT " + std::to_string(txn_id) + " " +
        std::to_string(insert_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        after_image
    );
    txn_state->second.last_lsn = insert_lsn;
    active_transaction_table[txn_id] = {TxnStatus::RUNNING, insert_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = insert_lsn;
    }

    PageUpdateLogRecord update;
    update.kind = PageUpdateKind::Insert;
    update.lsn = insert_lsn;
    update.prev_lsn = insert_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.after_tuple = std::move(after_tuple);
    txn_state->second.page_update_log_records.push_back(std::move(update));

    printThreadSafe(
        "  log: INSERT txn " + std::to_string(txn_id) +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        " LSN " + std::to_string(insert_lsn) +
        " prevLSN " + std::to_string(insert_prev_lsn)
    );
    printThreadSafe(
        "  recovery: insert " + metadata.name +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        "; WAL logged after image at LSN " +
        std::to_string(insert_lsn)
    );
    return insert_lsn;
}

LSN RecoveryManager::logDelete(int txn_id,
                                TableId table_id,
                                PageID page_id,
                                size_t slot_id,
                                std::unique_ptr<Tuple> before_tuple) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        throw std::runtime_error("WAL delete requested without BEGIN.");
    }

    auto& metadata = catalog.getTable(table_id);
    auto before_image = before_tuple->serialize();
    before_image_bytes_logged += before_image.size();
    before_image_records_logged++;
    LSN delete_prev_lsn = txn_state->second.last_lsn;
    LSN delete_lsn = log_manager.append(
        "DELETE " + std::to_string(txn_id) + " " +
        std::to_string(delete_prev_lsn) + " " +
        std::to_string(table_id) + " " +
        std::to_string(page_id) + " " +
        std::to_string(slot_id) + " " +
        before_image
    );
    txn_state->second.last_lsn = delete_lsn;
    active_transaction_table[txn_id] = {TxnStatus::RUNNING, delete_lsn};
    if (dirty_page_table.find(page_id) == dirty_page_table.end()) {
        dirty_page_table[page_id] = delete_lsn;
    }

    PageUpdateLogRecord update;
    update.kind = PageUpdateKind::Delete;
    update.lsn = delete_lsn;
    update.prev_lsn = delete_prev_lsn;
    update.table_id = table_id;
    update.page_id = page_id;
    update.slot_id = slot_id;
    update.before_tuple = std::move(before_tuple);
    txn_state->second.page_update_log_records.push_back(std::move(update));

    printThreadSafe(
        "  log: DELETE txn " + std::to_string(txn_id) +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        " LSN " + std::to_string(delete_lsn) +
        " prevLSN " + std::to_string(delete_prev_lsn)
    );
    printThreadSafe(
        "  recovery: delete " + metadata.name +
        " page " + std::to_string(page_id) +
        " slot " + std::to_string(slot_id) +
        "; WAL logged before image at LSN " +
        std::to_string(delete_lsn)
    );
    return delete_lsn;
}

LSN RecoveryManager::queueCommit(int txn_id) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        throw std::runtime_error("COMMIT requested for inactive recovery transaction.");
    }
    LSN commit_prev_lsn = txn_state->second.last_lsn;
    LSN commit_lsn = appendTxnRecord(txn_id, "COMMIT", commit_prev_lsn);
    txn_state->second.last_lsn = commit_lsn;
    active_transaction_table[txn_id] = {TxnStatus::COMMITTING, commit_lsn};
    committed_update_records += txn_state->second.page_update_log_records.size();
    printThreadSafe(
        "  log: COMMIT txn " + std::to_string(txn_id) +
        " LSN " + std::to_string(commit_lsn) +
        " prevLSN " + std::to_string(commit_prev_lsn)
    );
    return commit_lsn;
}

void RecoveryManager::abortTxn(int txn_id) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        return;
    }

    auto& updates = txn_state->second.page_update_log_records;
    if (!updates.empty()) {
        aborted_update_records += updates.size();
        for (auto it = updates.rbegin(); it != updates.rend(); ++it) {
            auto& metadata = catalog.getTable(it->table_id);
            TableHeap table(metadata, buffer_manager);
            LSN clr_prev_lsn = txn_state->second.last_lsn;
            LSN clr_lsn = appendUndoClrRecord(
                txn_id,
                clr_prev_lsn,
                *it
            );
            txn_state->second.last_lsn = clr_lsn;
            active_transaction_table[txn_id] = {TxnStatus::ABORTING, clr_lsn};
            printThreadSafe(
                "  log: CLR txn " + std::to_string(txn_id) +
                " LSN " + std::to_string(clr_lsn) +
                " prevLSN " + std::to_string(clr_prev_lsn) +
                " undoNextLSN " + std::to_string(it->prev_lsn)
            );
            applyUndoRecord(table, *it, clr_lsn, "runtime abort undo");
            runtime_undo_records++;
        }
    }

    LSN abort_prev_lsn = txn_state->second.last_lsn;
    LSN abort_lsn = appendTxnRecord(txn_id, "ABORT", abort_prev_lsn);
    txn_state->second.last_lsn = abort_lsn;
    active_transaction_table[txn_id] = {TxnStatus::ABORTING, abort_lsn};
    printThreadSafe(
        "  log: ABORT txn " + std::to_string(txn_id) +
        " LSN " + std::to_string(abort_lsn) +
        " prevLSN " + std::to_string(abort_prev_lsn)
    );
    forceLogUpTo(abort_lsn);
    abort_log_records++;
    printThreadSafe(
        "  recovery: abort restored " + std::to_string(updates.size()) +
        " page-change record(s); locks can now be released"
    );
}

size_t RecoveryManager::forceCommitGroupUpTo(LSN max_commit_lsn) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto forces_before = log_manager.getStableLogForces();
    forceLogUpTo(max_commit_lsn);
    commit_log_forces++;
    return log_manager.getStableLogForces() - forces_before;
}

void RecoveryManager::finishTxn(int txn_id) {
    std::lock_guard<std::mutex> guard(recovery_latch);
    auto txn_state = txn_states.find(txn_id);
    if (txn_state == txn_states.end()) {
        return;
    }
    LSN end_prev_lsn = txn_state->second.last_lsn;
    LSN end_lsn = appendTxnRecord(txn_id, "END", end_prev_lsn);
    active_transaction_table.erase(txn_id);
    txn_states.erase(txn_state);
    printThreadSafe(
        "  log: END txn " + std::to_string(txn_id) +
        " LSN " + std::to_string(end_lsn) +
        " prevLSN " + std::to_string(end_prev_lsn) +
        " (not forced for commit return)"
    );
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

// -----------------------------------------------------------------------------
// Transaction and Operator Context 
// -----------------------------------------------------------------------------

struct TxnContext {
    int id = 0;
    std::string label;
    enum State { RUNNING, COMMITTED, ABORTED } state = RUNNING;
    std::vector<TupleId> inserted_tuple_ids;
};

using TxnPtr = std::shared_ptr<TxnContext>;

class TransactionManager {
    int next_id = 1;
    std::mutex latch;
public:
    TxnPtr begin(const std::string& label = "") {
        std::lock_guard<std::mutex> guard(latch);
        int txn_id = next_id++;
        auto txn_label = label.empty() ?
            "T" + std::to_string(txn_id) :
            label;
        printThreadSafe(txn_label + " BEGIN");
        return std::make_shared<TxnContext>(
            TxnContext{txn_id, txn_label, TxnContext::RUNNING, {}}
        );
    }
    void commit(TxnContext& tx) {
        std::lock_guard<std::mutex> guard(latch);
        tx.state = TxnContext::COMMITTED;
    }
    void abort (TxnContext& tx) {
        std::lock_guard<std::mutex> guard(latch);
        tx.state = TxnContext::ABORTED;
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

enum class LockMode { S, X };

class LockManager {
    struct LockGrant {
        int txn_id;
        LockMode mode;
    };

    struct LockState {
        std::vector<LockGrant> grants;
    };

public:
    struct LockRequestResult {
        bool granted = false;
        bool deadlock = false;
        bool timed_out = false;
        bool canceled = false;
        std::string reason;
        std::vector<int> blockers;
        std::vector<int> cycle;
    };

    void setWaitObserver(
        std::function<void(int, const std::vector<int>&)> observer) {
        wait_observer = std::move(observer);
    }

    static std::string modeName(LockMode mode) {
        switch (mode) {
            case LockMode::S:
                return "S";
            case LockMode::X:
                return "X";
        }
        throw std::runtime_error("Unknown lock mode.");
    }

    LockRequestResult waitFor(int txn_id,
                              const std::string& resource,
                              LockMode mode,
                              std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> guard(latch);
        LockRequestResult result;
        if (canceled_txns.find(txn_id) != canceled_txns.end()) {
            result.canceled = true;
            result.reason = "transaction was canceled to resolve a deadlock";
            return result;
        }
        while (!canGrant(txn_id, resource, mode)) {
            if (canceled_txns.find(txn_id) != canceled_txns.end()) {
                result.canceled = true;
                result.reason = "transaction was canceled to resolve a deadlock";
                removeWaitEdges(txn_id);
                return result;
            }
            auto blockers = blockersFor(txn_id, resource, mode);
            setWaitEdges(txn_id, blockers);
            notifyWaitObserver(txn_id, blockers);
            result.blockers = blockers;
            result.cycle = findCycleFrom(txn_id);
            if (!result.cycle.empty()) {
                result.deadlock = true;
                result.reason = "waits-for cycle detected";
                removeWaitEdges(txn_id);
                return result;
            }

            if (lock_cv.wait_for(guard, timeout) == std::cv_status::timeout) {
                result.timed_out = true;
                result.reason = incompatibleHolderLabel(
                    locks[resource], txn_id, mode
                );
                removeWaitEdges(txn_id);
                return result;
            }
        }

        removeWaitEdges(txn_id);
        grantLock(locks[resource], txn_id, mode);
        result.granted = true;
        return result;
    }

    bool cancel(int txn_id) {
        std::lock_guard<std::mutex> guard(latch);
        bool released = false;
        canceled_txns.insert(txn_id);
        removeWaitEdges(txn_id);
        for (auto& entry : locks) {
            auto& state = entry.second;
            auto old_size = state.grants.size();
            state.grants.erase(
                std::remove_if(state.grants.begin(),
                               state.grants.end(),
                               [txn_id](const LockGrant& grant) {
                                   return grant.txn_id == txn_id;
                               }),
                state.grants.end()
            );
            released = released || old_size != state.grants.size();
        }
        lock_cv.notify_all();
        return released;
    }

    bool releaseAll(int txn_id) {
        std::lock_guard<std::mutex> guard(latch);
        bool released = false;
        removeWaitEdges(txn_id);
        for (auto& entry : locks) {
            auto& state = entry.second;
            auto old_size = state.grants.size();
            state.grants.erase(
                std::remove_if(state.grants.begin(),
                               state.grants.end(),
                               [txn_id](const LockGrant& grant) {
                                   return grant.txn_id == txn_id;
                               }),
                state.grants.end()
            );
            released = released || old_size != state.grants.size();
        }
        if (released) {
            lock_cv.notify_all();
        }
        return released;
    }

private:
    std::mutex latch;
    std::condition_variable lock_cv;
    std::map<std::string, LockState> locks;
    DirectedGraph waits_for;
    std::set<int> canceled_txns;
    std::function<void(int, const std::vector<int>&)> wait_observer;

    bool canGrant(int txn_id, const std::string& resource, LockMode mode) {
        auto& state = locks[resource];
        return std::all_of(
            state.grants.begin(),
            state.grants.end(),
            [txn_id, mode](const LockGrant& grant) {
                return grant.txn_id == txn_id ||
                       compatible(mode, grant.mode);
            }
        );
    }

    std::vector<int> blockersFor(int txn_id,
                                 const std::string& resource,
                                 LockMode mode) {
        std::vector<int> blockers;
        auto& state = locks[resource];
        for (const auto& grant : state.grants) {
            if (grant.txn_id != txn_id &&
                !compatible(mode, grant.mode) &&
                std::find(blockers.begin(), blockers.end(), grant.txn_id) ==
                    blockers.end()) {
                blockers.push_back(grant.txn_id);
            }
        }
        return blockers;
    }

    void setWaitEdges(int txn_id, const std::vector<int>& blockers) {
        waits_for.setOutgoingEdges(txn_id, blockers);
    }

    void notifyWaitObserver(int txn_id, const std::vector<int>& blockers) {
        if (wait_observer) {
            wait_observer(txn_id, blockers);
        }
    }

    void removeWaitEdges(int txn_id) {
        waits_for.removeNode(txn_id);
    }

    std::vector<int> findCycleFrom(int txn_id) const {
        return waits_for.cycleFrom(txn_id);
    }

    static bool compatible(LockMode requested, LockMode held) {
        if (requested == LockMode::S) {
            return held == LockMode::S;
        }
        return false;
    }

    static bool strongerOrEqual(LockMode held, LockMode requested) {
        if (held == requested || held == LockMode::X) {
            return true;
        }
        return false;
    }

    static void grantLock(LockState& state, int txn_id, LockMode mode) {
        for (const auto& grant : state.grants) {
            if (grant.txn_id == txn_id &&
                strongerOrEqual(grant.mode, mode)) {
                return;
            }
        }

        state.grants.erase(
            std::remove_if(state.grants.begin(),
                           state.grants.end(),
                           [txn_id, mode](const LockGrant& grant) {
                               return grant.txn_id == txn_id &&
                                      strongerOrEqual(mode, grant.mode);
                           }),
            state.grants.end()
        );
        state.grants.push_back({txn_id, mode});
    }

    static std::string incompatibleHolderLabel(const LockState& state,
                                               int txn_id,
                                               LockMode mode) {
        std::string label;
        for (const auto& grant : state.grants) {
            if (grant.txn_id == txn_id || compatible(mode, grant.mode)) {
                continue;
            }
            if (!label.empty()) {
                label += ", ";
            }
            label += modeName(grant.mode) + " lock is held by T" +
                     std::to_string(grant.txn_id);
        }
        return label.empty() ? "lock is not compatible" : label;
    }
};

enum class AccessType { Read, Write };

struct ConcurrencyControlResource {
    std::string key;
    std::string label;
};

struct ConcurrencyControlRequest {
    int txn_id = 0;
    std::string txn_label;
    AccessType type = AccessType::Read;
    std::vector<ConcurrencyControlResource> resources;
    std::string reason;
};

struct ConcurrencyControlResult {
    bool granted = true;
    bool deadlock = false;
    bool canceled = false;
    std::string reason;
    std::vector<int> cycle;
};

class ConcurrencyControlPolicy {
public:
    virtual ~ConcurrencyControlPolicy() = default;
    virtual void begin(int txn_id, const std::string& txn_label) = 0;
    virtual ConcurrencyControlResult beforeAccess(
        const ConcurrencyControlRequest& request) = 0;
    virtual void commit(int txn_id) = 0;
    virtual void abort(int txn_id) = 0;
    virtual bool cancel(int txn_id) = 0;
};

class TwoPhaseLockingPolicy : public ConcurrencyControlPolicy {
    LockManager& lock_manager;
    std::function<void(const std::string&)> log;
    std::function<std::string(const std::vector<int>&)> path_label;

    static LockMode modeFor(AccessType type) {
        return type == AccessType::Read ? LockMode::S : LockMode::X;
    }

public:
    TwoPhaseLockingPolicy(
        LockManager& lock_manager,
        std::function<void(const std::string&)> log,
        std::function<std::string(const std::vector<int>&)> path_label)
        : lock_manager(lock_manager),
          log(std::move(log)),
          path_label(std::move(path_label)) {}

    ConcurrencyControlResult beforeAccess(
        const ConcurrencyControlRequest& request) override {
        LockMode mode = modeFor(request.type);
        auto mode_name = LockManager::modeName(mode);
        for (const auto& resource : request.resources) {
            log(request.txn_label + " inferred " + mode_name +
                " lock on " + resource.label + " for " + request.reason);
            auto result = lock_manager.waitFor(
                request.txn_id,
                resource.key,
                mode,
                std::chrono::milliseconds(500)
            );

            if (result.granted) {
                continue;
            }
            if (result.deadlock) {
                return {false, true, false,
                        "waits-for graph cycle: " + path_label(result.cycle),
                        result.cycle};
            }
            if (result.canceled) {
                return {false, false, true, result.reason, result.cycle};
            }
            return {false, false, false, result.reason, result.cycle};
        }
        return {};
    }

    void begin(int txn_id, const std::string& txn_label) override {
        (void)txn_id;
        (void)txn_label;
    }

    void commit(int txn_id) override {
        lock_manager.releaseAll(txn_id);
    }

    void abort(int txn_id) override {
        lock_manager.releaseAll(txn_id);
    }

    bool cancel(int txn_id) override {
        return lock_manager.cancel(txn_id);
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

    /// This returns the generated tuple. When `next()` returns true, the
    /// referenced Tuple will contain the values for the next tuple. The
    /// reference is borrowed and is only valid until next()/close().
    virtual const Tuple& getOutput() const = 0;

    virtual std::optional<TupleId> getTupleId() const {
        return std::nullopt;
    }

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

static const Field& tupleField(const Tuple& tuple, size_t index) {
    if (index >= tuple.fields.size()) {
        throw std::runtime_error("Tuple field index out of range.");
    }
    return *tuple.fields[index];
}

static void clearTuple(Tuple& tuple) {
    tuple.fields.clear();
}

static void appendClonedFields(Tuple& dest, const Tuple& src) {
    for (const auto& field : src.fields) {
        dest.addField(field->clone());
    }
}

static void appendProjectedFields(Tuple& dest,
                                  const Tuple& src,
                                  const std::vector<size_t>& attrs) {
    for (size_t attr_index : attrs) {
        if (attr_index >= src.fields.size()) {
            throw std::runtime_error("Projection attribute index out of range.");
        }
        dest.addField(src.fields[attr_index]->clone());
    }
}

static void makeConcatenatedTuple(Tuple& dest,
                                  const Tuple& left,
                                  const Tuple& right) {
    clearTuple(dest);
    appendClonedFields(dest, left);
    appendClonedFields(dest, right);
}

std::vector<std::unique_ptr<Field>> cloneTupleFields(const Tuple& tuple) {
    std::vector<std::unique_ptr<Field>> cloned;
    cloned.reserve(tuple.fields.size());
    for (const auto& field : tuple.fields) {
        cloned.push_back(field->clone());
    }
    return cloned;
}

class ScanOperator : public Operator {
private:
    TableHeap& tableHeap;
    size_t currentPageIndex = 0;
    size_t currentSlotIndex = 0;
    std::unique_ptr<Tuple> currentTuple;
    std::optional<TupleId> currentTupleId;
    size_t tuple_count = 0;

public:
    ScanOperator(TableHeap& table) : tableHeap(table) {}

    void open() override {
        currentPageIndex = 0;
        currentSlotIndex = 0;
        tuple_count = 0;
        currentTuple.reset(); // Ensure currentTuple is reset
        currentTupleId.reset();
    }

    bool next() override {
        loadNextTuple();
        return currentTuple != nullptr;
    }

    void close() override {
        currentPageIndex = 0;
        currentSlotIndex = 0;
        currentTuple.reset();
        currentTupleId.reset();
    }

    const Tuple& getOutput() const override {
        if (!currentTuple) {
            throw std::runtime_error("ScanOperator::getOutput called without a current tuple.");
        }
        return *currentTuple;
    }

    std::optional<TupleId> getTupleId() const override {
        return currentTupleId;
    }

private:
    void loadNextTuple() {
        const auto& page_ids = tableHeap.getPageIds();
        while (currentPageIndex < page_ids.size()) {
            auto& currentPage = tableHeap.getPage(page_ids[currentPageIndex]);
            if (!currentPage) {
                currentSlotIndex = 0;
                currentPageIndex++;
                continue;
            }
            if (currentSlotIndex >= MAX_SLOTS) {
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
                    currentTupleId = TupleId{
                        tableHeap.getTableId(),
                        page_ids[currentPageIndex],
                        currentSlotIndex
                    };
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
        currentTupleId.reset();
    }
};

class IPredicate {
public:
    virtual ~IPredicate() = default;
    virtual bool check(const Tuple& tuple) const = 0;
};

class SimplePredicate : public IPredicate {
public:
    enum OperandType { DIRECT, INDIRECT };
    enum ComparisonOperator { EQ, NE, GT, GE, LT, LE };

    struct Operand {
        std::unique_ptr<Field> directValue;
        size_t index = 0;
        OperandType type;

        explicit Operand(std::unique_ptr<Field> value)
            : directValue(std::move(value)), type(DIRECT) {}
        explicit Operand(size_t idx)
            : index(idx), type(INDIRECT) {}
    };

    Operand left_operand;
    Operand right_operand;
    ComparisonOperator comparison_operator;

    SimplePredicate(Operand left, Operand right, ComparisonOperator op)
        : left_operand(std::move(left)),
          right_operand(std::move(right)),
          comparison_operator(op) {}

    bool check(const Tuple& tuple) const override {
        const Field* leftField = resolveOperand(left_operand, tuple);
        const Field* rightField = resolveOperand(right_operand, tuple);

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
            case FieldType::INT:
                return compare(leftField->asInt(), rightField->asInt());
            case FieldType::FLOAT:
                return compare(leftField->asFloat(), rightField->asFloat());
            case FieldType::STRING:
                return compare(leftField->asString(), rightField->asString());
        }
        return false;
    }

private:
    static const Field* resolveOperand(const Operand& operand, const Tuple& tuple) {
        if (operand.type == DIRECT) {
            return operand.directValue.get();
        }
        if (operand.index >= tuple.fields.size()) {
            return nullptr;
        }
        return tuple.fields[operand.index].get();
    }

    // Compares two values of the same type
    template <typename T>
    bool compare(const T& left_val, const T& right_val) const {
        switch (comparison_operator) {
            case ComparisonOperator::EQ: return left_val == right_val;
            case ComparisonOperator::NE: return left_val != right_val;
            case ComparisonOperator::GT: return left_val > right_val;
            case ComparisonOperator::GE: return left_val >= right_val;
            case ComparisonOperator::LT: return left_val < right_val;
            case ComparisonOperator::LE: return left_val <= right_val;
        }
        return false;
    }
};

class ComplexPredicate : public IPredicate {
public:
    enum LogicOperator { AND, OR };

private:
    std::vector<std::unique_ptr<IPredicate>> predicates;
    LogicOperator logic_operator;

public:
    explicit ComplexPredicate(LogicOperator op) : logic_operator(op) {}

    void addPredicate(std::unique_ptr<IPredicate> predicate) {
        predicates.push_back(std::move(predicate));
    }

    bool check(const Tuple& tuple) const override {
        if (logic_operator == AND) {
            for (const auto& pred : predicates) {
                if (!pred->check(tuple)) {
                    return false; // If any predicate fails, the AND condition fails
                }
            }
            return true; // All predicates passed
        }

        if (logic_operator == OR) {
            for (const auto& pred : predicates) {
                if (pred->check(tuple)) {
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
    const Tuple* currentOutput = nullptr;  // borrowed from child
    std::optional<TupleId> currentTupleId;

public:
    SelectOperator(Operator& input, std::unique_ptr<IPredicate> predicate)
        : UnaryOperator(input), predicate(std::move(predicate)) {}

    void open() override {
        input->setTxnContext(txn_);
        input->open();
        currentOutput = nullptr;
        currentTupleId.reset();
    }

    bool next() override {
        while (input->next()) {
            const Tuple& output = input->getOutput();
            if (predicate->check(output)) {
                // If the predicate is satisfied, store the output in the member variable
                currentOutput = &output;
                currentTupleId = input->getTupleId();
                return true;
            }
        }

        currentOutput = nullptr;
        currentTupleId.reset();
        return false;
    }

    void close() override {
        input->close();
        currentOutput = nullptr;
        currentTupleId.reset();
    }

    const Tuple& getOutput() const override {
        if (!currentOutput) {
            throw std::runtime_error("SelectOperator::getOutput called without a current tuple.");
        }
        return *currentOutput;
    }

    std::optional<TupleId> getTupleId() const override {
        return currentOutput ? currentTupleId : std::nullopt;
    }
};

class ProjectionOperator : public UnaryOperator {
private:
    std::vector<size_t> projected_attrs;
    Tuple currentOutput;
    std::optional<TupleId> currentTupleId;
    bool has_next = false;

public:
    ProjectionOperator(Operator& input, std::vector<size_t> projected_attrs)
        : UnaryOperator(input), projected_attrs(std::move(projected_attrs)) {}

    void open() override {
        input->setTxnContext(txn_);
        input->open();
        clearTuple(currentOutput);
        currentTupleId.reset();
        has_next = false;
    }

    bool next() override {
        if (!input->next()) {
            clearTuple(currentOutput);
            currentTupleId.reset();
            has_next = false;
            return false;
        }

        const Tuple& input_tuple = input->getOutput();
        currentTupleId = input->getTupleId();
        clearTuple(currentOutput);
        appendProjectedFields(currentOutput, input_tuple, projected_attrs);
        has_next = true;
        return true;
    }

    void close() override {
        input->close();
        clearTuple(currentOutput);
        currentTupleId.reset();
        has_next = false;
    }

    const Tuple& getOutput() const override {
        if (!has_next) {
            throw std::runtime_error("ProjectionOperator::getOutput called without a current tuple.");
        }
        return currentOutput;
    }

    std::optional<TupleId> getTupleId() const override {
        return has_next ? currentTupleId : std::nullopt;
    }
};

enum class PhysicalJoinKind {
    NestedLoopJoin,
    HashJoin,
    SortMergeJoin
};

std::string physicalJoinKindName(PhysicalJoinKind kind) {
    switch (kind) {
        case PhysicalJoinKind::NestedLoopJoin:
            return "NestedLoopJoin";
        case PhysicalJoinKind::HashJoin:
            return "HashJoin";
        case PhysicalJoinKind::SortMergeJoin:
            return "SortMergeJoin";
    }
    return "HashJoin";
}

int compareFields(const Field& lhs, const Field& rhs) {
    if (lhs.getType() != rhs.getType()) {
        throw std::runtime_error("Cannot compare fields of different types.");
    }

    switch (lhs.getType()) {
        case INT:
            if (lhs.asInt() < rhs.asInt()) return -1;
            if (lhs.asInt() > rhs.asInt()) return 1;
            return 0;
        case FLOAT:
            if (lhs.asFloat() < rhs.asFloat()) return -1;
            if (lhs.asFloat() > rhs.asFloat()) return 1;
            return 0;
        case STRING:
            if (lhs.asString() < rhs.asString()) return -1;
            if (lhs.asString() > rhs.asString()) return 1;
            return 0;
    }

    throw std::runtime_error("Unsupported field type for comparison.");
}

class SortOperator : public UnaryOperator {
private:
    std::vector<size_t> sort_attrs;
    std::vector<std::unique_ptr<Tuple>> tuples;
    const Tuple* currentOutput = nullptr;
    size_t output_index = 0;

public:
    SortOperator(Operator& input, std::vector<size_t> sort_attrs)
        : UnaryOperator(input), sort_attrs(std::move(sort_attrs)) {}

    void open() override {
        input->setTxnContext(txn_);
        input->open();
        tuples.clear();

        while (input->next()) {
            tuples.push_back(input->getOutput().clone());
        }

        std::stable_sort(tuples.begin(), tuples.end(),
                         [&](const auto& left, const auto& right) {
                             for (size_t attr_index : sort_attrs) {
                                 int cmp = compareFields(
                                     tupleField(*left, attr_index),
                                     tupleField(*right, attr_index)
                                 );
                                 if (cmp != 0) {
                                     return cmp < 0;
                                 }
                             }
                             return false;
                         });

        output_index = 0;
        currentOutput = nullptr;
    }

    bool next() override {
        if (output_index >= tuples.size()) {
            currentOutput = nullptr;
            return false;
        }
        currentOutput = tuples[output_index++].get();
        return true;
    }

    void close() override {
        input->close();
        tuples.clear();
        output_index = 0;
        currentOutput = nullptr;
    }

    const Tuple& getOutput() const override {
        if (!currentOutput) {
            throw std::runtime_error("SortOperator::getOutput called without a current tuple.");
        }
        return *currentOutput;
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
        std::vector<std::unique_ptr<Tuple>>,
        HashJoinKeyHasher
    > hashTable;

    const Tuple* currentLeftTuple = nullptr;  // borrowed from left child
    Tuple currentOutput;
    const std::vector<std::unique_ptr<Tuple>>* matchingRightTuples = nullptr;
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
            const Tuple& right_tuple = input_right->getOutput();
            const Field& key_field = tupleField(right_tuple, right_attr_index);
            hashTable[HashJoinKey(key_field)].push_back(right_tuple.clone());
        }
        input_right->close();

        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        matchingRightTuples = nullptr;
        matchingRightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    bool next() override {
        clearTuple(currentOutput);
        has_next = false;

        while (true) {
            while (!has_left_tuple ||
                   matchingRightTuples == nullptr ||
                   matchingRightTupleIndex >= matchingRightTuples->size()) {
                if (!input_left->next()) {
                    currentLeftTuple = nullptr;
                    return false;
                }

                currentLeftTuple = &input_left->getOutput();
                const Field& left_key = tupleField(*currentLeftTuple, left_attr_index);
                auto matches = hashTable.find(HashJoinKey(left_key));

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

            const Tuple& right_tuple = *(*matchingRightTuples)[matchingRightTupleIndex++];
            makeConcatenatedTuple(currentOutput, *currentLeftTuple, right_tuple);
            has_next = true;
            return true;
        }
    }

    void close() override {
        input_left->close();
        hashTable.clear();
        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        matchingRightTuples = nullptr;
        matchingRightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    const Tuple& getOutput() const override {
        if (!has_next) {
            throw std::runtime_error("HashJoinOperator::getOutput called without a current tuple.");
        }
        return currentOutput;
    }
};

class SortMergeJoinOperator : public BinaryOperator {
private:
    size_t left_attr_index;
    size_t right_attr_index;
    std::vector<std::unique_ptr<Tuple>> rightTuples;
    const Tuple* currentLeftTuple = nullptr;  // borrowed from left child
    Tuple currentOutput;
    size_t rightCursor = 0;
    size_t matchingRightTupleIndex = 0;
    size_t matchingRightTupleEnd = 0;
    bool has_match_group = false;
    bool has_next = false;

public:
    SortMergeJoinOperator(Operator& left, Operator& right,
                          size_t left_attr_index,
                          size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->setTxnContext(txn_);
        input_right->setTxnContext(txn_);
        input_left->open();
        input_right->open();

        rightTuples.clear();
        while (input_right->next()) {
            rightTuples.push_back(input_right->getOutput().clone());
        }
        input_right->close();

        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        rightCursor = 0;
        matchingRightTupleIndex = 0;
        matchingRightTupleEnd = 0;
        has_match_group = false;
        has_next = false;
    }

    bool next() override {
        clearTuple(currentOutput);
        has_next = false;

        while (true) {
            if (has_match_group && matchingRightTupleIndex < matchingRightTupleEnd) {
                const Tuple& right_tuple = *rightTuples[matchingRightTupleIndex++];
                makeConcatenatedTuple(currentOutput, *currentLeftTuple, right_tuple);
                has_next = true;
                return true;
            }

            if (!input_left->next()) {
                currentLeftTuple = nullptr;
                return false;
            }

            currentLeftTuple = &input_left->getOutput();
            has_match_group = false;
            const Field& left_key = tupleField(*currentLeftTuple, left_attr_index);

            while (rightCursor < rightTuples.size()) {
                const Tuple& right_tuple = *rightTuples[rightCursor];
                if (compareFields(tupleField(right_tuple, right_attr_index), left_key) >= 0) {
                    break;
                }
                rightCursor++;
            }

            size_t group_end = rightCursor;
            while (group_end < rightTuples.size() &&
                   compareFields(tupleField(*rightTuples[group_end], right_attr_index), left_key) == 0) {
                group_end++;
            }

            if (group_end == rightCursor) {
                continue;
            }

            matchingRightTupleIndex = rightCursor;
            matchingRightTupleEnd = group_end;
            has_match_group = true;
        }
    }

    void close() override {
        input_left->close();
        rightTuples.clear();
        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        rightCursor = 0;
        matchingRightTupleIndex = 0;
        matchingRightTupleEnd = 0;
        has_match_group = false;
        has_next = false;
    }

    const Tuple& getOutput() const override {
        if (!has_next) {
            throw std::runtime_error("SortMergeJoinOperator::getOutput called without a current tuple.");
        }
        return currentOutput;
    }
};

class NestedLoopJoinOperator : public BinaryOperator {
private:
    size_t left_attr_index;
    size_t right_attr_index;
    std::vector<std::unique_ptr<Tuple>> rightTuples;
    const Tuple* currentLeftTuple = nullptr;  // borrowed from left child
    Tuple currentOutput;
    size_t rightTupleIndex = 0;
    bool has_left_tuple = false;
    bool has_next = false;

public:
    NestedLoopJoinOperator(Operator& left, Operator& right,
                           size_t left_attr_index,
                           size_t right_attr_index)
        : BinaryOperator(left, right),
          left_attr_index(left_attr_index),
          right_attr_index(right_attr_index) {}

    void open() override {
        input_left->setTxnContext(txn_);
        input_right->setTxnContext(txn_);
        input_left->open();
        input_right->open();

        rightTuples.clear();
        while (input_right->next()) {
            rightTuples.push_back(input_right->getOutput().clone());
        }
        input_right->close();

        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        rightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    bool next() override {
        clearTuple(currentOutput);
        has_next = false;

        while (true) {
            if (!has_left_tuple) {
                if (!input_left->next()) {
                    currentLeftTuple = nullptr;
                    return false;
                }
                currentLeftTuple = &input_left->getOutput();
                rightTupleIndex = 0;
                has_left_tuple = true;
            }

            while (rightTupleIndex < rightTuples.size()) {
                const Tuple& right_tuple = *rightTuples[rightTupleIndex++];
                if (!(tupleField(*currentLeftTuple, left_attr_index) ==
                      tupleField(right_tuple, right_attr_index))) {
                    continue;
                }

                makeConcatenatedTuple(currentOutput, *currentLeftTuple, right_tuple);
                has_next = true;
                return true;
            }

            has_left_tuple = false;
        }
    }

    void close() override {
        input_left->close();
        rightTuples.clear();
        currentLeftTuple = nullptr;
        clearTuple(currentOutput);
        rightTupleIndex = 0;
        has_left_tuple = false;
        has_next = false;
    }

    const Tuple& getOutput() const override {
        if (!has_next) {
            throw std::runtime_error("NestedLoopJoinOperator::getOutput called without a current tuple.");
        }
        return currentOutput;
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
    const Tuple* currentOutput = nullptr;

    struct FieldVectorHasher {
        std::size_t operator()(const std::vector<Field>& fields) const {
            std::size_t hash = 0;
            for (const auto& field : fields) {
                std::size_t fieldHash = 0;
                // Depending on the type, hash the corresponding data
                switch (field.getType()) {
                    case INT:
                        // Convert integer data to string and hash
                        fieldHash = std::hash<int>{}(field.asInt());
                        break;
                    case FLOAT:
                        // Convert float data to string and hash
                        fieldHash = std::hash<float>{}(field.asFloat());
                        break;
                    case STRING:
                        // Directly hash the string data
                        fieldHash = std::hash<std::string>{}(field.asString());
                        break;
                }
                // Combine the hash of the current field with the hash so far
                hash ^= fieldHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

public:
    HashAggregationOperator(Operator& input,
                            std::vector<size_t> group_by_attrs,
                            std::vector<AggrFunc> aggr_funcs)
        : UnaryOperator(input),
          group_by_attrs(std::move(group_by_attrs)),
          aggr_funcs(std::move(aggr_funcs)) {}

    void open() override {
        input->setTxnContext(txn_);
        input->open();
        output_tuples_index = 0;
        output_tuples.clear();
        currentOutput = nullptr;

        // Assume a hash map to aggregate tuples based on group_by_attrs
        std::unordered_map<std::vector<Field>, std::vector<Field>, FieldVectorHasher> hash_table;

        while (input->next()) {
            const Tuple& tuple = input->getOutput();

            // Extract group keys and initialize aggregation values
            std::vector<Field> group_keys;
            group_keys.reserve(group_by_attrs.size());
            for (size_t index : group_by_attrs) {
                group_keys.emplace_back(tupleField(tuple, index));
            }

            auto group_it = hash_table.find(group_keys);
            if (group_it == hash_table.end()) {
                std::vector<Field> aggr_values;
                aggr_values.reserve(aggr_funcs.size());
                for (const auto& aggr_func : aggr_funcs) {
                    aggr_values.push_back(
                        initialAggregate(aggr_func, tupleField(tuple, aggr_func.attr_index))
                    );
                }
                hash_table.emplace(std::move(group_keys), std::move(aggr_values));
                continue;
            }

            auto& aggr_values = group_it->second;
            for (size_t i = 0; i < aggr_funcs.size(); ++i) {
                aggr_values[i] = updateAggregate(
                    aggr_funcs[i],
                    aggr_values[i],
                    tupleField(tuple, aggr_funcs[i].attr_index)
                );
            }
        }

        // Prepare output tuples from the hash table
        for (const auto& entry : hash_table) {
            const auto& group_keys = entry.first;
            const auto& aggr_values = entry.second;

            Tuple output_tuple;
            // Assuming Tuple has a method to add Fields
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
        if (output_tuples_index >= output_tuples.size()) {
            currentOutput = nullptr;
            return false;
        }
        currentOutput = &output_tuples[output_tuples_index++];
        return true;
    }

    void close() override {
        input->close();
        output_tuples.clear();
        output_tuples_index = 0;
        currentOutput = nullptr;
    }

    const Tuple& getOutput() const override {
        if (!currentOutput) {
            throw std::runtime_error("HashAggregationOperator::getOutput called without a current tuple.");
        }
        return *currentOutput;
    }

private:
    Field initialAggregate(const AggrFunc& aggrFunc, const Field& newValue) {
        if (aggrFunc.func == AggrFuncType::COUNT) {
            return Field(1);
        }
        return Field(newValue);
    }

    Field updateAggregate(const AggrFunc& aggrFunc, const Field& currentAggr, const Field& newValue) {
        if (aggrFunc.func == AggrFuncType::COUNT) {
            if (currentAggr.getType() != FieldType::INT) {
                throw std::runtime_error("COUNT aggregate state must be an integer.");
            }
            return Field(currentAggr.asInt() + 1);
        }

        if (currentAggr.getType() != newValue.getType()) {
            throw std::runtime_error("Mismatched Field types in aggregation.");
        }

        switch (aggrFunc.func) {
            case AggrFuncType::COUNT:
                break;
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
                } else if (currentAggr.getType() == FieldType::STRING) {
                    return Field(std::max(currentAggr.asString(), newValue.asString()));
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
                } else if (currentAggr.getType() == FieldType::STRING) {
                    return Field(std::min(currentAggr.asString(), newValue.asString()));
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

class InsertOperator : public Operator {
private:
    TableHeap& tableHeap;
    std::unique_ptr<Tuple> tupleToInsert;
    RecoveryManager* recoveryManager = nullptr;
    Tuple emptyOutput;

public:
    explicit InsertOperator(TableHeap& tableHeap,
                            RecoveryManager* recoveryManager = nullptr)
        : tableHeap(tableHeap),
          recoveryManager(recoveryManager) {}

    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {}

    bool next() override {
        if (!tupleToInsert) {
            return false;
        }
        auto inserted =
            insertTupleIntoTableWithId(
                tableHeap,
                std::move(tupleToInsert),
                recoveryManager,
                txn_ ? txn_->id : 0
            );
        if (!inserted.has_value()) return false;
        if (txn_) {
            txn_->inserted_tuple_ids.push_back(*inserted);
        }
        return true;
    }

    void close() override {}

    const Tuple& getOutput() const override {
        return emptyOutput;
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
    Tuple emptyOutput;

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
            whereColumn,
            whereValue,
            assignments,
            recoveryManager,
            txn_ ? txn_->id : 0
        );
        executed = true;
        return updatedCount > 0;
    }

    void close() override {}

    const Tuple& getOutput() const override {
        return emptyOutput;
    }
};

class DeleteOperator : public Operator {
private:
    TableHeap& tableHeap;
    size_t whereColumn;
    Field whereValue;
    RecoveryManager* recoveryManager = nullptr;
    bool executed = false;
    size_t deletedCount = 0;
    Tuple emptyOutput;

public:
    DeleteOperator(TableHeap& tableHeap,
                   size_t whereColumn,
                   const Field& whereValue,
                   RecoveryManager* recoveryManager = nullptr)
        : tableHeap(tableHeap),
          whereColumn(whereColumn),
          whereValue(whereValue),
          recoveryManager(recoveryManager) {}

    void open() override {
        executed = false;
        deletedCount = 0;
    }

    bool next() override {
        if (executed) {
            return false;
        }

        deletedCount = tableHeap.deleteTuples(
            whereColumn,
            whereValue,
            recoveryManager,
            txn_ ? txn_->id : 0
        );
        executed = true;
        return deletedCount > 0;
    }

    void close() override {}

    const Tuple& getOutput() const override {
        return emptyOutput;
    }
};

struct ColumnRef {
    std::string tableName;
    std::string columnName;
    int attributeIndex = -1;
};

struct JoinClause {
    std::string tableName;
    std::string actualTableName;
    ColumnRef left;
    ColumnRef right;
};

struct FilterClause {
    ColumnRef column;
    std::string value;
};

struct ColumnEqualityClause {
    ColumnRef left;
    ColumnRef right;
};

struct QueryComponents {
    std::string tableName;
    std::string baseTableName;
    std::map<std::string, std::string> tableAliases;
    std::vector<ColumnRef> selectColumns;
    std::vector<std::optional<AggrFuncType>> selectAggregates;
    std::vector<JoinClause> joins;
    std::vector<FilterClause> filters;
    std::vector<ColumnEqualityClause> columnEqualities;
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
    bool equalityWhereCondition = false;
    std::string equalityWhereAttributeName;
    int equalityWhereAttributeIndex = -1;
    std::string equalityWhereValue;
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
    IMAGE_COPY,
    ANALYZE,
    INSERT,
    UPDATE,
    DELETE
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

bool isQueryKeyword(const std::string& token) {
    return token == "JOIN" || token == "ON" || token == "WHERE" ||
           token == "GROUP" || token == "BY";
}

std::string actualTableName(const QueryComponents& components,
                            const std::string& query_table_name) {
    auto alias_it = components.tableAliases.find(query_table_name);
    if (alias_it != components.tableAliases.end()) {
        return alias_it->second;
    }
    return query_table_name;
}

std::optional<AggrFuncType> parseAggregateToken(const std::string& aggregate_token) {
    if (aggregate_token.empty()) {
        return std::nullopt;
    }
    if (aggregate_token == "MIN") {
        return AggrFuncType::MIN;
    }
    if (aggregate_token == "MAX") {
        return AggrFuncType::MAX;
    }
    if (aggregate_token == "COUNT") {
        return AggrFuncType::COUNT;
    }
    if (aggregate_token == "SUM") {
        return AggrFuncType::SUM;
    }
    throw std::runtime_error("Unsupported projection aggregate: " + aggregate_token);
}

bool hasAggregateProjection(const QueryComponents& components) {
    return std::any_of(
        components.selectAggregates.begin(),
        components.selectAggregates.end(),
        [](const auto& aggregate) { return aggregate.has_value(); }
    );
}

QueryComponents parseQuery(const std::string& query) {
    QueryComponents components;
    if (!std::regex_search(query, std::regex("^\\s*PROJECT\\s+"))) {
        throw std::runtime_error("Query must start with PROJECT.");
    }

    std::regex sumRegex("^\\s*PROJECT\\s+SUM\\{([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
    std::smatch sumMatches;
    if (std::regex_search(query, sumMatches, sumRegex)) {
        components.sumOperation = true;
        parseAttributeToken(
            sumMatches[1],
            components.sumAttributeIndex,
            components.sumAttributeName
        );
    }
    std::regex tableRegex(
        "\\bFROM\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s+AS\\s+([A-Za-z_][A-Za-z0-9_]*)|\\s+([A-Za-z_][A-Za-z0-9_]*))?");
    std::smatch tableMatches;
    if (std::regex_search(query, tableMatches, tableRegex)) {
        components.baseTableName = tableMatches[1];
        std::string query_table_name = components.baseTableName;
        if (tableMatches[2].matched) {
            query_table_name = tableMatches[2];
        } else if (tableMatches[3].matched && !isQueryKeyword(tableMatches[3])) {
            query_table_name = tableMatches[3];
        }
        components.tableName = query_table_name;
        components.tableAliases[components.tableName] = components.baseTableName;
    }

    auto from_pos = query.find(" FROM ");
    if (from_pos != std::string::npos) {
        std::string select_part = query.substr(0, from_pos);
        std::regex columnRefRegex(
            "(?:(MIN|MAX|COUNT|SUM)\\s*)?\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
        std::smatch columnMatches;
        auto columnStart = select_part.cbegin();
        while (std::regex_search(columnStart, select_part.cend(), columnMatches, columnRefRegex)) {
            components.selectColumns.push_back(
                parseColumnRef(columnMatches[2], columnMatches[3])
            );
            components.selectAggregates.push_back(parseAggregateToken(columnMatches[1]));
            columnStart = columnMatches.suffix().first;
        }
    }

    const bool aggregate_projection = hasAggregateProjection(components);
    if (aggregate_projection &&
        std::any_of(
            components.selectAggregates.begin(),
            components.selectAggregates.end(),
            [](const auto& aggregate) { return !aggregate.has_value(); })) {
        throw std::runtime_error("Aggregate projections cannot be mixed with plain projections.");
    }

    std::regex joinRegex(
        "JOIN\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s+AS\\s+([A-Za-z_][A-Za-z0-9_]*)|\\s+([A-Za-z_][A-Za-z0-9_]*))?\\s+ON\\s+"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}\\s*=\\s*"
        "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
    std::smatch joinMatches;
    auto joinStart = query.cbegin();
    while (std::regex_search(joinStart, query.cend(), joinMatches, joinRegex)) {
        std::string actual_table_name = joinMatches[1];
        std::string query_table_name = actual_table_name;
        if (joinMatches[2].matched) {
            query_table_name = joinMatches[2];
        } else if (joinMatches[3].matched && !isQueryKeyword(joinMatches[3])) {
            query_table_name = joinMatches[3];
        }
        components.tableAliases[query_table_name] = actual_table_name;
        components.joins.push_back({
            query_table_name,
            actual_table_name,
            parseColumnRef(joinMatches[4], joinMatches[5]),
            parseColumnRef(joinMatches[6], joinMatches[7])
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

    std::regex equalityWhereRegex(
        "\\bWHERE\\s+\\{?([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}?\\s*=\\s*([^\\s]+)");
    std::smatch equalityWhereMatches;
    if (std::regex_search(query, equalityWhereMatches, equalityWhereRegex)) {
        components.equalityWhereCondition = true;
        parseAttributeToken(
            equalityWhereMatches[1],
            components.equalityWhereAttributeIndex,
            components.equalityWhereAttributeName
        );
        components.equalityWhereValue = equalityWhereMatches[2];
    }

    auto where_pos = query.find(" WHERE ");
    if (where_pos != std::string::npos) {
        std::string where_part = query.substr(where_pos);
        std::regex columnEqualityRegex(
            "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}\\s*=\\s*"
            "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}");
        std::smatch columnEqualityMatches;
        auto columnEqualityStart = where_part.cbegin();
        while (std::regex_search(
                   columnEqualityStart,
                   where_part.cend(),
                   columnEqualityMatches,
                   columnEqualityRegex)) {
            components.columnEqualities.push_back({
                parseColumnRef(columnEqualityMatches[1], columnEqualityMatches[2]),
                parseColumnRef(columnEqualityMatches[3], columnEqualityMatches[4])
            });
            columnEqualityStart = columnEqualityMatches.suffix().first;
        }

        std::regex filterRegex(
            "\\{([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*|\\d+)\\}\\s*=\\s*'?([^\\s']+)'?");
        std::smatch filterMatches;
        auto filterStart = where_part.cbegin();
        while (std::regex_search(filterStart, where_part.cend(), filterMatches, filterRegex)) {
            std::string value = filterMatches[3];
            if (!value.empty() && value.front() == '{') {
                filterStart = filterMatches.suffix().first;
                continue;
            }
            components.filters.push_back({
                parseColumnRef(filterMatches[1], filterMatches[2]),
                value
            });
            filterStart = filterMatches.suffix().first;
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

void resolveColumnRef(ColumnRef& column,
                      Catalog& catalog,
                      const QueryComponents& components) {
    auto& metadata = catalog.getTable(actualTableName(components, column.tableName));

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
    auto& base_metadata = catalog.getTable(components.baseTableName);

    for (auto& column : components.selectColumns) {
        resolveColumnRef(column, catalog, components);
    }

    for (auto& join : components.joins) {
        resolveColumnRef(join.left, catalog, components);
        resolveColumnRef(join.right, catalog, components);
    }

    for (auto& filter : components.filters) {
        resolveColumnRef(filter.column, catalog, components);
    }

    for (auto& equality : components.columnEqualities) {
        resolveColumnRef(equality.left, catalog, components);
        resolveColumnRef(equality.right, catalog, components);
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
    resolveBaseAttribute(
        components.equalityWhereAttributeIndex,
        components.equalityWhereAttributeName,
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

std::string aggregateName(AggrFuncType aggregate_type) {
    switch (aggregate_type) {
        case AggrFuncType::COUNT:
            return "COUNT";
        case AggrFuncType::MAX:
            return "MAX";
        case AggrFuncType::MIN:
            return "MIN";
        case AggrFuncType::SUM:
            return "SUM";
    }
    return "UNKNOWN";
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
            for (size_t i = 0; i < components.selectColumns.size(); i++) {
                if (components.selectAggregates[i].has_value()) {
                    std::cout << aggregateName(*components.selectAggregates[i]);
                }
                std::cout << "{" << columnLabel(components.selectColumns[i]) << "} ";
            }
        } else {
            for (auto attr : queryColumns) {
                std::cout << "{" << attr + 1 << "} "; // Convert back to 1-based indexing for display
            }
        }
        std::cout << "\n  Aggregate Projection: "
                  << (hasAggregateProjection(components) ? "Yes" : "No");
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
        if (!components.filters.empty()) {
            std::cout << "\n  Filter(s): ";
            for (const auto& filter : components.filters) {
                std::cout << "{" << columnLabel(filter.column)
                          << "} = " << filter.value << " ";
            }
        }
        if (!components.columnEqualities.empty()) {
            std::cout << "\n  Predicate edge(s): ";
            for (const auto& equality : components.columnEqualities) {
                std::cout << "{" << columnLabel(equality.left)
                          << "} = {" << columnLabel(equality.right) << "} ";
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
        } else if (components.type == StatementType::IMAGE_COPY) {
            std::cout << "IMAGE COPY";
        } else if (components.type == StatementType::ANALYZE) {
            std::cout << "ANALYZE";
            if (!components.tableName.empty()) {
                std::cout << " " << components.tableName;
            }
        } else if (components.type == StatementType::INSERT) {
            std::cout << "INSERT " << components.tableName << " ";
            for (size_t i = 0; i < components.values.size(); i++) {
                if (i != 0) std::cout << "|";
                std::cout << components.values[i];
            }
        } else if (components.type == StatementType::UPDATE) {
            std::cout << "UPDATE " << components.tableName << " SET ";
            for (const auto& assignment : components.assignments) {
                std::cout << assignment.first << "=" << assignment.second << " ";
            }
            std::cout << "WHERE "
                      << components.whereColumn << "=" << components.whereValue;
        } else {
            std::cout << "DELETE FROM " << components.tableName
                      << " WHERE "
                      << components.whereColumn << "=" << components.whereValue;
        }
    }
    std::cout << std::endl;
}

std::string joinStrings(const std::vector<std::string>& values,
                        const std::string& separator) {
    std::string output;
    for (size_t i = 0; i < values.size(); i++) {
        if (i != 0) {
            output += separator;
        }
        output += values[i];
    }
    return output;
}

std::vector<std::string> writtenJoinOrder(const QueryComponents& components) {
    std::vector<std::string> table_names{components.tableName};
    for (const auto& join : components.joins) {
        table_names.push_back(join.tableName);
    }
    return table_names;
}

std::vector<JoinClause> queryJoinEdges(const QueryComponents& components) {
    std::vector<JoinClause> edges = components.joins;
    for (const auto& equality : components.columnEqualities) {
        edges.push_back({"", "", equality.left, equality.right});
    }
    return edges;
}

std::vector<std::string> randomJoinOrder(const QueryComponents& components) {
    auto table_names = writtenJoinOrder(components);
    std::mt19937 rng(102);
    std::shuffle(table_names.begin(), table_names.end(), rng);

    std::vector<std::string> order;
    std::set<std::string> joined_tables;
    auto edges = queryJoinEdges(components);
    while (!table_names.empty()) {
        size_t chosen = table_names.size();
        for (size_t i = 0; i < table_names.size(); i++) {
            if (order.empty()) {
                chosen = i;
                break;
            }
            for (const auto& edge : edges) {
                bool connects_left =
                    edge.left.tableName == table_names[i] &&
                    joined_tables.find(edge.right.tableName) != joined_tables.end();
                bool connects_right =
                    edge.right.tableName == table_names[i] &&
                    joined_tables.find(edge.left.tableName) != joined_tables.end();
                if (connects_left || connects_right) {
                    chosen = i;
                    break;
                }
            }
            if (chosen != table_names.size()) {
                break;
            }
        }
        if (chosen == table_names.size()) {
            chosen = 0;
        }
        order.push_back(table_names[chosen]);
        joined_tables.insert(table_names[chosen]);
        table_names.erase(table_names.begin() + static_cast<long>(chosen));
    }
    return order;
}

struct LogicalColumnExpr {
    ColumnRef column;
};

struct LogicalAggregateExpr {
    AggrFuncType function = AggrFuncType::COUNT;
    LogicalColumnExpr input;
};

struct LogicalPredicate {
    enum class Kind { COLUMN_EQ_LITERAL, COLUMN_EQ_COLUMN, COLUMN_GT_LITERAL, COLUMN_LT_LITERAL };

    Kind kind = Kind::COLUMN_EQ_LITERAL;
    ColumnRef left;
    ColumnRef right;
    std::string literal;
};

struct LogicalPlanNode {
    enum class Kind { SCAN, JOIN, FILTER, PROJECT, AGGREGATE };

    Kind kind = Kind::SCAN;
    std::string tableName;
    std::string actualTableName;
    std::vector<LogicalColumnExpr> projectExprs;
    std::vector<LogicalAggregateExpr> aggregateExprs;
    std::vector<LogicalPredicate> predicates;
    std::vector<std::unique_ptr<LogicalPlanNode>> inputs;
};

struct PhysicalPlanNode {
    enum class Kind {
        SCAN,
        FILTER,
        PROJECT,
        HASH_AGGREGATE,
        NESTED_LOOP_JOIN,
        HASH_JOIN,
        SORT_MERGE_JOIN,
        SORT
    };

    Kind kind = Kind::SCAN;
    std::string tableName;
    JoinClause join;
    std::vector<ColumnRef> sortColumns;
    double rows = 0.0;
    double cost = 0.0;
    std::vector<std::shared_ptr<PhysicalPlanNode>> inputs;
};

std::string logicalNodeName(LogicalPlanNode::Kind kind) {
    switch (kind) {
        case LogicalPlanNode::Kind::SCAN:
            return "LogicalScan";
        case LogicalPlanNode::Kind::JOIN:
            return "LogicalEquiJoin";
        case LogicalPlanNode::Kind::FILTER:
            return "LogicalFilter";
        case LogicalPlanNode::Kind::PROJECT:
            return "LogicalProject";
        case LogicalPlanNode::Kind::AGGREGATE:
            return "LogicalAggregate";
    }
    return "LogicalUnknown";
}

std::string physicalNodeName(PhysicalPlanNode::Kind kind) {
    switch (kind) {
        case PhysicalPlanNode::Kind::SCAN:
            return "Scan";
        case PhysicalPlanNode::Kind::FILTER:
            return "Filter";
        case PhysicalPlanNode::Kind::PROJECT:
            return "Project";
        case PhysicalPlanNode::Kind::HASH_AGGREGATE:
            return "HashAggregate";
        case PhysicalPlanNode::Kind::NESTED_LOOP_JOIN:
            return "NestedLoopJoin";
        case PhysicalPlanNode::Kind::HASH_JOIN:
            return "HashJoin";
        case PhysicalPlanNode::Kind::SORT_MERGE_JOIN:
            return "SortMergeJoin";
        case PhysicalPlanNode::Kind::SORT:
            return "Sort";
    }
    return "PhysicalUnknown";
}

std::string logicalColumnLabel(const LogicalColumnExpr& expression) {
    return columnLabel(expression.column);
}

std::string logicalAggregateLabel(const LogicalAggregateExpr& expression) {
    return aggregateName(expression.function) + "(" +
           logicalColumnLabel(expression.input) + ")";
}

std::string logicalPredicateLabel(const LogicalPredicate& predicate) {
    switch (predicate.kind) {
        case LogicalPredicate::Kind::COLUMN_EQ_LITERAL:
            return columnLabel(predicate.left) + " = " + predicate.literal;
        case LogicalPredicate::Kind::COLUMN_EQ_COLUMN:
            return columnLabel(predicate.left) + " = " + columnLabel(predicate.right);
        case LogicalPredicate::Kind::COLUMN_GT_LITERAL:
            return columnLabel(predicate.left) + " > " + predicate.literal;
        case LogicalPredicate::Kind::COLUMN_LT_LITERAL:
            return columnLabel(predicate.left) + " < " + predicate.literal;
    }
    return "UNKNOWN";
}

std::vector<std::string> logicalExpressionLabels(const LogicalPlanNode& node) {
    std::vector<std::string> expressions;
    if (node.kind == LogicalPlanNode::Kind::SCAN) {
        expressions.push_back(node.tableName + "(" + node.actualTableName + ")");
    }
    for (const auto& predicate : node.predicates) {
        expressions.push_back(logicalPredicateLabel(predicate));
    }
    for (const auto& aggregate : node.aggregateExprs) {
        expressions.push_back(logicalAggregateLabel(aggregate));
    }
    for (const auto& project : node.projectExprs) {
        expressions.push_back(logicalColumnLabel(project));
    }
    return expressions;
}

std::vector<LogicalColumnExpr> logicalProjectionExprs(const QueryComponents& components) {
    std::vector<LogicalColumnExpr> expressions;
    for (const auto& column : components.selectColumns) {
        expressions.push_back({column});
    }
    return expressions;
}

std::vector<LogicalAggregateExpr> logicalAggregateExprs(const QueryComponents& components) {
    std::vector<LogicalAggregateExpr> expressions;
    for (size_t i = 0; i < components.selectColumns.size(); i++) {
        if (components.selectAggregates[i].has_value()) {
            expressions.push_back({
                *components.selectAggregates[i],
                {components.selectColumns[i]}
            });
        }
    }
    if (components.sumOperation) {
        expressions.push_back({
            AggrFuncType::SUM,
            {parseColumnRef(
                components.tableName,
                attributeLabel(
                    components.sumAttributeName,
                    components.sumAttributeIndex
                ))}
        });
    }
    return expressions;
}

std::vector<LogicalPredicate> logicalFilterPredicates(const QueryComponents& components) {
    std::vector<LogicalPredicate> predicates;
    for (const auto& filter : components.filters) {
        predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_LITERAL,
            filter.column,
            {},
            filter.value
        });
    }
    for (const auto& equality : components.columnEqualities) {
        predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
            equality.left,
            equality.right,
            ""
        });
    }
    if (components.whereCondition) {
        ColumnRef column = parseColumnRef(
            components.tableName,
            attributeLabel(
                components.whereAttributeName,
                components.whereAttributeIndex
            )
        );
        predicates.push_back({
            LogicalPredicate::Kind::COLUMN_GT_LITERAL,
            column,
            {},
            std::to_string(components.lowerBound)
        });
        predicates.push_back({
            LogicalPredicate::Kind::COLUMN_LT_LITERAL,
            column,
            {},
            std::to_string(components.upperBound)
        });
    }
    if (components.equalityWhereCondition) {
        predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_LITERAL,
            parseColumnRef(
                components.tableName,
                attributeLabel(
                    components.equalityWhereAttributeName,
                    components.equalityWhereAttributeIndex
                )
            ),
            {},
            components.equalityWhereValue
        });
    }
    return predicates;
}

std::vector<LogicalPredicate> logicalFiltersForTable(const QueryComponents& components,
                                                     const std::string& table_name) {
    std::vector<LogicalPredicate> predicates;
    for (const auto& filter : components.filters) {
        if (filter.column.tableName == table_name) {
            predicates.push_back({
                LogicalPredicate::Kind::COLUMN_EQ_LITERAL,
                filter.column,
                {},
                filter.value
            });
        }
    }
    return predicates;
}

std::unique_ptr<LogicalPlanNode> scanNode(const QueryComponents& components,
                                          const std::string& table_name) {
    auto node = std::make_unique<LogicalPlanNode>();
    node->kind = LogicalPlanNode::Kind::SCAN;
    node->tableName = table_name;
    node->actualTableName = actualTableName(components, table_name);
    return node;
}

std::unique_ptr<LogicalPlanNode> rewrittenScanNode(const QueryComponents& components,
                                                   const std::string& table_name) {
    auto scan = scanNode(components, table_name);
    auto filters = logicalFiltersForTable(components, table_name);
    if (filters.empty()) {
        return scan;
    }

    auto filter_node = std::make_unique<LogicalPlanNode>();
    filter_node->kind = LogicalPlanNode::Kind::FILTER;
    filter_node->predicates = std::move(filters);
    filter_node->inputs.push_back(std::move(scan));
    return filter_node;
}

std::optional<JoinClause> joinEdgeForNextTable(const std::string& table_name,
                                               const std::set<std::string>& joined_tables,
                                               const QueryComponents& components) {
    for (const auto& edge : queryJoinEdges(components)) {
        bool next_is_left = edge.left.tableName == table_name &&
            joined_tables.find(edge.right.tableName) != joined_tables.end();
        bool next_is_right = edge.right.tableName == table_name &&
            joined_tables.find(edge.left.tableName) != joined_tables.end();
        if (next_is_left || next_is_right) {
            return edge;
        }
    }
    return std::nullopt;
}

std::unique_ptr<LogicalPlanNode> buildLogicalPlanFromOrder(
    const QueryComponents& components,
    const std::vector<std::string>& table_order) {
    if (table_order.empty()) {
        throw std::runtime_error("Cannot build a logical plan without tables.");
    }

    auto root = scanNode(components, table_order.front());
    std::set<std::string> joined_tables{table_order.front()};
    for (size_t i = 1; i < table_order.size(); i++) {
        auto edge = joinEdgeForNextTable(table_order[i], joined_tables, components);
        if (!edge) {
            throw std::runtime_error("Random join order is not connected.");
        }
        auto join_node = std::make_unique<LogicalPlanNode>();
        join_node->kind = LogicalPlanNode::Kind::JOIN;
        join_node->predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
            edge->left,
            edge->right,
            ""
        });
        join_node->inputs.push_back(std::move(root));
        join_node->inputs.push_back(scanNode(components, table_order[i]));
        root = std::move(join_node);
        joined_tables.insert(table_order[i]);
    }

    auto filters = logicalFilterPredicates(components);
    if (!filters.empty()) {
        auto filter_node = std::make_unique<LogicalPlanNode>();
        filter_node->kind = LogicalPlanNode::Kind::FILTER;
        filter_node->predicates = std::move(filters);
        filter_node->inputs.push_back(std::move(root));
        root = std::move(filter_node);
    }

    if (hasAggregateProjection(components) || components.sumOperation || components.groupBy) {
        auto aggregate_node = std::make_unique<LogicalPlanNode>();
        aggregate_node->kind = LogicalPlanNode::Kind::AGGREGATE;
        aggregate_node->aggregateExprs = logicalAggregateExprs(components);
        aggregate_node->inputs.push_back(std::move(root));
        root = std::move(aggregate_node);
    } else {
        auto project_node = std::make_unique<LogicalPlanNode>();
        project_node->kind = LogicalPlanNode::Kind::PROJECT;
        project_node->projectExprs = logicalProjectionExprs(components);
        project_node->inputs.push_back(std::move(root));
        root = std::move(project_node);
    }
    return root;
}

std::unique_ptr<LogicalPlanNode> buildRandomStartLogicalPlan(
    const QueryComponents& components) {
    return buildLogicalPlanFromOrder(components, randomJoinOrder(components));
}

bool predicateTablesSeen(const ColumnEqualityClause& equality,
                         const std::set<std::string>& table_names) {
    return table_names.find(equality.left.tableName) != table_names.end() &&
           table_names.find(equality.right.tableName) != table_names.end();
}

std::unique_ptr<LogicalPlanNode> buildRewrittenLogicalPlan(const QueryComponents& components) {
    auto root = rewrittenScanNode(components, components.tableName);
    std::set<std::string> seen_tables{components.tableName};
    std::vector<bool> equality_used(components.columnEqualities.size(), false);

    for (const auto& join : components.joins) {
        auto join_node = std::make_unique<LogicalPlanNode>();
        join_node->kind = LogicalPlanNode::Kind::JOIN;
        join_node->predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
            join.left,
            join.right,
            ""
        });

        auto tables_after_join = seen_tables;
        tables_after_join.insert(join.tableName);
        for (size_t i = 0; i < components.columnEqualities.size(); i++) {
            if (!equality_used[i] &&
                predicateTablesSeen(components.columnEqualities[i], tables_after_join)) {
                join_node->predicates.push_back({
                    LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
                    components.columnEqualities[i].left,
                    components.columnEqualities[i].right,
                    ""
                });
                equality_used[i] = true;
            }
        }

        join_node->inputs.push_back(std::move(root));
        join_node->inputs.push_back(rewrittenScanNode(components, join.tableName));
        root = std::move(join_node);
        seen_tables.insert(join.tableName);
    }

    std::vector<LogicalPredicate> residual_predicates;
    for (size_t i = 0; i < components.columnEqualities.size(); i++) {
        if (!equality_used[i]) {
            residual_predicates.push_back({
                LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
                components.columnEqualities[i].left,
                components.columnEqualities[i].right,
                ""
            });
        }
    }
    if (!residual_predicates.empty()) {
        auto filter_node = std::make_unique<LogicalPlanNode>();
        filter_node->kind = LogicalPlanNode::Kind::FILTER;
        filter_node->predicates = std::move(residual_predicates);
        filter_node->inputs.push_back(std::move(root));
        root = std::move(filter_node);
    }

    if (hasAggregateProjection(components) || components.sumOperation || components.groupBy) {
        auto aggregate_node = std::make_unique<LogicalPlanNode>();
        aggregate_node->kind = LogicalPlanNode::Kind::AGGREGATE;
        aggregate_node->aggregateExprs = logicalAggregateExprs(components);
        aggregate_node->inputs.push_back(std::move(root));
        root = std::move(aggregate_node);
    } else {
        auto project_node = std::make_unique<LogicalPlanNode>();
        project_node->kind = LogicalPlanNode::Kind::PROJECT;
        project_node->projectExprs = logicalProjectionExprs(components);
        project_node->inputs.push_back(std::move(root));
        root = std::move(project_node);
    }
    return root;
}

using GroupId = int;
using ExpressionId = int;

constexpr GroupId INVALID_GROUP_ID = 0;
constexpr ExpressionId INVALID_EXPRESSION_ID = 0;

void combineMemoHash(size_t& seed, size_t value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct MemoExpressionKey {
    std::string op;
    std::vector<GroupId> inputs;
    std::vector<std::string> details;

    bool operator==(const MemoExpressionKey& other) const {
        return op == other.op &&
               inputs == other.inputs &&
               details == other.details;
    }
};

struct MemoExpressionKeyHash {
    size_t operator()(const MemoExpressionKey& key) const {
        size_t seed = std::hash<std::string>{}(key.op);
        for (GroupId input : key.inputs) {
            combineMemoHash(seed, std::hash<int>{}(input));
        }
        for (const auto& detail : key.details) {
            combineMemoHash(seed, std::hash<std::string>{}(detail));
        }
        return seed;
    }
};

struct MemoExpression {
    ExpressionId id = INVALID_EXPRESSION_ID;
    std::string op;
    std::vector<GroupId> inputs;
    std::vector<std::string> details;

    MemoExpressionKey key() const {
        return {op, inputs, details};
    }
};

struct RuleFire {
    std::string ruleName;
    std::string detail;
};

struct MemoTransformationStats {
    size_t initialGroups = 0;
    size_t initialExpressions = 0;
    size_t finalGroups = 0;
    size_t finalExpressions = 0;
    size_t mergedGroups = 0;
    size_t deduplicatedExpressions = 0;
    std::vector<RuleFire> firedRules;
};

struct MemoWinner {
    std::string requiredTrait;
    std::string expression;
    double cost = 0.0;
};

struct MemoGroup {
    GroupId id = INVALID_GROUP_ID;
    std::string logicalProperty;
    std::vector<MemoExpression> expressions;
    std::unordered_map<MemoExpressionKey, ExpressionId, MemoExpressionKeyHash> expressionIds;
    std::map<std::string, MemoWinner> winners;
};

class Memo {
    std::vector<MemoGroup> groups;
    std::map<std::string, GroupId> groupByProperty;
    GroupId rootGroupId = INVALID_GROUP_ID;
    ExpressionId nextExpressionId = 1;
    size_t groupMergeCount = 0;
    size_t duplicateExpressionCount = 0;

    MemoGroup& groupFor(GroupId group_id) {
        return groups[group_id - 1];
    }

    const MemoGroup& groupFor(GroupId group_id) const {
        return groups[group_id - 1];
    }

public:
    GroupId internGroup(const std::string& logical_property) {
        auto it = groupByProperty.find(logical_property);
        if (it != groupByProperty.end()) {
            return it->second;
        }

        GroupId group_id = static_cast<GroupId>(groups.size() + 1);
        MemoGroup group;
        group.id = group_id;
        group.logicalProperty = logical_property;
        groups.push_back(std::move(group));
        groupByProperty[logical_property] = group_id;
        return group_id;
    }

    bool addExpressionToGroup(GroupId group_id, MemoExpression expression) {
        auto& group = groupFor(group_id);
        auto expression_key = expression.key();
        if (group.expressionIds.find(expression_key) != group.expressionIds.end()) {
            duplicateExpressionCount++;
            return false;
        }

        expression.id = nextExpressionId++;
        group.expressionIds[expression_key] = expression.id;
        group.expressions.push_back(std::move(expression));
        return true;
    }

    bool addExpression(GroupId group_id, MemoExpression expression) {
        return addExpressionToGroup(group_id, std::move(expression));
    }

    void setWinner(GroupId group_id, MemoWinner winner) {
        groupFor(group_id).winners[winner.requiredTrait] = std::move(winner);
    }

    void setFinalGroupId(GroupId group_id) {
        rootGroupId = group_id;
    }

    GroupId finalGroupId() const {
        return rootGroupId;
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

    size_t mergedGroupCount() const {
        return groupMergeCount;
    }

    size_t deduplicatedExpressionCount() const {
        return duplicateExpressionCount;
    }
};

std::set<std::string> memoPropertySet(const std::string& property,
                                      const std::string& field_name) {
    std::set<std::string> values;
    auto prefix = field_name + "={";
    auto start = property.find(prefix);
    if (start == std::string::npos) {
        return values;
    }
    start += prefix.size();
    auto end = property.find("}", start);
    std::stringstream stream(property.substr(start, end - start));
    std::string value;
    while (std::getline(stream, value, '|')) {
        if (!value.empty()) {
            values.insert(value);
        }
    }
    return values;
}

std::string memoPropertyField(const std::string& name,
                              const std::set<std::string>& values) {
    if (values.empty()) {
        return "";
    }
    return name + "={" + joinStrings(
        std::vector<std::string>(values.begin(), values.end()),
        "|"
    ) + "}";
}

std::string memoPropertyForInputs(const std::vector<GroupId>& input_groups,
                                  const std::vector<std::string>& predicates,
                                  const Memo& memo) {
    std::set<std::string> tables;
    std::set<std::string> all_predicates(predicates.begin(), predicates.end());
    for (GroupId group_id : input_groups) {
        const auto& property = memo.allGroups()[group_id - 1].logicalProperty;
        auto input_tables = memoPropertySet(property, "tables");
        tables.insert(input_tables.begin(), input_tables.end());
        auto input_predicates = memoPropertySet(property, "predicates");
        all_predicates.insert(input_predicates.begin(), input_predicates.end());
    }

    std::vector<std::string> fields;
    fields.push_back(memoPropertyField("tables", tables));
    auto predicate_field = memoPropertyField("predicates", all_predicates);
    if (!predicate_field.empty()) {
        fields.push_back(predicate_field);
    }
    return joinStrings(fields, " ");
}

bool predicateMentionsAnyTable(const std::string& predicate,
                               const std::set<std::string>& tables) {
    for (const auto& table : tables) {
        if (predicate.find(table + ".") != std::string::npos) {
            return true;
        }
    }
    return false;
}

MemoExpression makeMemoExpression(const std::string& op,
                                  std::vector<GroupId> inputs,
                                  std::vector<std::string> details) {
    MemoExpression expression;
    expression.op = op;
    expression.inputs = std::move(inputs);
    expression.details = std::move(details);
    return expression;
}

std::string logicalPropertyForNode(const LogicalPlanNode& node,
                                   const std::vector<GroupId>& input_groups,
                                   const Memo& memo) {
    std::set<std::string> tables;
    std::set<std::string> predicates;
    for (GroupId group_id : input_groups) {
        const auto& property = memo.allGroups()[group_id - 1].logicalProperty;
        auto input_tables = memoPropertySet(property, "tables");
        tables.insert(input_tables.begin(), input_tables.end());
        auto input_predicates = memoPropertySet(property, "predicates");
        predicates.insert(input_predicates.begin(), input_predicates.end());
    }

    if (node.kind == LogicalPlanNode::Kind::SCAN) {
        tables.insert(node.tableName);
    }

    for (const auto& predicate : node.predicates) {
        predicates.insert(logicalPredicateLabel(predicate));
    }

    if (tables.empty()) {
        return logicalNodeName(node.kind);
    }

    std::set<std::string> output;
    if (node.kind == LogicalPlanNode::Kind::PROJECT ||
        node.kind == LogicalPlanNode::Kind::AGGREGATE) {
        for (const auto& expression : logicalExpressionLabels(node)) {
            output.insert(expression);
        }
    }

    std::vector<std::string> fields;
    fields.push_back(memoPropertyField("tables", tables));
    auto predicate_field = memoPropertyField("predicates", predicates);
    if (!predicate_field.empty()) {
        fields.push_back(predicate_field);
    }
    auto output_field = memoPropertyField("output", output);
    if (!output_field.empty()) {
        fields.push_back(output_field);
    }
    return joinStrings(fields, " ");
}

GroupId addMemoExpression(const LogicalPlanNode& node, Memo& memo) {
    std::vector<GroupId> input_groups;
    for (const auto& input : node.inputs) {
        input_groups.push_back(addMemoExpression(*input, memo));
    }

    GroupId group_id = memo.internGroup(
        logicalPropertyForNode(node, input_groups, memo)
    );
    memo.addExpression(group_id, makeMemoExpression(
        logicalNodeName(node.kind),
        input_groups,
        logicalExpressionLabels(node)
    ));
    return group_id;
}

Memo buildMemo(const LogicalPlanNode& root) {
    Memo memo;
    memo.setFinalGroupId(addMemoExpression(root, memo));
    return memo;
}

void recordMemoRule(MemoTransformationStats& stats,
                    const std::string& rule_name,
                    const std::string& detail) {
    stats.firedRules.push_back({rule_name, detail});
}

enum class PatternKind {
    MatchOp,
    PickOne,
    PickMany,
    IgnoreOne,
    IgnoreMany
};

enum class RuleKind {
    Transformation,
    Implementation
};

struct RulePattern {
    PatternKind kind = PatternKind::IgnoreOne;
    std::string op;
    std::vector<RulePattern> inputs;
};

RulePattern matchOpPattern(const std::string& op,
                           std::vector<RulePattern> inputs = {}) {
    RulePattern pattern;
    pattern.kind = PatternKind::MatchOp;
    pattern.op = op;
    pattern.inputs = std::move(inputs);
    return pattern;
}

RulePattern pickOnePattern() {
    RulePattern pattern;
    pattern.kind = PatternKind::PickOne;
    return pattern;
}

bool matchRulePattern(const RulePattern& pattern,
                      const MemoExpression& expression) {
    if (pattern.kind != PatternKind::MatchOp) {
        return true;
    }

    if (expression.op != pattern.op) {
        return false;
    }

    size_t required_inputs = 0;
    bool accepts_many = false;
    for (const auto& input : pattern.inputs) {
        if (input.kind == PatternKind::PickMany ||
            input.kind == PatternKind::IgnoreMany) {
            accepts_many = true;
            continue;
        }
        required_inputs++;
    }
    return accepts_many
        ? expression.inputs.size() >= required_inputs
        : expression.inputs.size() == required_inputs;
}

struct RuleBinding {
    Memo& memo;
    const QueryComponents& components;
    const std::vector<MemoGroup>& groups;
    const MemoGroup& group;
    const MemoExpression& expression;
};

struct OptimizerRule {
    std::string name;
    RuleKind kind = RuleKind::Implementation;
    int promise = 0;
    RulePattern pattern;
    std::function<std::vector<MemoExpression>(RuleBinding&)> apply;
};

std::vector<MemoExpression> addRewrittenLogicalPlan(RuleBinding& binding) {
    auto rewritten_plan = buildRewrittenLogicalPlan(binding.components);
    addMemoExpression(*rewritten_plan, binding.memo);
    return {};
}

std::vector<MemoExpression> commuteEquiJoin(RuleBinding& binding) {
    auto commuted = binding.expression;
    std::swap(commuted.inputs[0], commuted.inputs[1]);
    return {commuted};
}

std::vector<MemoExpression> associateLeftToRight(RuleBinding& binding) {
    std::vector<MemoExpression> results;
    GroupId left_group_id = binding.expression.inputs[0];
    GroupId right_group_id = binding.expression.inputs[1];
    const auto& left_group = binding.groups[left_group_id - 1];

    for (const auto& left_expression : left_group.expressions) {
        if (left_expression.op != "LogicalEquiJoin" ||
            left_expression.inputs.size() != 2) {
            continue;
        }

        GroupId a_group_id = left_expression.inputs[0];
        GroupId b_group_id = left_expression.inputs[1];
        auto a_tables = memoPropertySet(
            binding.groups[a_group_id - 1].logicalProperty,
            "tables"
        );
        bool top_predicate_uses_a = false;
        for (const auto& predicate : binding.expression.details) {
            top_predicate_uses_a =
                top_predicate_uses_a ||
                predicateMentionsAnyTable(predicate, a_tables);
        }
        if (top_predicate_uses_a) {
            continue;
        }

        GroupId bc_group_id = binding.memo.internGroup(
            memoPropertyForInputs(
                {b_group_id, right_group_id},
                binding.expression.details,
                binding.memo
            )
        );
        binding.memo.addExpression(bc_group_id, makeMemoExpression(
            "LogicalEquiJoin",
            {b_group_id, right_group_id},
            binding.expression.details
        ));

        results.push_back(makeMemoExpression(
            "LogicalEquiJoin",
            {a_group_id, bc_group_id},
            left_expression.details
        ));
    }
    return results;
}

std::vector<MemoExpression> associateRightToLeft(RuleBinding& binding) {
    std::vector<MemoExpression> results;
    GroupId left_group_id = binding.expression.inputs[0];
    GroupId right_group_id = binding.expression.inputs[1];
    const auto& right_group = binding.groups[right_group_id - 1];

    for (const auto& right_expression : right_group.expressions) {
        if (right_expression.op != "LogicalEquiJoin" ||
            right_expression.inputs.size() != 2) {
            continue;
        }

        GroupId b_group_id = right_expression.inputs[0];
        GroupId c_group_id = right_expression.inputs[1];
        auto c_tables = memoPropertySet(
            binding.groups[c_group_id - 1].logicalProperty,
            "tables"
        );
        bool top_predicate_uses_c = false;
        for (const auto& predicate : binding.expression.details) {
            top_predicate_uses_c =
                top_predicate_uses_c ||
                predicateMentionsAnyTable(predicate, c_tables);
        }
        if (top_predicate_uses_c) {
            continue;
        }

        GroupId ab_group_id = binding.memo.internGroup(
            memoPropertyForInputs(
                {left_group_id, b_group_id},
                binding.expression.details,
                binding.memo
            )
        );
        binding.memo.addExpression(ab_group_id, makeMemoExpression(
            "LogicalEquiJoin",
            {left_group_id, b_group_id},
            binding.expression.details
        ));

        results.push_back(makeMemoExpression(
            "LogicalEquiJoin",
            {ab_group_id, c_group_id},
            right_expression.details
        ));
    }
    return results;
}

std::vector<MemoExpression> implementLogicalScan(RuleBinding& binding) {
    return {makeMemoExpression("Scan", {}, binding.expression.details)};
}

std::vector<MemoExpression> implementSelect(RuleBinding& binding) {
    return {makeMemoExpression(
        "Filter",
        binding.expression.inputs,
        binding.expression.details
    )};
}

std::vector<MemoExpression> implementAggregate(RuleBinding& binding) {
    return {makeMemoExpression(
        "HashAggregate",
        binding.expression.inputs,
        binding.expression.details
    )};
}

std::vector<MemoExpression> implementNestedLoopJoin(RuleBinding& binding) {
    return {makeMemoExpression(
        "NestedLoopJoin",
        binding.expression.inputs,
        binding.expression.details
    )};
}

std::vector<MemoExpression> implementHashJoin(RuleBinding& binding) {
    return {makeMemoExpression(
        "HashJoin",
        binding.expression.inputs,
        binding.expression.details
    )};
}

std::vector<MemoExpression> implementSortMergeJoin(RuleBinding& binding) {
    return {makeMemoExpression(
        "SortMergeJoin",
        binding.expression.inputs,
        binding.expression.details
    )};
}

std::vector<OptimizerRule> memoTransformationRules() {
    auto scan_pattern = matchOpPattern("LogicalScan");
    auto filter_pattern = matchOpPattern("LogicalFilter", {pickOnePattern()});
    auto project_pattern = matchOpPattern("LogicalProject", {pickOnePattern()});
    auto aggregate_pattern = matchOpPattern("LogicalAggregate", {pickOnePattern()});
    auto join_pattern = matchOpPattern(
        "LogicalEquiJoin",
        {pickOnePattern(), pickOnePattern()}
    );

    std::vector<OptimizerRule> rules;
    for (const auto& root_pattern : {filter_pattern, project_pattern, aggregate_pattern}) {
        rules.push_back({
            "FILTER_PUSH_DOWN",
            RuleKind::Transformation,
            100,
            root_pattern,
            addRewrittenLogicalPlan
        });
        rules.push_back({
            "JOIN_PREDICATE_ATTACH",
            RuleKind::Transformation,
            90,
            root_pattern,
            addRewrittenLogicalPlan
        });
    }

    std::vector<OptimizerRule> rest = {
        {
            "EQJOIN_COMMUTE",
            RuleKind::Transformation,
            80,
            join_pattern,
            commuteEquiJoin
        },
        {
            "EQJOIN_LTOR",
            RuleKind::Transformation,
            70,
            join_pattern,
            associateLeftToRight
        },
        {
            "EQJOIN_RTOL",
            RuleKind::Transformation,
            70,
            join_pattern,
            associateRightToLeft
        },
        {
            "LOGICAL_SCAN_TO_SCAN",
            RuleKind::Implementation,
            60,
            scan_pattern,
            implementLogicalScan
        },
        {
            "SELECT_TO_FILTER",
            RuleKind::Implementation,
            55,
            filter_pattern,
            implementSelect
        },
        {
            "AGG_TO_HASH_AGG",
            RuleKind::Implementation,
            50,
            aggregate_pattern,
            implementAggregate
        },
        {
            "EQJOIN_TO_LOOPS_JOIN",
            RuleKind::Implementation,
            45,
            join_pattern,
            implementNestedLoopJoin
        },
        {
            "EQJOIN_TO_HASH_JOIN",
            RuleKind::Implementation,
            45,
            join_pattern,
            implementHashJoin
        },
        {
            "EQJOIN_TO_MERGE_JOIN",
            RuleKind::Implementation,
            45,
            join_pattern,
            implementSortMergeJoin
        }
    };
    rules.insert(rules.end(), rest.begin(), rest.end());
    return rules;
}

std::string ruleFireDetail(const OptimizerRule& rule,
                           const MemoGroup& group) {
    return "G" + std::to_string(group.id) +
           " matched " + rule.name +
           " with promise " + std::to_string(rule.promise);
}

bool shouldRunPlanRewriteRuleAgain(const OptimizerRule& rule, size_t pass) {
    if (rule.name == "FILTER_PUSH_DOWN" ||
        rule.name == "JOIN_PREDICATE_ATTACH") {
        return pass == 0;
    }
    return true;
}

void applyMemoTransformationRules(Memo& memo,
                                  const QueryComponents& components,
                                  MemoTransformationStats& stats) {
    stats.initialGroups = memo.groupCount();
    stats.initialExpressions = memo.expressionCount();
    auto rules = memoTransformationRules();

    for (size_t pass = 0; pass < 6; pass++) {
        bool changed = false;
        auto groups_snapshot = memo.allGroups();
        for (const auto& group : groups_snapshot) {
            for (const auto& expression : group.expressions) {
                for (const auto& rule : rules) {
                    if (!shouldRunPlanRewriteRuleAgain(rule, pass) ||
                        !matchRulePattern(rule.pattern, expression)) {
                        continue;
                    }

                    auto before_groups = memo.groupCount();
                    auto before_expressions = memo.expressionCount();
                    RuleBinding binding{
                        memo,
                        components,
                        groups_snapshot,
                        group,
                        expression
                    };
                    auto generated = rule.apply(binding);
                    for (auto& generated_expression : generated) {
                        memo.addExpressionToGroup(
                            group.id,
                            std::move(generated_expression)
                        );
                    }

                    bool rule_changed =
                        memo.groupCount() != before_groups ||
                        memo.expressionCount() != before_expressions;
                    if (rule_changed ||
                        rule.name == "FILTER_PUSH_DOWN" ||
                        rule.name == "JOIN_PREDICATE_ATTACH") {
                        recordMemoRule(stats, rule.name, ruleFireDetail(rule, group));
                    }
                    changed = changed || rule_changed;
                }
            }
        }
        if (!changed) {
            break;
        }
    }

    stats.finalGroups = memo.groupCount();
    stats.finalExpressions = memo.expressionCount();
    stats.mergedGroups = memo.mergedGroupCount();
    stats.deduplicatedExpressions = memo.deduplicatedExpressionCount();
}

struct ColumnStats {
    FieldType type = INT;
    size_t ndv = 0;
    bool hasIntRange = false;
    int minInt = 0;
    int maxInt = 0;
    std::map<std::string, size_t> mcvCounts;
    std::vector<HistogramBucket> equiWidthBuckets;
    std::vector<HistogramBucket> equiDepthBuckets;
};

struct TableStats {
    size_t rowCount = 0;
    size_t pageCount = 0;
    std::map<std::string, ColumnStats> columns;
};

struct StatisticsCatalog {
    std::map<std::string, TableStats> tables;
};

std::string formatEstimate(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

std::string statsColumnName(const TableMetadata& metadata, size_t index) {
    return metadata.schema.columns[index].name;
}

const TableStats& tableStatsFor(const StatisticsCatalog& stats,
                                const QueryComponents& components,
                                const std::string& table_name) {
    auto table_it = stats.tables.find(actualTableName(components, table_name));
    if (table_it == stats.tables.end()) {
        throw std::runtime_error("Missing stats for table " + table_name);
    }
    return table_it->second;
}

const ColumnStats& columnStatsFor(const StatisticsCatalog& stats,
                                  const QueryComponents& components,
                                  const ColumnRef& column) {
    const auto& table_stats = tableStatsFor(stats, components, column.tableName);
    auto column_it = table_stats.columns.find(column.columnName);
    if (column_it == table_stats.columns.end()) {
        throw std::runtime_error("Missing stats for column " + columnLabel(column));
    }
    return column_it->second;
}

double estimateEqualityFilterRows(double input_rows,
                                  const ColumnStats& column_stats) {
    if (column_stats.ndv == 0) {
        return input_rows;
    }
    return std::max(1.0, input_rows / static_cast<double>(column_stats.ndv));
}

double estimateEqualityFilterRowsWithMcv(double input_rows,
                                         const ColumnStats& column_stats,
                                         const std::string& value) {
    auto value_it = column_stats.mcvCounts.find(value);
    if (value_it != column_stats.mcvCounts.end()) {
        return static_cast<double>(value_it->second);
    }
    return estimateEqualityFilterRows(input_rows, column_stats);
}

double estimateJoinRows(double left_rows,
                        double right_rows,
                        const ColumnStats& left_stats,
                        const ColumnStats& right_stats) {
    const auto denominator = std::max(left_stats.ndv, right_stats.ndv);
    if (denominator == 0) {
        return left_rows * right_rows;
    }
    return std::max(1.0, (left_rows * right_rows) / static_cast<double>(denominator));
}

std::vector<HistogramBucket> buildEquiWidthBuckets(std::vector<int> values,
                                                   size_t bucket_count) {
    std::vector<HistogramBucket> buckets;
    if (values.empty() || bucket_count == 0) {
        return buckets;
    }
    std::sort(values.begin(), values.end());
    int min_value = values.front();
    int max_value = values.back();
    size_t domain_width = static_cast<size_t>(max_value - min_value) + 1;
    size_t width = std::max<size_t>(
        1,
        (domain_width + bucket_count - 1) / bucket_count
    );

    for (size_t i = 0; i < bucket_count; i++) {
        int lower = min_value + static_cast<int>(i * width);
        if (lower > max_value) {
            break;
        }
        int upper = std::min(max_value, lower + static_cast<int>(width) - 1);
        buckets.push_back({lower, upper, 0});
    }

    for (int value : values) {
        size_t index = std::min(
            buckets.size() - 1,
            static_cast<size_t>(value - min_value) / width
        );
        buckets[index].count++;
    }
    return buckets;
}

std::vector<HistogramBucket> buildEquiDepthBuckets(std::vector<int> values,
                                                   size_t bucket_count) {
    std::vector<HistogramBucket> buckets;
    if (values.empty() || bucket_count == 0) {
        return buckets;
    }
    std::sort(values.begin(), values.end());
    bucket_count = std::min(bucket_count, values.size());
    for (size_t i = 0; i < bucket_count; i++) {
        size_t begin = i * values.size() / bucket_count;
        size_t end = ((i + 1) * values.size() / bucket_count) - 1;
        buckets.push_back({
            values[begin],
            values[end],
            end - begin + 1
        });
    }
    return buckets;
}

constexpr size_t HISTOGRAM_BUCKET_COUNT = 5;
constexpr size_t MCV_VALUE_LIMIT = 10;

std::map<std::string, size_t> topKValueCounts(
    const std::map<std::string, size_t>& value_counts,
    size_t limit) {
    std::vector<std::pair<std::string, size_t>> values(
        value_counts.begin(),
        value_counts.end()
    );
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first < right.first;
    });

    std::map<std::string, size_t> top_values;
    for (size_t i = 0; i < values.size() && i < limit; i++) {
        top_values[values[i].first] = values[i].second;
    }
    return top_values;
}

TableStats analyzeTableStats(TableMetadata& metadata,
                             BufferManager& buffer_manager) {
    TableStats table_stats;
    table_stats.pageCount = metadata.page_ids.size();

    std::vector<std::unordered_set<std::string>> distinct_values(
        metadata.schema.columns.size()
    );
    std::vector<std::map<std::string, size_t>> value_counts(
        metadata.schema.columns.size()
    );
    std::vector<std::vector<int>> int_values(metadata.schema.columns.size());
    TableHeap table(metadata, buffer_manager);
    ScanOperator scan(table);
    scan.open();
    while (scan.next()) {
        const auto& tuple = scan.getOutput();
        table_stats.rowCount++;
        for (size_t i = 0; i < tuple.fields.size(); i++) {
            auto value_text = fieldToString(*tuple.fields[i]);
            auto column_name = statsColumnName(metadata, i);
            auto& column_stats = table_stats.columns[column_name];
            distinct_values[i].insert(value_text);
            value_counts[i][value_text]++;
            if (tuple.fields[i]->getType() == INT) {
                auto value = tuple.fields[i]->asInt();
                int_values[i].push_back(value);
                if (!column_stats.hasIntRange) {
                    column_stats.minInt = value;
                    column_stats.maxInt = value;
                    column_stats.hasIntRange = true;
                } else {
                    column_stats.minInt = std::min(column_stats.minInt, value);
                    column_stats.maxInt = std::max(column_stats.maxInt, value);
                }
            }
        }
    }
    scan.close();

    for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
        auto column_name = statsColumnName(metadata, i);
        auto& column_stats = table_stats.columns[column_name];
        column_stats.type = metadata.schema.columns[i].type;
        column_stats.ndv = distinct_values[i].size();
        column_stats.mcvCounts = topKValueCounts(
            value_counts[i],
            MCV_VALUE_LIMIT
        );
        if (column_stats.type == INT) {
            column_stats.equiWidthBuckets = buildEquiWidthBuckets(
                int_values[i],
                HISTOGRAM_BUCKET_COUNT
            );
            column_stats.equiDepthBuckets = buildEquiDepthBuckets(
                int_values[i],
                HISTOGRAM_BUCKET_COUNT
            );
        }
    }
    return table_stats;
}

std::vector<PersistedColumnStats> makePersistedStats(
    const TableMetadata& metadata,
    const TableStats& table_stats) {
    std::vector<PersistedColumnStats> records;
    for (size_t i = 0; i < metadata.schema.columns.size(); i++) {
        const auto& column = metadata.schema.columns[i];
        const auto& column_stats = table_stats.columns.at(column.name);
        records.push_back({
            metadata.table_id,
            static_cast<int>(i),
            table_stats.rowCount,
            table_stats.pageCount,
            column_stats.ndv,
            column_stats.hasIntRange,
            column_stats.minInt,
            column_stats.maxInt,
            column_stats.mcvCounts,
            column_stats.equiWidthBuckets,
            column_stats.equiDepthBuckets
        });
    }
    return records;
}

StatisticsCatalog loadQueryTableStats(const QueryComponents& components,
                                      Catalog& catalog) {
    StatisticsCatalog stats;
    std::set<std::string> loaded_tables;

    for (const auto& table_name : writtenJoinOrder(components)) {
        std::string actual_table = actualTableName(components, table_name);
        if (loaded_tables.find(actual_table) != loaded_tables.end()) {
            continue;
        }
        loaded_tables.insert(actual_table);

        auto& metadata = catalog.getTable(actual_table);
        auto records = catalog.loadColumnStats(metadata.table_id);
        if (records.size() < metadata.schema.columns.size()) {
            throw std::runtime_error(
                "Missing optimizer stats for table " + actual_table +
                ". Run ANALYZE first."
            );
        }

        TableStats table_stats;
        for (const auto& record : records) {
            if (record.column_id < 0 ||
                static_cast<size_t>(record.column_id) >= metadata.schema.columns.size()) {
                continue;
            }
            table_stats.rowCount = record.row_count;
            table_stats.pageCount = record.page_count;
            const auto& column = metadata.schema.columns[record.column_id];
            auto& column_stats = table_stats.columns[column.name];
            column_stats.type = column.type;
            column_stats.ndv = record.ndv;
            column_stats.hasIntRange = record.has_int_range;
            column_stats.minInt = record.min_int;
            column_stats.maxInt = record.max_int;
            column_stats.mcvCounts = record.mcv_counts;
            column_stats.equiWidthBuckets = record.equi_width_buckets;
            column_stats.equiDepthBuckets = record.equi_depth_buckets;
        }
        stats.tables[actual_table] = std::move(table_stats);
    }
    return stats;
}

double estimateRowsAfterTableFilters(const StatisticsCatalog& stats,
                                     const QueryComponents& components,
                                     const std::string& table_name,
                                     bool use_mcv = true) {
    double rows = static_cast<double>(
        tableStatsFor(stats, components, table_name).rowCount
    );
    for (const auto& filter : components.filters) {
        if (filter.column.tableName == table_name) {
            const auto& column_stats = columnStatsFor(stats, components, filter.column);
            if (use_mcv) {
                rows = estimateEqualityFilterRowsWithMcv(
                    rows,
                    column_stats,
                    filter.value
                );
            } else {
                rows = estimateEqualityFilterRows(rows, column_stats);
            }
        }
    }
    return rows;
}

struct PhysicalJoinCostStep {
    JoinClause join;
    double leftRows = 0.0;
    double leftPages = 0.0;
    double leftTotalCost = 0.0;
    double rightRows = 0.0;
    double rightPages = 0.0;
    double rightAccessCost = 0.0;
    double outputRows = 0.0;
    double outputPages = 0.0;
    double nestedLoopCost = 0.0;
    double hashJoinCost = 0.0;
    double sortMergeCost = 0.0;
    bool orderedOutputRequired = false;
    std::string leftRequiredOrder = "unordered";
    std::string rightRequiredOrder = "unordered";
    std::string deliveredOrder = "unordered";
    PhysicalJoinKind chosen = PhysicalJoinKind::HashJoin;
};

struct PhysicalJoinPlan {
    std::vector<PhysicalJoinKind> joinKinds;
    std::vector<PhysicalJoinCostStep> steps;
    double finalRows = 0.0;
    double finalPages = 0.0;
    double totalCost = 0.0;
    std::string deliveredProperty = "unordered";
    size_t sortEnforcers = 0;
    double enforcerCost = 0.0;
    std::vector<ColumnRef> finalSortColumns;
};

struct JoinPlanNode {
    bool isLeaf = true;
    std::string tableName;
    std::shared_ptr<JoinPlanNode> left;
    std::shared_ptr<JoinPlanNode> right;
    JoinClause join;
    PhysicalJoinKind joinKind = PhysicalJoinKind::HashJoin;
    std::vector<std::string> tables;
    double rows = 0.0;
    double pages = 0.0;
    double totalCost = 0.0;
};

struct PlanSnapshot {
    size_t transformations = 0;
    double estimatedCost = 0.0;
    QueryComponents components;
    std::vector<PhysicalJoinKind> joinKinds;
    std::shared_ptr<JoinPlanNode> planRoot;
    std::string order;
};

struct PlannedQuery {
    QueryComponents components;
    PhysicalJoinPlan physicalPlan;
    std::shared_ptr<JoinPlanNode> planRoot;
    std::shared_ptr<PhysicalPlanNode> physicalRoot;
    std::string planDescription;
    size_t dpStatesKept = 0;
    size_t dpCandidatesConsidered = 0;
    size_t dpCrossProductsPruned = 0;
    std::vector<PlanSnapshot> annealingCheckpoints;

    PlannedQuery() = default;

    PlannedQuery(QueryComponents components,
                 PhysicalJoinPlan physicalPlan,
                 std::shared_ptr<JoinPlanNode> planRoot = nullptr,
                 std::string planDescription = "",
                 std::shared_ptr<PhysicalPlanNode> physicalRoot = nullptr)
        : components(std::move(components)),
          physicalPlan(std::move(physicalPlan)),
          planRoot(std::move(planRoot)),
          physicalRoot(std::move(physicalRoot)),
          planDescription(std::move(planDescription)) {}
};

double pagesAfterFilters(const TableStats& table_stats, double filtered_rows) {
    if (table_stats.rowCount == 0 || table_stats.pageCount == 0) {
        return 1.0;
    }
    auto page_fraction = filtered_rows / static_cast<double>(table_stats.rowCount);
    return std::max(1.0, static_cast<double>(table_stats.pageCount) * page_fraction);
}

double fileScanCost(const TableStats& table_stats) {
    return std::max(1.0, static_cast<double>(table_stats.pageCount));
}

double joinedOutputPages(double output_rows,
                         double left_rows,
                         double left_pages,
                         double right_rows,
                         double right_pages) {
    double left_rows_per_page = std::max(1.0, left_rows / std::max(1.0, left_pages));
    double right_rows_per_page = std::max(1.0, right_rows / std::max(1.0, right_pages));
    double output_rows_per_page = std::max(
        1.0,
        std::min(left_rows_per_page, right_rows_per_page) / 2.0
    );
    return std::max(1.0, output_rows / output_rows_per_page);
}

double sortCost(double pages) {
    return 4.0 * std::max(1.0, pages);
}

double tupleCompareCost(double rows) {
    return 0.01 * std::max(1.0, rows);
}

double tupleHashCost(double rows) {
    return 0.05 * std::max(1.0, rows);
}

double tupleMaterializationCost(double rows) {
    return 0.10 * std::max(1.0, rows);
}

PhysicalJoinKind chooseCheapestJoin(const PhysicalJoinCostStep& step) {
    PhysicalJoinKind chosen = PhysicalJoinKind::HashJoin;
    double best_cost = step.hashJoinCost;

    if (step.nestedLoopCost < best_cost) {
        best_cost = step.nestedLoopCost;
        chosen = PhysicalJoinKind::NestedLoopJoin;
    }
    if (step.sortMergeCost < best_cost) {
        chosen = PhysicalJoinKind::SortMergeJoin;
    }
    return chosen;
}

double costForKind(const PhysicalJoinCostStep& step, PhysicalJoinKind kind) {
    switch (kind) {
        case PhysicalJoinKind::NestedLoopJoin:
            return step.nestedLoopCost;
        case PhysicalJoinKind::HashJoin:
            return step.hashJoinCost;
        case PhysicalJoinKind::SortMergeJoin:
            return step.sortMergeCost;
    }
    return step.hashJoinCost;
}

PhysicalJoinCostStep estimatePhysicalJoinTrees(const QueryComponents& components,
                                               const StatisticsCatalog& stats,
                                               const JoinClause& join,
                                               double left_rows,
                                               double left_pages,
                                               double left_total_cost,
                                               double right_rows,
                                               double right_pages,
                                               double right_total_cost,
                                               bool ordered_output_required = false) {
    double output_rows = estimateJoinRows(
        left_rows,
        right_rows,
        columnStatsFor(stats, components, join.left),
        columnStatsFor(stats, components, join.right)
    );
    double output_pages = joinedOutputPages(
        output_rows,
        left_rows,
        left_pages,
        right_rows,
        right_pages
    );

    PhysicalJoinCostStep step;
    step.join = join;
    step.leftRows = left_rows;
    step.leftPages = left_pages;
    step.leftTotalCost = left_total_cost;
    step.rightRows = right_rows;
    step.rightPages = right_pages;
    step.rightAccessCost = right_total_cost;
    step.outputRows = output_rows;
    step.outputPages = output_pages;
    double final_sort_cost = ordered_output_required ? sortCost(output_pages) : 0.0;
    step.orderedOutputRequired = ordered_output_required;
    step.nestedLoopCost = left_total_cost + right_total_cost +
        tupleCompareCost(left_rows * right_rows) +
        tupleMaterializationCost(output_rows) + final_sort_cost;
    step.hashJoinCost = left_total_cost + right_total_cost +
        tupleHashCost(left_rows + right_rows) +
        tupleMaterializationCost(output_rows) + final_sort_cost;
    step.sortMergeCost = left_total_cost + right_total_cost +
        sortCost(left_pages) + sortCost(right_pages) +
        tupleCompareCost(left_rows + right_rows) +
        tupleMaterializationCost(output_rows);
    step.chosen = chooseCheapestJoin(step);
    return step;
}

std::vector<JoinClause> joinGraphEdges(const QueryComponents& components) {
    return queryJoinEdges(components);
}

bool planContainsTable(const JoinPlanNode& node, const std::string& table_name) {
    return std::find(node.tables.begin(), node.tables.end(), table_name) !=
        node.tables.end();
}

std::optional<JoinClause> joinEdgeBetweenPlans(const JoinPlanNode& left,
                                               const JoinPlanNode& right,
                                               const std::vector<JoinClause>& edges) {
    for (const auto& edge : edges) {
        bool left_has_left = planContainsTable(left, edge.left.tableName);
        bool left_has_right = planContainsTable(left, edge.right.tableName);
        bool right_has_left = planContainsTable(right, edge.left.tableName);
        bool right_has_right = planContainsTable(right, edge.right.tableName);
        if ((left_has_left && right_has_right) ||
            (left_has_right && right_has_left)) {
            return edge;
        }
    }
    return std::nullopt;
}

std::string joinPlanTreeString(const std::shared_ptr<JoinPlanNode>& node) {
    if (!node) {
        return "";
    }
    if (node->isLeaf) {
        return node->tableName;
    }
    std::string left_tree = joinPlanTreeString(node->left);
    std::string right_tree = joinPlanTreeString(node->right);
    if (node->joinKind == PhysicalJoinKind::SortMergeJoin) {
        return "SortMergeJoin(Sort(" + left_tree + "), Sort(" + right_tree + "))";
    }
    return physicalJoinKindName(node->joinKind) + "(" +
        left_tree + ", " + right_tree + ")";
}

PhysicalPlanNode::Kind physicalJoinNodeKind(PhysicalJoinKind kind) {
    switch (kind) {
        case PhysicalJoinKind::NestedLoopJoin:
            return PhysicalPlanNode::Kind::NESTED_LOOP_JOIN;
        case PhysicalJoinKind::HashJoin:
            return PhysicalPlanNode::Kind::HASH_JOIN;
        case PhysicalJoinKind::SortMergeJoin:
            return PhysicalPlanNode::Kind::SORT_MERGE_JOIN;
    }
    return PhysicalPlanNode::Kind::HASH_JOIN;
}

std::shared_ptr<PhysicalPlanNode> makePhysicalNode(
    PhysicalPlanNode::Kind kind,
    std::vector<std::shared_ptr<PhysicalPlanNode>> inputs = {}) {
    auto node = std::make_shared<PhysicalPlanNode>();
    node->kind = kind;
    node->inputs = std::move(inputs);
    return node;
}

bool hasTableFilter(const QueryComponents& components,
                    const std::string& table_name) {
    return std::any_of(
        components.filters.begin(),
        components.filters.end(),
        [&](const FilterClause& filter) {
            return filter.column.tableName == table_name;
        }
    );
}

std::shared_ptr<PhysicalPlanNode> physicalScanForTable(
    const QueryComponents& components,
    const std::string& table_name,
    double rows,
    double cost) {
    auto scan = makePhysicalNode(PhysicalPlanNode::Kind::SCAN);
    scan->tableName = table_name;
    scan->rows = rows;
    scan->cost = cost;
    if (!hasTableFilter(components, table_name)) {
        return scan;
    }

    auto filter = makePhysicalNode(PhysicalPlanNode::Kind::FILTER, {scan});
    filter->rows = rows;
    filter->cost = cost;
    return filter;
}

std::shared_ptr<PhysicalPlanNode> physicalPlanFromJoinTree(
    const QueryComponents& components,
    const std::shared_ptr<JoinPlanNode>& node) {
    if (!node) {
        return nullptr;
    }
    if (node->isLeaf) {
        return physicalScanForTable(
            components,
            node->tableName,
            node->rows,
            node->totalCost
        );
    }

    auto left = physicalPlanFromJoinTree(components, node->left);
    auto right = physicalPlanFromJoinTree(components, node->right);
    if (node->joinKind == PhysicalJoinKind::SortMergeJoin) {
        left = makePhysicalNode(PhysicalPlanNode::Kind::SORT, {left});
        right = makePhysicalNode(PhysicalPlanNode::Kind::SORT, {right});
    }

    auto join = makePhysicalNode(physicalJoinNodeKind(node->joinKind), {left, right});
    join->join = node->join;
    join->rows = node->rows;
    join->cost = node->totalCost;
    return join;
}

std::shared_ptr<PhysicalPlanNode> buildPhysicalPlanTree(
    const QueryComponents& components,
    const std::shared_ptr<JoinPlanNode>& plan_root,
    const PhysicalJoinPlan& physical_plan) {
    auto root = physicalPlanFromJoinTree(components, plan_root);
    if (!root) {
        root = physicalScanForTable(
            components,
            components.tableName,
            physical_plan.finalRows,
            physical_plan.totalCost
        );
    }

    if (components.whereCondition ||
        components.equalityWhereCondition ||
        !components.columnEqualities.empty()) {
        root = makePhysicalNode(PhysicalPlanNode::Kind::FILTER, {root});
    }

    if (hasAggregateProjection(components) || components.sumOperation || components.groupBy) {
        root = makePhysicalNode(PhysicalPlanNode::Kind::HASH_AGGREGATE, {root});
    } else if (!components.selectColumns.empty()) {
        root = makePhysicalNode(PhysicalPlanNode::Kind::PROJECT, {root});
    }

    if (!physical_plan.finalSortColumns.empty()) {
        auto sort = makePhysicalNode(PhysicalPlanNode::Kind::SORT, {root});
        sort->sortColumns = physical_plan.finalSortColumns;
        root = sort;
    }

    root->rows = physical_plan.finalRows;
    root->cost = physical_plan.totalCost;
    return root;
}

std::string physicalPlanExpression(const std::shared_ptr<PhysicalPlanNode>& node) {
    if (!node) {
        return "";
    }
    auto name = physicalNodeName(node->kind);
    if (node->kind == PhysicalPlanNode::Kind::SCAN) {
        return name + "(" + node->tableName + ")";
    }
    if (node->inputs.empty()) {
        return name;
    }

    std::vector<std::string> input_expressions;
    for (const auto& input : node->inputs) {
        input_expressions.push_back(physicalPlanExpression(input));
    }
    return name + "(" + joinStrings(input_expressions, ", ") + ")";
}

void appendPhysicalPlanTree(const std::shared_ptr<PhysicalPlanNode>& node,
                            const std::string& indent,
                            std::ostringstream& out) {
    if (!node) {
        return;
    }
    out << indent << physicalNodeName(node->kind);
    if (node->kind == PhysicalPlanNode::Kind::SCAN) {
        out << "(" << node->tableName << ")";
    }
    if (node->kind == PhysicalPlanNode::Kind::SORT &&
        !node->sortColumns.empty()) {
        out << " by {" << columnLabel(node->sortColumns.front()) << "}";
    }
    if (node->rows > 0.0 || node->cost > 0.0) {
        out << "  # rows=" << formatEstimate(node->rows)
            << " cost=" << formatEstimate(node->cost);
    }
    out << "\n";
    for (const auto& input : node->inputs) {
        appendPhysicalPlanTree(input, indent + "  ", out);
    }
}

std::string physicalPlanTreeString(
    const std::shared_ptr<PhysicalPlanNode>& root,
    const std::string& indent = "") {
    std::ostringstream out;
    appendPhysicalPlanTree(root, indent, out);
    return out.str();
}

PlannedQuery makeBasePlanForTable(const QueryComponents& components,
                                  const StatisticsCatalog& stats,
                                  const std::string& table_name) {
    QueryComponents planned = components;
    planned.joins.clear();
    planned.tableName = table_name;
    planned.baseTableName = actualTableName(components, table_name);

    const auto& base_stats = tableStatsFor(stats, components, table_name);
    PhysicalJoinPlan physical_plan;
    physical_plan.finalRows = estimateRowsAfterTableFilters(
        stats,
        components,
        table_name
    );
    physical_plan.finalPages = pagesAfterFilters(
        base_stats,
        physical_plan.finalRows
    );
    physical_plan.totalCost = fileScanCost(base_stats);

    auto node = std::make_shared<JoinPlanNode>();
    node->isLeaf = true;
    node->tableName = table_name;
    node->tables = {table_name};
    node->rows = physical_plan.finalRows;
    node->pages = physical_plan.finalPages;
    node->totalCost = physical_plan.totalCost;
    return {planned, physical_plan, node};
}

std::string orderPropertyForColumn(const ColumnRef& column) {
    return "ordered by {" + columnLabel(column) + "}";
}

struct PhysicalPropertySet {
    std::optional<ColumnRef> orderedBy;

    bool requiresOrder() const {
        return orderedBy.has_value();
    }

    std::string describe() const {
        return orderedBy ? orderPropertyForColumn(*orderedBy) : "unordered";
    }
};

PhysicalPropertySet unorderedProperty() {
    return {};
}

PhysicalPropertySet orderedByProperty(const ColumnRef& column) {
    PhysicalPropertySet property;
    property.orderedBy = column;
    return property;
}

using PlanTraitSet = PhysicalPropertySet;

struct MemoWinnerSearch {
    GroupId finalGroupId = INVALID_GROUP_ID;
    std::string requiredTrait = "unordered";
    std::string deliveredTrait = "unordered";
    std::string expression;
    double cost = 0.0;
    PlannedQuery plannedQuery;
};

struct MemoPlanChoice {
    std::shared_ptr<JoinPlanNode> planRoot;
    std::shared_ptr<PhysicalPlanNode> physicalRoot;
    PhysicalJoinPlan physicalPlan;

    MemoPlanChoice() = default;

    MemoPlanChoice(std::shared_ptr<JoinPlanNode> planRoot,
                   PhysicalJoinPlan physicalPlan,
                   std::shared_ptr<PhysicalPlanNode> physicalRoot = nullptr)
        : planRoot(std::move(planRoot)),
          physicalRoot(std::move(physicalRoot)),
          physicalPlan(std::move(physicalPlan)) {}
};

MemoPlanChoice addSortEnforcer(MemoPlanChoice choice,
                               const PhysicalPropertySet& required_property) {
    if (!required_property.requiresOrder() ||
        choice.physicalPlan.deliveredProperty == required_property.describe()) {
        return choice;
    }

    double enforcer_cost = sortCost(choice.physicalPlan.finalPages);
    choice.physicalPlan.totalCost += enforcer_cost;
    choice.physicalPlan.enforcerCost += enforcer_cost;
    choice.physicalPlan.sortEnforcers++;
    choice.physicalPlan.finalSortColumns = {*required_property.orderedBy};
    choice.physicalPlan.deliveredProperty = required_property.describe();
    return choice;
}

class OptimizerAlgebra {
public:
    virtual ~OptimizerAlgebra() = default;

    virtual std::string name() const = 0;
    virtual std::vector<std::string> logicalOperators() const = 0;
    virtual std::vector<std::string> physicalAlgorithms() const = 0;

    virtual Memo buildInitialMemo(const QueryComponents& components) const = 0;

    virtual PlannedQuery makeBasePlan(const QueryComponents& components,
                                      const StatisticsCatalog& stats,
                                      const std::string& table_name) const = 0;

    virtual PhysicalJoinCostStep estimateJoin(
        const QueryComponents& components,
        const StatisticsCatalog& stats,
        const JoinClause& join,
        double left_rows,
        double left_pages,
        double left_total_cost,
        double right_rows,
        double right_pages,
        double right_access_cost,
        bool ordered_output_required) const = 0;
};

class BuzzDBOptimizerAlgebra : public OptimizerAlgebra {
public:
    std::string name() const override {
        return "BuzzDB relational algebra";
    }

    std::vector<std::string> logicalOperators() const override {
        return {
            "LogicalScan",
            "LogicalFilter",
            "LogicalEquiJoin",
            "LogicalProject",
            "LogicalAggregate"
        };
    }

    std::vector<std::string> physicalAlgorithms() const override {
        return {
            "Scan",
            "Filter",
            "NestedLoopJoin",
            "HashJoin",
            "SortMergeJoin",
            "HashAggregate"
        };
    }

    Memo buildInitialMemo(const QueryComponents& components) const override {
        auto logical_plan = buildRandomStartLogicalPlan(components);
        return buildMemo(*logical_plan);
    }

    PlannedQuery makeBasePlan(const QueryComponents& components,
                              const StatisticsCatalog& stats,
                              const std::string& table_name) const override {
        return makeBasePlanForTable(components, stats, table_name);
    }

    PhysicalJoinCostStep estimateJoin(
        const QueryComponents& components,
        const StatisticsCatalog& stats,
        const JoinClause& join,
        double left_rows,
        double left_pages,
        double left_total_cost,
        double right_rows,
        double right_pages,
        double right_access_cost,
        bool ordered_output_required) const override {
        return estimatePhysicalJoinTrees(
            components,
            stats,
            join,
            left_rows,
            left_pages,
            left_total_cost,
            right_rows,
            right_pages,
            right_access_cost,
            ordered_output_required
        );
    }
};

PhysicalJoinPlan combineMemoJoinPlans(const PhysicalJoinPlan& left_plan,
                                      const PhysicalJoinPlan& right_plan,
                                      const PhysicalJoinCostStep& step) {
    PhysicalJoinPlan plan;
    plan.joinKinds = left_plan.joinKinds;
    plan.joinKinds.insert(
        plan.joinKinds.end(),
        right_plan.joinKinds.begin(),
        right_plan.joinKinds.end()
    );
    plan.joinKinds.push_back(step.chosen);

    plan.steps = left_plan.steps;
    plan.steps.insert(
        plan.steps.end(),
        right_plan.steps.begin(),
        right_plan.steps.end()
    );
    plan.steps.push_back(step);
    plan.totalCost = costForKind(step, step.chosen);
    plan.finalRows = step.outputRows;
    plan.finalPages = step.outputPages;
    plan.sortEnforcers = left_plan.sortEnforcers + right_plan.sortEnforcers;
    plan.enforcerCost = left_plan.enforcerCost + right_plan.enforcerCost;
    plan.deliveredProperty = step.deliveredOrder;
    return plan;
}

std::optional<PhysicalJoinKind> memoImplementationJoinKind(const std::string& op) {
    if (op == "NestedLoopJoin") {
        return PhysicalJoinKind::NestedLoopJoin;
    }
    if (op == "HashJoin") {
        return PhysicalJoinKind::HashJoin;
    }
    if (op == "SortMergeJoin") {
        return PhysicalJoinKind::SortMergeJoin;
    }
    return std::nullopt;
}

struct SearchStrategyStats {
    size_t optimizeGroupTasks = 0;
    size_t applyRuleTasks = 0;
    size_t optimizeInputTasks = 0;
    size_t enforceSortTasks = 0;
    size_t expressionsVisited = 0;
    size_t expressionsPrunedByCostBound = 0;
    size_t rulesSkippedByProperty = 0;
    size_t winnerCacheHits = 0;
    size_t taskBudget = 0;
    size_t tasksExecuted = 0;
    size_t firstRootPlanTasks = 0;
    double finalCostBound = 0.0;
    bool stoppedByTaskBudget = false;
};

std::optional<MemoPlanChoice> chooseMemoGroupPlan(
    const Memo& memo,
    const QueryComponents& components,
    const StatisticsCatalog& stats,
    const OptimizerAlgebra& algebra,
    GroupId group_id,
    const PhysicalPropertySet& required_property,
    const std::vector<JoinClause>& edges,
    std::map<std::pair<GroupId, std::string>, MemoPlanChoice>& winners,
    std::set<std::pair<GroupId, std::string>>& active_groups,
    SearchStrategyStats* search_stats = nullptr) {
    auto required_property_name = required_property.describe();
    auto cache_key = std::make_pair(group_id, required_property_name);
    auto cached = winners.find(cache_key);
    if (cached != winners.end()) {
        if (search_stats) {
            search_stats->winnerCacheHits++;
        }
        return cached->second;
    }
    if (active_groups.find(cache_key) != active_groups.end()) {
        return std::nullopt;
    }
    active_groups.insert(cache_key);
    if (search_stats) {
        search_stats->optimizeGroupTasks++;
    }

    const auto& group = memo.allGroups()[group_id - 1];
    std::optional<MemoPlanChoice> best;
    auto unordered_property_value = unorderedProperty();

    for (const auto& expression : group.expressions) {
        if (search_stats) {
            search_stats->applyRuleTasks++;
            search_stats->expressionsVisited++;
        }
        std::optional<MemoPlanChoice> candidate;

        if (expression.op == "Scan") {
            auto tables = memoPropertySet(group.logicalProperty, "tables");
            if (tables.size() == 1) {
                auto base = algebra.makeBasePlan(
                    components,
                    stats,
                    *tables.begin()
                );
                candidate = MemoPlanChoice{
                    base.planRoot,
                    base.physicalPlan
                };
            }
        } else if ((expression.op == "Filter" ||
                    expression.op == "HashAggregate") &&
                   expression.inputs.size() == 1) {
            if (search_stats) {
                search_stats->optimizeInputTasks++;
            }
            candidate = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                required_property,
                edges,
                winners,
                active_groups,
                search_stats
            );
        } else if (auto join_kind = memoImplementationJoinKind(expression.op);
                   join_kind && expression.inputs.size() == 2) {
            if (search_stats) {
                search_stats->optimizeInputTasks++;
            }
            auto left = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                unordered_property_value,
                edges,
                winners,
                active_groups,
                search_stats
            );
            if (search_stats) {
                search_stats->optimizeInputTasks++;
            }
            auto right = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[1],
                unordered_property_value,
                edges,
                winners,
                active_groups,
                search_stats
            );
            if (left && right) {
                auto edge = joinEdgeBetweenPlans(
                    *left->planRoot,
                    *right->planRoot,
                    edges
                );
                if (edge) {
                    PhysicalPropertySet left_required;
                    PhysicalPropertySet right_required;
                    ColumnRef delivered_order_column;
                    if (*join_kind == PhysicalJoinKind::SortMergeJoin) {
                        if (planContainsTable(*left->planRoot, edge->left.tableName)) {
                            left_required = orderedByProperty(edge->left);
                            right_required = orderedByProperty(edge->right);
                            delivered_order_column = edge->left;
                        } else {
                            left_required = orderedByProperty(edge->right);
                            right_required = orderedByProperty(edge->left);
                            delivered_order_column = edge->right;
                        }

                        if (search_stats) {
                            search_stats->optimizeInputTasks++;
                        }
                        left = chooseMemoGroupPlan(
                            memo,
                            components,
                            stats,
                            algebra,
                            expression.inputs[0],
                            left_required,
                            edges,
                            winners,
                            active_groups,
                            search_stats
                        );
                        if (search_stats) {
                            search_stats->optimizeInputTasks++;
                        }
                        right = chooseMemoGroupPlan(
                            memo,
                            components,
                            stats,
                            algebra,
                            expression.inputs[1],
                            right_required,
                            edges,
                            winners,
                            active_groups,
                            search_stats
                        );
                        if (!left || !right) {
                            continue;
                        }
                    }

                    auto step = algebra.estimateJoin(
                        components,
                        stats,
                        *edge,
                        left->planRoot->rows,
                        left->planRoot->pages,
                        left->planRoot->totalCost,
                        right->planRoot->rows,
                        right->planRoot->pages,
                        right->planRoot->totalCost,
                        false
                    );
                    step.chosen = *join_kind;
                    step.leftRequiredOrder = left_required.describe();
                    step.rightRequiredOrder = right_required.describe();
                    step.deliveredOrder = *join_kind == PhysicalJoinKind::SortMergeJoin
                        ? orderPropertyForColumn(delivered_order_column)
                        : std::string("unordered");

                    auto joined = std::make_shared<JoinPlanNode>();
                    joined->isLeaf = false;
                    joined->left = left->planRoot;
                    joined->right = right->planRoot;
                    joined->join = *edge;
                    joined->joinKind = *join_kind;
                    joined->tables = joined->left->tables;
                    joined->tables.insert(
                        joined->tables.end(),
                        joined->right->tables.begin(),
                        joined->right->tables.end()
                    );
                    joined->rows = step.outputRows;
                    joined->pages = step.outputPages;
                    auto physical_plan = combineMemoJoinPlans(
                        left->physicalPlan,
                        right->physicalPlan,
                        step
                    );
                    joined->totalCost = physical_plan.totalCost;

                    candidate = MemoPlanChoice{
                        joined,
                        physical_plan
                    };
                }
            }
        }

        if (candidate) {
            auto before_sort_count = candidate->physicalPlan.sortEnforcers;
            candidate = addSortEnforcer(*candidate, required_property);
            if (search_stats &&
                candidate->physicalPlan.sortEnforcers > before_sort_count) {
                search_stats->enforceSortTasks++;
            }
        }
        if (candidate &&
            (!best ||
             candidate->physicalPlan.totalCost <
                 best->physicalPlan.totalCost)) {
            best = candidate;
        }
    }

    active_groups.erase(cache_key);
    if (best) {
        winners[cache_key] = *best;
    }
    return best;
}

MemoWinnerSearch chooseMemoWinner(Memo& memo,
                                  const QueryComponents& components,
                                  const StatisticsCatalog& stats,
                                  const OptimizerAlgebra& algebra,
                                  const PlanTraitSet& required_traits,
                                  SearchStrategyStats* search_stats = nullptr) {
    auto edges = joinGraphEdges(components);
    std::map<std::pair<GroupId, std::string>, MemoPlanChoice> group_winners;
    std::set<std::pair<GroupId, std::string>> active_groups;
    auto final_choice = chooseMemoGroupPlan(
        memo,
        components,
        stats,
        algebra,
        memo.finalGroupId(),
        required_traits,
        edges,
        group_winners,
        active_groups,
        search_stats
    );
    if (!final_choice) {
        throw std::runtime_error("Memo winner search found no executable expression.");
    }
    PlannedQuery planned_query{
        components,
        final_choice->physicalPlan,
        final_choice->planRoot,
        joinPlanTreeString(final_choice->planRoot),
        buildPhysicalPlanTree(
            components,
            final_choice->planRoot,
            final_choice->physicalPlan
        )
    };
    MemoWinnerSearch winner;
    winner.finalGroupId = memo.finalGroupId();
    winner.requiredTrait = required_traits.describe();
    winner.deliveredTrait = planned_query.physicalPlan.deliveredProperty;
    winner.expression = physicalPlanExpression(planned_query.physicalRoot);
    winner.cost = planned_query.physicalPlan.totalCost;
    winner.plannedQuery = planned_query;
    if (winner.finalGroupId != 0) {
        memo.setWinner(winner.finalGroupId, {
            winner.requiredTrait,
            winner.expression,
            winner.cost
        });
    }
    return winner;
}

class SearchSpace {
public:
    virtual ~SearchSpace() = default;

    virtual std::string name() const = 0;
    virtual std::vector<std::string> rules() const = 0;
    virtual void expand(Memo& memo,
                        const QueryComponents& components,
                        MemoTransformationStats& stats) const = 0;
};

class MemoRewriteSearchSpace : public SearchSpace {
public:
    std::string name() const override {
        return "matcher-driven memo rewrite and implementation search space";
    }

    std::vector<std::string> rules() const override {
        return {
            "FILTER_PUSH_DOWN",
            "JOIN_PREDICATE_ATTACH",
            "EQJOIN_COMMUTE",
            "EQJOIN_LTOR",
            "EQJOIN_RTOL",
            "LOGICAL_SCAN_TO_SCAN",
            "SELECT_TO_FILTER",
            "AGG_TO_HASH_AGG",
            "EQJOIN_TO_LOOPS_JOIN",
            "EQJOIN_TO_HASH_JOIN",
            "EQJOIN_TO_MERGE_JOIN"
        };
    }

    void expand(Memo& memo,
                const QueryComponents& components,
                MemoTransformationStats& stats) const override {
        applyMemoTransformationRules(memo, components, stats);
    }
};

struct OptimizerContext {
    Memo& memo;
    const QueryComponents& components;
    const StatisticsCatalog& stats;
    const OptimizerAlgebra& algebra;
    const PlanTraitSet& requiredTraits;
    MemoTransformationStats* memoStats = nullptr;
};

struct StrategyComparison {
    std::string name;
    PlannedQuery plannedQuery;
    SearchStrategyStats stats;
    long long optimizationMicros = 0;
};

PlannedQuery plannedQueryFromChoice(const QueryComponents& components,
                                    const MemoPlanChoice& choice) {
    return {
        components,
        choice.physicalPlan,
        choice.planRoot,
        joinPlanTreeString(choice.planRoot),
        choice.physicalRoot
            ? choice.physicalRoot
            : buildPhysicalPlanTree(
                  components,
                  choice.planRoot,
                  choice.physicalPlan
              )
    };
}

class SearchStrategy {
public:
    virtual ~SearchStrategy() = default;
    virtual std::string name() const = 0;
    virtual PlannedQuery optimize(OptimizerContext& context) = 0;
    virtual SearchStrategyStats stats() const {
        return {};
    }
};

class BottomUpMemoSearchStrategy : public SearchStrategy {
    SearchStrategyStats lastStats;

public:
    std::string name() const override {
        return "BottomUpMemo";
    }

    PlannedQuery optimize(OptimizerContext& context) override {
        lastStats = {};
        if (context.memoStats) {
            applyMemoTransformationRules(
                context.memo,
                context.components,
                *context.memoStats
            );
        }
        auto planned_query = chooseMemoWinner(
            context.memo,
            context.components,
            context.stats,
            context.algebra,
            context.requiredTraits,
            &lastStats
        ).plannedQuery;
        return planned_query;
    }

    SearchStrategyStats stats() const override {
        return lastStats;
    }
};

enum class CascadesTaskKind {
    OptimizeGroup,
    ApplyRule,
    OptimizeInput
};

std::optional<JoinClause> joinEdgeBetweenTableSets(
    const std::set<std::string>& left_tables,
    const std::set<std::string>& right_tables,
    const std::vector<JoinClause>& edges) {
    for (const auto& edge : edges) {
        if ((left_tables.count(edge.left.tableName) &&
             right_tables.count(edge.right.tableName)) ||
            (left_tables.count(edge.right.tableName) &&
             right_tables.count(edge.left.tableName))) {
            return edge;
        }
    }
    return std::nullopt;
}

struct CascadesTask {
    CascadesTaskKind kind = CascadesTaskKind::OptimizeGroup;
    GroupId groupId = INVALID_GROUP_ID;
    PhysicalPropertySet requiredProperty;
    size_t expressionIndex = 0;
    std::string name;

    static CascadesTask optimizeGroup(GroupId group_id,
                                      PhysicalPropertySet property) {
        return {CascadesTaskKind::OptimizeGroup, group_id, property, 0, ""};
    }

    static CascadesTask applyRule(GroupId group_id,
                                  PhysicalPropertySet property,
                                  size_t expression_index,
                                  const std::string& rule_name) {
        return {
            CascadesTaskKind::ApplyRule,
            group_id,
            property,
            expression_index,
            rule_name
        };
    }

    static CascadesTask optimizeInput(GroupId group_id,
                                      PhysicalPropertySet property) {
        return {CascadesTaskKind::OptimizeInput, group_id, property, 0, ""};
    }
};

class CascadesSearchStrategy : public SearchStrategy {
    SearchStrategyStats lastStats;
    std::map<std::pair<GroupId, std::string>, MemoPlanChoice> groupWinners;
    std::map<std::pair<GroupId, std::string>, size_t> expandedExpressionCounts;
    std::map<std::pair<GroupId, std::string>, size_t> ruleExpandedExpressionCounts;
    std::map<GroupId, std::vector<CascadesTask>> parentTasks;
    size_t taskBudget = 0;
    GroupId rootGroupId = INVALID_GROUP_ID;
    std::string rootProperty;
    double costBound = std::numeric_limits<double>::infinity();

    std::pair<GroupId, std::string> keyFor(
        GroupId group_id,
        const PhysicalPropertySet& property) const {
        return {group_id, property.describe()};
    }

    std::pair<GroupId, std::string> ruleKeyFor(
        GroupId group_id,
        const PhysicalPropertySet& property,
        bool include_transformations) const {
        return {
            group_id,
            property.describe() +
                (include_transformations ? " full-rules" : " implementation-first")
        };
    }

    bool hasWinner(GroupId group_id,
                   const PhysicalPropertySet& property) const {
        return groupWinners.find(keyFor(group_id, property)) != groupWinners.end();
    }

    const MemoPlanChoice& winnerFor(GroupId group_id,
                                    const PhysicalPropertySet& property) const {
        return groupWinners.at(keyFor(group_id, property));
    }

    bool needsOptimization(const Memo& memo,
                           GroupId group_id,
                           const PhysicalPropertySet& property) const {
        auto key = keyFor(group_id, property);
        auto expanded = expandedExpressionCounts.find(key);
        size_t expression_count =
            memo.allGroups()[group_id - 1].expressions.size();
        return expanded == expandedExpressionCounts.end() ||
               expanded->second < expression_count;
    }

    bool recordCandidate(GroupId group_id,
                         const PhysicalPropertySet& required_property,
                         MemoPlanChoice candidate) {
        auto before_sort_count = candidate.physicalPlan.sortEnforcers;
        candidate = addSortEnforcer(std::move(candidate), required_property);
        if (candidate.physicalPlan.sortEnforcers > before_sort_count) {
            lastStats.enforceSortTasks++;
        }

        if (candidate.physicalPlan.totalCost >= costBound) {
            lastStats.expressionsPrunedByCostBound++;
            return false;
        }

        bool is_root_candidate =
            group_id == rootGroupId &&
            required_property.describe() == rootProperty;
        auto key = keyFor(group_id, required_property);
        auto winner = groupWinners.find(key);
        if (winner == groupWinners.end() ||
            candidate.physicalPlan.totalCost <
                winner->second.physicalPlan.totalCost) {
            if (is_root_candidate) {
                costBound = candidate.physicalPlan.totalCost;
                lastStats.finalCostBound = costBound;
            }
            groupWinners[key] = std::move(candidate);
            return true;
        }
        return false;
    }

    void scheduleParentTasks(GroupId group_id,
                             std::deque<CascadesTask>& task_queue) {
        auto parents = parentTasks.find(group_id);
        if (parents == parentTasks.end()) {
            return;
        }
        for (const auto& parent_task : parents->second) {
            task_queue.push_back(parent_task);
        }
    }

    void scheduleMissingInput(std::deque<CascadesTask>& task_queue,
                              const CascadesTask& retry_task,
                              GroupId group_id,
                              PhysicalPropertySet property) {
        parentTasks[group_id].push_back(retry_task);
        task_queue.push_front(retry_task);
        task_queue.push_front(CascadesTask::optimizeInput(group_id, property));
    }

    void applyRulesForGroup(OptimizerContext& context,
                            GroupId group_id,
                            const PhysicalPropertySet& required_property,
                            bool include_transformations) {
        auto rules = memoTransformationRules();
        auto expansion_key = ruleKeyFor(
            group_id,
            required_property,
            include_transformations
        );
        size_t processed_expressions = 0;
        auto processed = ruleExpandedExpressionCounts.find(expansion_key);
        if (processed != ruleExpandedExpressionCounts.end()) {
            processed_expressions = processed->second;
        }
        auto required_property_name = required_property.describe();
        auto unordered_property_name = unorderedProperty().describe();
        if (context.memoStats) {
            context.memoStats->initialGroups = context.memoStats->initialGroups == 0
                ? context.memo.groupCount()
                : context.memoStats->initialGroups;
            context.memoStats->initialExpressions =
                context.memoStats->initialExpressions == 0
                    ? context.memo.expressionCount()
                    : context.memoStats->initialExpressions;
        }

        bool made_progress = true;
        while (made_progress) {
            made_progress = false;
            auto groups_snapshot = context.memo.allGroups();
            if (group_id == INVALID_GROUP_ID ||
                static_cast<size_t>(group_id) > groups_snapshot.size()) {
                return;
            }
            const auto group = groups_snapshot[group_id - 1];
            if (processed_expressions >= group.expressions.size()) {
                break;
            }

            size_t start_index = processed_expressions;
            processed_expressions = group.expressions.size();
            std::vector<RuleKind> phases = include_transformations
                ? std::vector<RuleKind>{RuleKind::Transformation, RuleKind::Implementation}
                : std::vector<RuleKind>{RuleKind::Implementation};
            for (RuleKind phase : phases) {
                for (size_t i = start_index; i < group.expressions.size(); i++) {
                    const auto& expression = group.expressions[i];
                    for (const auto& rule : rules) {
                        if (rule.kind != phase ||
                            !matchRulePattern(rule.pattern, expression)) {
                            continue;
                        }
                        if (required_property_name == unordered_property_name &&
                            rule.name == "EQJOIN_TO_MERGE_JOIN") {
                            lastStats.rulesSkippedByProperty++;
                            continue;
                        }

                        auto before_groups = context.memo.groupCount();
                        auto before_expressions = context.memo.expressionCount();
                        RuleBinding binding{
                            context.memo,
                            context.components,
                            groups_snapshot,
                            group,
                            expression
                        };
                        auto generated = rule.apply(binding);
                        for (auto& generated_expression : generated) {
                            context.memo.addExpressionToGroup(
                                group.id,
                                std::move(generated_expression)
                            );
                        }

                        bool rule_changed =
                            context.memo.groupCount() != before_groups ||
                            context.memo.expressionCount() != before_expressions;
                        made_progress = made_progress || rule_changed;
                        if (context.memoStats &&
                            (rule_changed ||
                             rule.name == "FILTER_PUSH_DOWN" ||
                             rule.name == "JOIN_PREDICATE_ATTACH")) {
                            recordMemoRule(
                                *context.memoStats,
                                rule.name,
                                ruleFireDetail(rule, group)
                            );
                        }
                    }
                }
            }
        }
        ruleExpandedExpressionCounts[expansion_key] = processed_expressions;

        if (context.memoStats) {
            context.memoStats->finalGroups = context.memo.groupCount();
            context.memoStats->finalExpressions = context.memo.expressionCount();
            context.memoStats->mergedGroups = context.memo.mergedGroupCount();
            context.memoStats->deduplicatedExpressions =
                context.memo.deduplicatedExpressionCount();
        }
    }

    void applyExpression(OptimizerContext& context,
                         const std::vector<JoinClause>& edges,
                         const CascadesTask& task,
                         std::deque<CascadesTask>& task_queue) {
        const auto& group = context.memo.allGroups()[task.groupId - 1];
        if (task.expressionIndex >= group.expressions.size()) {
            return;
        }

        const auto& expression = group.expressions[task.expressionIndex];
        lastStats.applyRuleTasks++;
        lastStats.expressionsVisited++;

        if (expression.op == "Scan") {
            auto tables = memoPropertySet(group.logicalProperty, "tables");
            if (tables.size() == 1) {
                auto base = context.algebra.makeBasePlan(
                    context.components,
                    context.stats,
                    *tables.begin()
                );
                if (recordCandidate(
                    task.groupId,
                    task.requiredProperty,
                    MemoPlanChoice{base.planRoot, base.physicalPlan}
                )) {
                    scheduleParentTasks(task.groupId, task_queue);
                }
            }
            return;
        }

        if ((expression.op == "Filter" ||
             expression.op == "HashAggregate") &&
            expression.inputs.size() == 1) {
            if (!hasWinner(expression.inputs[0], task.requiredProperty) ||
                needsOptimization(context.memo, expression.inputs[0], task.requiredProperty)) {
                scheduleMissingInput(
                    task_queue,
                    task,
                    expression.inputs[0],
                    task.requiredProperty
                );
                return;
            }
            if (recordCandidate(
                task.groupId,
                task.requiredProperty,
                winnerFor(expression.inputs[0], task.requiredProperty)
            )) {
                scheduleParentTasks(task.groupId, task_queue);
            }
            return;
        }

        auto join_kind = memoImplementationJoinKind(expression.op);
        if (!join_kind || expression.inputs.size() != 2) {
            return;
        }

        const auto& left_group = context.memo.allGroups()[expression.inputs[0] - 1];
        const auto& right_group = context.memo.allGroups()[expression.inputs[1] - 1];
        auto left_tables = memoPropertySet(left_group.logicalProperty, "tables");
        auto right_tables = memoPropertySet(right_group.logicalProperty, "tables");
        auto edge = joinEdgeBetweenTableSets(left_tables, right_tables, edges);
        if (!edge) {
            return;
        }

        auto left_required = unorderedProperty();
        auto right_required = unorderedProperty();
        ColumnRef delivered_order_column;
        if (*join_kind == PhysicalJoinKind::SortMergeJoin) {
            if (left_tables.count(edge->left.tableName)) {
                left_required = orderedByProperty(edge->left);
                right_required = orderedByProperty(edge->right);
                delivered_order_column = edge->left;
            } else {
                left_required = orderedByProperty(edge->right);
                right_required = orderedByProperty(edge->left);
                delivered_order_column = edge->right;
            }
        }

        if (!hasWinner(expression.inputs[0], left_required) ||
            needsOptimization(context.memo, expression.inputs[0], left_required)) {
            scheduleMissingInput(
                task_queue,
                task,
                expression.inputs[0],
                left_required
            );
            return;
        }
        if (!hasWinner(expression.inputs[1], right_required) ||
            needsOptimization(context.memo, expression.inputs[1], right_required)) {
            scheduleMissingInput(
                task_queue,
                task,
                expression.inputs[1],
                right_required
            );
            return;
        }

        auto left = winnerFor(expression.inputs[0], left_required);
        auto right = winnerFor(expression.inputs[1], right_required);
        auto step = context.algebra.estimateJoin(
            context.components,
            context.stats,
            *edge,
            left.planRoot->rows,
            left.planRoot->pages,
            left.planRoot->totalCost,
            right.planRoot->rows,
            right.planRoot->pages,
            right.planRoot->totalCost,
            false
        );
        step.chosen = *join_kind;
        step.leftRequiredOrder = left_required.describe();
        step.rightRequiredOrder = right_required.describe();
        step.deliveredOrder = *join_kind == PhysicalJoinKind::SortMergeJoin
            ? orderPropertyForColumn(delivered_order_column)
            : std::string("unordered");

        auto joined = std::make_shared<JoinPlanNode>();
        joined->isLeaf = false;
        joined->left = left.planRoot;
        joined->right = right.planRoot;
        joined->join = *edge;
        joined->joinKind = *join_kind;
        joined->tables = joined->left->tables;
        joined->tables.insert(
            joined->tables.end(),
            joined->right->tables.begin(),
            joined->right->tables.end()
        );
        joined->rows = step.outputRows;
        joined->pages = step.outputPages;

        auto plan = combineMemoJoinPlans(left.physicalPlan, right.physicalPlan, step);
        joined->totalCost = plan.totalCost;

        if (recordCandidate(
            task.groupId,
            task.requiredProperty,
            MemoPlanChoice{joined, plan}
        )) {
            scheduleParentTasks(task.groupId, task_queue);
        }
    }

public:
    explicit CascadesSearchStrategy(size_t task_budget = 0)
        : taskBudget(task_budget) {}

    std::string name() const override {
        return taskBudget > 0
            ? "Cascades(cost-bound, soft-task-budget=" + std::to_string(taskBudget) + ")"
            : "Cascades(cost-bound)";
    }

    PlannedQuery optimize(OptimizerContext& context) override {
        lastStats = {};
        lastStats.taskBudget = taskBudget;
        groupWinners.clear();
        expandedExpressionCounts.clear();
        ruleExpandedExpressionCounts.clear();
        parentTasks.clear();
        rootGroupId = context.memo.finalGroupId();
        rootProperty = context.requiredTraits.describe();
        costBound = std::numeric_limits<double>::infinity();

        auto edges = joinGraphEdges(context.components);
        std::deque<CascadesTask> task_queue;
        bool full_exploration_enabled = false;
        task_queue.push_back(
            CascadesTask::optimizeGroup(
                context.memo.finalGroupId(),
                context.requiredTraits
            )
        );

        while (!task_queue.empty()) {
            auto task = task_queue.front();
            task_queue.pop_front();
            lastStats.tasksExecuted++;

            if (task.kind == CascadesTaskKind::OptimizeGroup) {
                auto group_key = keyFor(task.groupId, task.requiredProperty);
                size_t current_expression_count =
                    context.memo.allGroups()[task.groupId - 1].expressions.size();
                size_t previous_expression_count = 0;
                auto expanded_count = expandedExpressionCounts.find(group_key);
                if (expanded_count != expandedExpressionCounts.end()) {
                    previous_expression_count = expanded_count->second;
                }
                if (previous_expression_count >= current_expression_count) {
                    continue;
                }
                lastStats.optimizeGroupTasks++;
                applyRulesForGroup(
                    context,
                    task.groupId,
                    task.requiredProperty,
                    full_exploration_enabled
                );
                const auto& group = context.memo.allGroups()[task.groupId - 1];
                std::vector<size_t> expression_order(group.expressions.size());
                for (size_t i = previous_expression_count;
                     i < expression_order.size();
                     i++) {
                    expression_order[i] = i;
                }
                expression_order.erase(
                    expression_order.begin(),
                    expression_order.begin() +
                        static_cast<long>(previous_expression_count)
                );
                std::stable_sort(
                    expression_order.begin(),
                    expression_order.end(),
                    [&](size_t left, size_t right) {
                        const auto& left_op = group.expressions[left].op;
                        const auto& right_op = group.expressions[right].op;
                        auto promise = [](const std::string& op) {
                            if (op == "HashJoin") {
                                return 100;
                            }
                            if (op == "Scan" ||
                                op == "Filter" ||
                                op == "HashAggregate") {
                                return 90;
                            }
                            if (op == "NestedLoopJoin") {
                                return 80;
                            }
                            if (op == "SortMergeJoin") {
                                return 70;
                            }
                            return 10;
                        };
                        return promise(left_op) > promise(right_op);
                    }
                );
                expandedExpressionCounts[group_key] = group.expressions.size();

                for (auto it = expression_order.rbegin();
                     it != expression_order.rend();
                     ++it) {
                    size_t i = *it;
                    const auto& expression = group.expressions[i];
                    if (task.requiredProperty.describe() == unorderedProperty().describe() &&
                        expression.op == "SortMergeJoin") {
                        continue;
                    }
                    task_queue.push_front(
                        CascadesTask::applyRule(
                            task.groupId,
                            task.requiredProperty,
                            i,
                            expression.op
                        )
                    );
                }
            } else if (task.kind == CascadesTaskKind::ApplyRule) {
                bool had_root_winner =
                    hasWinner(context.memo.finalGroupId(), context.requiredTraits);
                applyExpression(context, edges, task, task_queue);
                if (!had_root_winner &&
                    hasWinner(context.memo.finalGroupId(), context.requiredTraits)) {
                    lastStats.firstRootPlanTasks = lastStats.tasksExecuted;
                    full_exploration_enabled = true;
                    expandedExpressionCounts.clear();
                    ruleExpandedExpressionCounts.clear();
                    task_queue.push_back(
                        CascadesTask::optimizeGroup(
                            context.memo.finalGroupId(),
                            context.requiredTraits
                        )
                    );
                }
            } else if (task.kind == CascadesTaskKind::OptimizeInput) {
                lastStats.optimizeInputTasks++;
                if (!hasWinner(task.groupId, task.requiredProperty) ||
                    needsOptimization(context.memo, task.groupId, task.requiredProperty)) {
                    task_queue.push_front(
                        CascadesTask::optimizeGroup(
                            task.groupId,
                            task.requiredProperty
                        )
                    );
                }
            }

            if (taskBudget > 0 &&
                lastStats.tasksExecuted >= taskBudget &&
                hasWinner(context.memo.finalGroupId(), context.requiredTraits)) {
                lastStats.stoppedByTaskBudget = true;
                break;
            }
        }

        auto final_key = keyFor(context.memo.finalGroupId(), context.requiredTraits);
        auto final_choice = groupWinners.find(final_key);
        if (final_choice == groupWinners.end()) {
            throw std::runtime_error("Cascades search found no executable expression.");
        }
        return plannedQueryFromChoice(context.components, final_choice->second);
    }

    SearchStrategyStats stats() const override {
        return lastStats;
    }
};

std::optional<MemoPlanChoice> chooseRandomMemoGroupPlan(
    const Memo& memo,
    const QueryComponents& components,
    const StatisticsCatalog& stats,
    const OptimizerAlgebra& algebra,
    GroupId group_id,
    const PhysicalPropertySet& required_property,
    const std::vector<JoinClause>& edges,
    std::set<std::pair<GroupId, std::string>>& active_groups,
    std::mt19937& rng) {
    auto active_key = std::make_pair(group_id, required_property.describe());
    if (active_groups.find(active_key) != active_groups.end()) {
        return std::nullopt;
    }
    active_groups.insert(active_key);

    const auto& group = memo.allGroups()[group_id - 1];
    std::vector<size_t> expression_indexes(group.expressions.size());
    for (size_t i = 0; i < expression_indexes.size(); i++) {
        expression_indexes[i] = i;
    }
    std::shuffle(expression_indexes.begin(), expression_indexes.end(), rng);

    auto unordered_property_value = unorderedProperty();
    for (size_t expression_index : expression_indexes) {
        const auto& expression = group.expressions[expression_index];
        if (expression.op == "Scan") {
            auto tables = memoPropertySet(group.logicalProperty, "tables");
            if (tables.size() == 1) {
                auto base = algebra.makeBasePlan(components, stats, *tables.begin());
                active_groups.erase(active_key);
                return addSortEnforcer(
                    MemoPlanChoice{base.planRoot, base.physicalPlan},
                    required_property
                );
            }
        } else if ((expression.op == "Filter" ||
                    expression.op == "HashAggregate") &&
                   expression.inputs.size() == 1) {
            auto child = chooseRandomMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                required_property,
                edges,
                active_groups,
                rng
            );
            if (child) {
                active_groups.erase(active_key);
                return child;
            }
        } else if (auto join_kind = memoImplementationJoinKind(expression.op);
                   join_kind && expression.inputs.size() == 2) {
            auto left = chooseRandomMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                unordered_property_value,
                edges,
                active_groups,
                rng
            );
            auto right = chooseRandomMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[1],
                unordered_property_value,
                edges,
                active_groups,
                rng
            );
            if (!left || !right) {
                continue;
            }
            auto edge = joinEdgeBetweenPlans(*left->planRoot, *right->planRoot, edges);
            if (!edge) {
                continue;
            }
            auto step = algebra.estimateJoin(
                components,
                stats,
                *edge,
                left->planRoot->rows,
                left->planRoot->pages,
                left->planRoot->totalCost,
                right->planRoot->rows,
                right->planRoot->pages,
                right->planRoot->totalCost,
                false
            );
            step.chosen = *join_kind;
            step.deliveredOrder = *join_kind == PhysicalJoinKind::SortMergeJoin
                ? required_property.describe()
                : std::string("unordered");

            auto joined = std::make_shared<JoinPlanNode>();
            joined->isLeaf = false;
            joined->left = left->planRoot;
            joined->right = right->planRoot;
            joined->join = *edge;
            joined->joinKind = *join_kind;
            joined->tables = joined->left->tables;
            joined->tables.insert(
                joined->tables.end(),
                joined->right->tables.begin(),
                joined->right->tables.end()
            );
            joined->rows = step.outputRows;
            joined->pages = step.outputPages;

            auto plan = combineMemoJoinPlans(
                left->physicalPlan,
                right->physicalPlan,
                step
            );
            joined->totalCost = plan.totalCost;

            active_groups.erase(active_key);
            return addSortEnforcer(MemoPlanChoice{joined, plan}, required_property);
        }
    }

    active_groups.erase(active_key);
    return std::nullopt;
}

class RandomizedSearchStrategy : public SearchStrategy {
    SearchStrategyStats lastStats;

public:
    std::string name() const override {
        return "Randomized";
    }

    PlannedQuery optimize(OptimizerContext& context) override {
        lastStats = {};
        if (context.memoStats) {
            applyMemoTransformationRules(
                context.memo,
                context.components,
                *context.memoStats
            );
        }
        auto edges = joinGraphEdges(context.components);
        std::set<std::pair<GroupId, std::string>> active_groups;
        std::mt19937 rng(7);
        auto choice = chooseRandomMemoGroupPlan(
            context.memo,
            context.components,
            context.stats,
            context.algebra,
            context.memo.finalGroupId(),
            context.requiredTraits,
            edges,
            active_groups,
            rng
        );
        if (!choice) {
            return BottomUpMemoSearchStrategy().optimize(context);
        }
        return plannedQueryFromChoice(context.components, *choice);
    }

    SearchStrategyStats stats() const override {
        return lastStats;
    }
};

struct OptimizerResult {
    QueryComponents logicalQuery;
    Memo memo;
    MemoTransformationStats memoStats;
    PlannedQuery plannedQuery;
    PlanTraitSet requiredTraits;
    MemoWinnerSearch memoWinner;
    std::string algebraName;
    std::string searchSpaceName;
    std::vector<StrategyComparison> strategyComparisons;
    std::vector<std::string> logicalOperators;
    std::vector<std::string> physicalAlgorithms;
    std::vector<std::string> searchSpaceRules;
};

class Optimizer {
    const StatisticsCatalog& stats;
    PlanTraitSet requiredTraits;
    BuzzDBOptimizerAlgebra algebra;
    MemoRewriteSearchSpace searchSpace;

public:
    Optimizer(const StatisticsCatalog& stats,
              PlanTraitSet requiredTraits = {})
        : stats(stats),
          requiredTraits(requiredTraits) {}

    OptimizerResult optimize(const QueryComponents& components) const {
        auto initial_memo = algebra.buildInitialMemo(components);

        std::vector<std::unique_ptr<SearchStrategy>> strategies;
        strategies.push_back(std::make_unique<BottomUpMemoSearchStrategy>());
        strategies.push_back(std::make_unique<RandomizedSearchStrategy>());
        strategies.push_back(std::make_unique<CascadesSearchStrategy>(1000));
        strategies.push_back(std::make_unique<CascadesSearchStrategy>(10000));

        std::vector<StrategyComparison> comparisons;
        std::vector<Memo> strategy_memos;
        std::vector<MemoTransformationStats> strategy_memo_stats;
        for (auto& strategy : strategies) {
            auto strategy_memo = initial_memo;
            MemoTransformationStats memo_stats;
            memo_stats.initialGroups = strategy_memo.groupCount();
            memo_stats.initialExpressions = strategy_memo.expressionCount();
            OptimizerContext strategy_context{
                strategy_memo,
                components,
                stats,
                algebra,
                requiredTraits,
                &memo_stats
            };
            auto start = std::chrono::high_resolution_clock::now();
            auto planned_query = strategy->optimize(strategy_context);
            auto end = std::chrono::high_resolution_clock::now();
            memo_stats.finalGroups = strategy_memo.groupCount();
            memo_stats.finalExpressions = strategy_memo.expressionCount();
            memo_stats.mergedGroups = strategy_memo.mergedGroupCount();
            memo_stats.deduplicatedExpressions =
                strategy_memo.deduplicatedExpressionCount();
            comparisons.push_back({
                strategy->name(),
                planned_query,
                strategy->stats(),
                std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start
                ).count()
            });
            strategy_memos.push_back(std::move(strategy_memo));
            strategy_memo_stats.push_back(std::move(memo_stats));
        }

        auto selected = std::find_if(
            comparisons.begin(),
            comparisons.end(),
            [](const StrategyComparison& comparison) {
                return comparison.name == "Cascades(cost-bound, soft-task-budget=10000)";
            }
        );
        if (selected == comparisons.end()) {
            selected = comparisons.begin();
        }
        size_t selected_index = static_cast<size_t>(
            std::distance(comparisons.begin(), selected)
        );
        auto selected_memo = strategy_memos[selected_index];
        auto selected_memo_stats = strategy_memo_stats[selected_index];

        MemoWinnerSearch memo_winner;
        memo_winner.finalGroupId = selected_memo.finalGroupId();
        memo_winner.requiredTrait = requiredTraits.describe();
        memo_winner.deliveredTrait =
            selected->plannedQuery.physicalPlan.deliveredProperty;
        memo_winner.expression = physicalPlanExpression(
            selected->plannedQuery.physicalRoot
        );
        memo_winner.cost = selected->plannedQuery.physicalPlan.totalCost;
        memo_winner.plannedQuery = selected->plannedQuery;
        if (memo_winner.finalGroupId != INVALID_GROUP_ID) {
            selected_memo.setWinner(memo_winner.finalGroupId, {
                memo_winner.requiredTrait,
                memo_winner.expression,
                memo_winner.cost
            });
        }

        return {
            components,
            selected_memo,
            selected_memo_stats,
            memo_winner.plannedQuery,
            requiredTraits,
            memo_winner,
            algebra.name(),
            searchSpace.name(),
            comparisons,
            algebra.logicalOperators(),
            algebra.physicalAlgorithms(),
            searchSpace.rules()
        };
    }
};

std::string physicalPlanTreeString(const QueryComponents& components,
                                   const std::vector<PhysicalJoinKind>& join_kinds) {
    std::string tree = "Scan(" + components.tableName + ")";
    for (size_t i = 0; i < components.joins.size(); i++) {
        auto kind = i < join_kinds.size()
            ? join_kinds[i]
            : PhysicalJoinKind::HashJoin;
        std::string right_tree = "Scan(" + components.joins[i].tableName + ")";
        if (kind == PhysicalJoinKind::SortMergeJoin) {
            tree = "SortMergeJoin(Sort(" + tree + "), Sort(" + right_tree + "))";
        } else {
            tree = physicalJoinKindName(kind) + "(" + tree + ", " + right_tree + ")";
        }
    }
    if (components.whereCondition ||
        components.equalityWhereCondition ||
        !components.columnEqualities.empty()) {
        tree = "Select(" + tree + ")";
    }
    if (hasAggregateProjection(components) || components.sumOperation || components.groupBy) {
        tree = "HashAggregate(" + tree + ")";
    }
    if (hasAggregateProjection(components)) {
        return tree;
    }
    return "Project(" + tree + ")";
}

std::unique_ptr<IPredicate> makeScanFilterPredicate(const QueryComponents& components,
                                                    Catalog& catalog,
                                                    const std::string& table_name) {
    auto predicate = std::make_unique<ComplexPredicate>(ComplexPredicate::LogicOperator::AND);
    bool has_filter = false;
    auto& metadata = catalog.getTable(actualTableName(components, table_name));

    for (const auto& filter : components.filters) {
        if (filter.column.tableName != table_name) {
            continue;
        }
        if (filter.column.attributeIndex < 0 ||
            static_cast<size_t>(filter.column.attributeIndex) >= metadata.schema.columns.size()) {
            throw std::runtime_error("Pushed filter column is out of range.");
        }

        auto filter_field = parseLiteralField(
            metadata.schema.columns[
                static_cast<size_t>(filter.column.attributeIndex)
            ].type,
            filter.value
        );
        predicate->addPredicate(std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(static_cast<size_t>(filter.column.attributeIndex)),
            SimplePredicate::Operand(filter_field.clone()),
            SimplePredicate::ComparisonOperator::EQ
        ));
        has_filter = true;
    }

    if (!has_filter) {
        return nullptr;
    }
    return predicate;
}

QueryTable executeQuery(const QueryComponents& components,
                        Catalog& catalog,
                        BufferManager& buffer_manager,
                        const TxnPtr& txn = nullptr,
                        bool print_tuples = true,
                        const std::vector<PhysicalJoinKind>& join_kinds = {},
                        const std::vector<size_t>& final_sort_attrs = {},
                        const std::shared_ptr<JoinPlanNode>& plan_root = nullptr) {
    std::map<std::string, size_t> table_offsets;
    std::map<std::string, size_t> table_widths;
    std::vector<std::unique_ptr<TableHeap>> heaps;
    std::vector<std::unique_ptr<ScanOperator>> scans;
    std::vector<std::unique_ptr<SelectOperator>> pushedSelects;
    std::vector<std::unique_ptr<SortOperator>> sortOpBuffers;
    std::vector<std::unique_ptr<HashJoinOperator>> hashJoinOpBuffers;
    std::vector<std::unique_ptr<SortMergeJoinOperator>> sortMergeJoinOpBuffers;
    std::vector<std::unique_ptr<NestedLoopJoinOperator>> nestedLoopJoinOpBuffers;

    auto addScan = [&](const std::string& table_name) -> Operator& {
        auto& metadata = catalog.getTable(actualTableName(components, table_name));
        table_widths[table_name] = metadata.schema.columns.size();
        heaps.push_back(std::make_unique<TableHeap>(metadata, buffer_manager));
        scans.push_back(std::make_unique<ScanOperator>(*heaps.back()));
        auto predicate = makeScanFilterPredicate(
            components,
            catalog,
            table_name
        );
        if (predicate) {
            pushedSelects.push_back(std::make_unique<SelectOperator>(
                *scans.back(),
                std::move(predicate)
            ));
            return *pushedSelects.back();
        }
        return *scans.back();
    };

    auto tableOffsetIn = [&](const std::vector<std::string>& tables,
                             const std::string& table_name) {
        size_t offset = 0;
        for (const auto& table : tables) {
            if (table == table_name) {
                return offset;
            }
            offset += table_widths[table];
        }
        throw std::runtime_error("JOIN table is not in this subtree.");
    };

    auto addJoinOperator = [&](Operator& left_op,
                               Operator& right_op,
                               size_t left_attr_index,
                               size_t right_attr_index,
                               PhysicalJoinKind join_kind) -> Operator& {
        if (join_kind == PhysicalJoinKind::NestedLoopJoin) {
            nestedLoopJoinOpBuffers.push_back(std::make_unique<NestedLoopJoinOperator>(
                left_op, right_op, left_attr_index, right_attr_index));
            return *nestedLoopJoinOpBuffers.back();
        }
        if (join_kind == PhysicalJoinKind::SortMergeJoin) {
            sortOpBuffers.push_back(std::make_unique<SortOperator>(
                left_op, std::vector<size_t>{left_attr_index}));
            Operator* sorted_left = sortOpBuffers.back().get();

            sortOpBuffers.push_back(std::make_unique<SortOperator>(
                right_op, std::vector<size_t>{right_attr_index}));
            Operator* sorted_right = sortOpBuffers.back().get();

            sortMergeJoinOpBuffers.push_back(std::make_unique<SortMergeJoinOperator>(
                *sorted_left, *sorted_right, left_attr_index, right_attr_index));
            return *sortMergeJoinOpBuffers.back();
        }
        hashJoinOpBuffers.push_back(std::make_unique<HashJoinOperator>(
            left_op, right_op, left_attr_index, right_attr_index));
        return *hashJoinOpBuffers.back();
    };

    struct BuiltPlanOperator {
        Operator* op = nullptr;
        std::vector<std::string> tables;
        size_t width = 0;
    };

    std::function<BuiltPlanOperator(const std::shared_ptr<JoinPlanNode>&)> buildPlanNode =
        [&](const std::shared_ptr<JoinPlanNode>& node) -> BuiltPlanOperator {
            if (node->isLeaf) {
                Operator& scan = addScan(node->tableName);
                return {&scan, {node->tableName}, table_widths[node->tableName]};
            }

            auto left = buildPlanNode(node->left);
            auto right = buildPlanNode(node->right);
            size_t left_attr_index;
            size_t right_attr_index;
            if (std::find(left.tables.begin(), left.tables.end(), node->join.left.tableName) !=
                    left.tables.end() &&
                std::find(right.tables.begin(), right.tables.end(), node->join.right.tableName) !=
                    right.tables.end()) {
                left_attr_index = tableOffsetIn(left.tables, node->join.left.tableName) +
                    static_cast<size_t>(node->join.left.attributeIndex);
                right_attr_index = tableOffsetIn(right.tables, node->join.right.tableName) +
                    static_cast<size_t>(node->join.right.attributeIndex);
            } else if (std::find(left.tables.begin(), left.tables.end(), node->join.right.tableName) !=
                           left.tables.end() &&
                       std::find(right.tables.begin(), right.tables.end(), node->join.left.tableName) !=
                           right.tables.end()) {
                left_attr_index = tableOffsetIn(left.tables, node->join.right.tableName) +
                    static_cast<size_t>(node->join.right.attributeIndex);
                right_attr_index = tableOffsetIn(right.tables, node->join.left.tableName) +
                    static_cast<size_t>(node->join.left.attributeIndex);
            } else {
                throw std::runtime_error("GOO join edge does not connect the two subtrees.");
            }

            Operator& join_op = addJoinOperator(
                *left.op,
                *right.op,
                left_attr_index,
                right_attr_index,
                node->joinKind
            );
            left.tables.insert(left.tables.end(), right.tables.begin(), right.tables.end());
            return {&join_op, left.tables, left.width + right.width};
        };

    Operator* rootOp = nullptr;
    size_t output_width = 0;
    if (plan_root) {
        auto built = buildPlanNode(plan_root);
        rootOp = built.op;
        output_width = built.width;
        size_t offset = 0;
        for (const auto& table_name : built.tables) {
            table_offsets[table_name] = offset;
            offset += table_widths[table_name];
        }
    } else {
        rootOp = &addScan(components.tableName);
        table_offsets[components.tableName] = 0;
        output_width = table_widths[components.tableName];

        size_t join_index = 0;
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
            auto join_kind = join_index < join_kinds.size()
                ? join_kinds[join_index]
                : PhysicalJoinKind::HashJoin;
            rootOp = &addJoinOperator(
                *rootOp,
                right_scan,
                left_attr_index,
                right_attr_index,
                join_kind
            );
            table_offsets[join.tableName] = output_width;
            output_width += table_widths[join.tableName];
            join_index++;
        }
    }

    // Buffer for optional operators to ensure lifetime
    std::optional<SelectOperator> filterSelectOpBuffer;
    std::optional<SelectOperator> selectOpBuffer;
    std::optional<SelectOperator> equalitySelectOpBuffer;
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

    if (projected_columns.empty() &&
        !hasAggregateProjection(components) &&
        !components.sumOperation &&
        !components.groupBy) {
        for (size_t attr_index = 0; attr_index < output_width; attr_index++) {
            projected_columns.push_back(attr_index);
        }
    }

    if (!components.columnEqualities.empty()) {
        auto filterPredicate = std::make_unique<ComplexPredicate>(ComplexPredicate::LogicOperator::AND);
        for (const auto& equality : components.columnEqualities) {
            auto left_offset_it = table_offsets.find(equality.left.tableName);
            auto right_offset_it = table_offsets.find(equality.right.tableName);
            if (left_offset_it == table_offsets.end() ||
                right_offset_it == table_offsets.end()) {
                throw std::runtime_error("WHERE predicate edge table is not in the query output.");
            }
            filterPredicate->addPredicate(std::make_unique<SimplePredicate>(
                SimplePredicate::Operand(
                    left_offset_it->second + static_cast<size_t>(equality.left.attributeIndex)
                ),
                SimplePredicate::Operand(
                    right_offset_it->second + static_cast<size_t>(equality.right.attributeIndex)
                ),
                SimplePredicate::ComparisonOperator::EQ
            ));
        }
        filterSelectOpBuffer.emplace(*rootOp, std::move(filterPredicate));
        rootOp = &*filterSelectOpBuffer;
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

    if (components.equalityWhereAttributeIndex != -1) {
        auto& base_metadata = catalog.getTable(components.baseTableName);
        auto equality_field = parseLiteralField(
            base_metadata.schema.columns[
                static_cast<size_t>(components.equalityWhereAttributeIndex)
            ].type,
            components.equalityWhereValue
        );
        auto predicate = std::make_unique<SimplePredicate>(
            SimplePredicate::Operand(components.equalityWhereAttributeIndex),
            SimplePredicate::Operand(equality_field.clone()),
            SimplePredicate::ComparisonOperator::EQ
        );
        equalitySelectOpBuffer.emplace(*rootOp, std::move(predicate));
        rootOp = &*equalitySelectOpBuffer;
    }

    if (!final_sort_attrs.empty()) {
        sortOpBuffers.push_back(std::make_unique<SortOperator>(
            *rootOp,
            final_sort_attrs
        ));
        rootOp = sortOpBuffers.back().get();
    }

    // Apply projection aggregates, SUM, or GROUP BY operation
    if (hasAggregateProjection(components)) {
        std::vector<AggrFunc> aggrFuncs;
        for (size_t i = 0; i < projected_columns.size(); i++) {
            aggrFuncs.push_back({*components.selectAggregates[i], projected_columns[i]});
        }
        hashAggOpBuffer.emplace(*rootOp, std::vector<size_t>{}, aggrFuncs);
        rootOp = &*hashAggOpBuffer;
    } else if (components.sumOperation || components.groupBy) {
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

    if (!hasAggregateProjection(components) && !projected_columns.empty()) {
        projectionOpBuffer.emplace(*rootOp, projected_columns);
        rootOp = &*projectionOpBuffer;
    }

    // Execute the Root Operator
    rootOp->setTxnContext(txn);
    rootOp->open();
    QueryTable result;
    while (rootOp->next()) {
        result.push_back({cloneTupleFields(rootOp->getOutput()), rootOp->getTupleId()});
    }
    rootOp->close();
    if (print_tuples) {
        printQueryTable(result);
    }
    return result;
}

class BuzzDB {
public:
    BufferManager buffer_manager;
    Catalog catalog;
    RecoveryManager recovery_manager;
    TransactionManager txn_manager;
    LockManager concurrency_control_lock_manager;
    std::unique_ptr<ConcurrencyControlPolicy> concurrency_control_policy;
    std::mutex execution_latch;
    std::mutex txn_label_latch;
    std::map<int, std::string> txn_labels;
    bool print_concurrency_control = false;
    std::map<std::string, PlannedQuery> planned_query_cache;

    BuzzDB()
        : buffer_manager(),
          catalog(buffer_manager),
          recovery_manager(buffer_manager, catalog) {
        concurrency_control_lock_manager.setWaitObserver(
            [this](int txn_id, const std::vector<int>& blockers) {
                for (int blocker : blockers) {
                    logConcurrencyControl("waits-for edge: " + txnIdLabel(txn_id) +
                          " -> " + txnIdLabel(blocker));
                }
            }
        );
        concurrency_control_policy = std::make_unique<TwoPhaseLockingPolicy>(
            concurrency_control_lock_manager,
            [this](const std::string& line) { logConcurrencyControl(line); },
            [this](const std::vector<int>& path) { return txnPathLabel(path); }
        );
        recovery_manager.setRestartUndoLockCallback(
            [this](TableId table_id, PageID page_id, size_t slot_id) {
                return acquireRestartUndoLock(table_id, page_id, slot_id);
            }
        );
        recovery_manager.setRestartUndoReleaseCallback(
            [this]() {
                releaseRestartUndoLocks();
            }
        );
        catalog.bootstrap();
        if (catalog.isNewDatabase()) {
            recovery_manager.resetLog();
        }
    }

    bool isNewDatabase() const { return catalog.isNewDatabase(); }

    std::vector<std::string> userTableNames() {
        return catalog.listUserTableNames();
    }

    void analyze(const std::string& table_name = "", bool print_output = true) {
        std::vector<std::string> table_names = table_name.empty()
            ? userTableNames()
            : std::vector<std::string>{table_name};
        std::vector<PersistedColumnStats> records;

        for (const auto& name : table_names) {
            auto& metadata = catalog.getTable(name);
            auto table_stats = analyzeTableStats(metadata, buffer_manager);
            auto table_records = makePersistedStats(metadata, table_stats);
            records.insert(records.end(), table_records.begin(), table_records.end());
            if (print_output) {
                std::cout << "  " << name << ": "
                          << table_stats.rowCount << " rows, "
                          << table_stats.pageCount << " pages, "
                          << table_stats.columns.size() << " columns"
                          << std::endl;
            }
        }

        catalog.persistColumnStats(records);
        planned_query_cache.clear();
        if (print_output) {
            std::cout << "ANALYZE persisted optimizer stats for "
                      << table_names.size() << " table(s)" << std::endl;
        }
    }

    void printBufferPoolSummary() const {
        buffer_manager.printBufferPoolSummary();
    }

    void clearBufferPool() {
        buffer_manager.clearBufferPool();
    }

    TxnPtr begin(const std::string& label = "") {
        auto txn = txn_manager.begin(label);
        std::lock_guard<std::mutex> guard(txn_label_latch);
        txn_labels[txn->id] = txnLabel(txn);
        concurrency_control_policy->begin(txn->id, txnLabel(txn));
        return txn;
    }

    TxnPtr beginLoggedTxn(const std::string& label) {
        auto txn = begin(label);
        recovery_manager.beginTxn(txn->id);
        return txn;
    }

    bool commit(const TxnPtr& tx,
                const std::vector<std::string>& buffered_statements = {}) {
        (void)buffered_statements;

        if (recovery_manager.hasTxn(tx->id)) {
            LSN commit_lsn = recovery_manager.queueCommit(tx->id);
            recovery_manager.forceCommitGroupUpTo(commit_lsn);
            printThreadSafe(
                "  recovery: forced COMMIT log through LSN " +
                std::to_string(commit_lsn)
            );
            recovery_manager.finishTxn(tx->id);
        }
        txn_manager.commit(*tx);
        tx->inserted_tuple_ids.clear();
        concurrency_control_policy->commit(tx->id);
        logConcurrencyControl(txnLabel(tx) + " COMMIT; release strict 2PL locks");
        return true;
    }

    void undoInsertedTuples(const TxnPtr& tx) {
        for (auto it = tx->inserted_tuple_ids.rbegin();
             it != tx->inserted_tuple_ids.rend();
             ++it) {
            auto& metadata = catalog.getTable(it->table_id);
            TableHeap table(metadata, buffer_manager);
            if (table.deletePhysicalTuple(
                    it->page_id, it->slot_id, "runtime insert abort undo")) {
                catalog.persistTableMetadata(metadata);
            }
        }
        tx->inserted_tuple_ids.clear();
    }

    void abort(const TxnPtr& tx) {
        if (recovery_manager.hasTxn(tx->id)) {
            recovery_manager.abortTxn(tx->id);
            recovery_manager.finishTxn(tx->id);
        }
        undoInsertedTuples(tx);
        txn_manager.abort(*tx);
        concurrency_control_policy->abort(tx->id);
        logConcurrencyControl(txnLabel(tx) + " ABORT; release strict 2PL locks");
    }

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

    static std::string txnLabel(const TxnPtr& txn) {
        if (!txn) {
            return "system";
        }
        return txn->label.empty() ?
            "T" + std::to_string(txn->id) :
            txn->label;
    }

    std::string txnIdLabel(int txn_id) {
        if (txn_id == RECOVERY_TXN_ID) {
            return "RECOVERY";
        }
        std::lock_guard<std::mutex> guard(txn_label_latch);
        auto it = txn_labels.find(txn_id);
        return it == txn_labels.end() ?
            "T" + std::to_string(txn_id) :
            it->second;
    }

    std::string txnPathLabel(const std::vector<int>& path) {
        std::string label;
        for (int txn_id : path) {
            if (!label.empty()) {
                label += " -> ";
            }
            label += txnIdLabel(txn_id);
        }
        return label.empty() ? "none" : label;
    }

    static std::string tableResourceKey(const std::string& table_name) {
        return "table:" + table_name;
    }

    static std::string tableResourceLabel(const std::string& table_name) {
        return "table " + table_name;
    }

    static ConcurrencyControlResource tableResource(
        const std::string& table_name) {
        return {tableResourceKey(table_name), tableResourceLabel(table_name)};
    }

    static std::string tupleResourceKey(TableId table_id,
                                        PageID page_id,
                                        size_t slot_id) {
        return "tuple:" + std::to_string(table_id) + ":" +
               std::to_string(page_id) + ":" + std::to_string(slot_id);
    }

    static std::string statementTypeName(StatementType type) {
        switch (type) {
            case StatementType::INSERT:
                return "INSERT";
            case StatementType::UPDATE:
                return "UPDATE";
            case StatementType::DELETE:
                return "DELETE";
            default:
                throw std::runtime_error("Statement type does not need a table lock.");
        }
    }

    void logConcurrencyControl(const std::string& line) {
        if (print_concurrency_control) {
            printThreadSafe("  " + line);
        }
    }

    bool acquireTxnAccess(const TxnPtr& txn,
                          AccessType type,
                          const std::vector<ConcurrencyControlResource>& resources,
                          const std::string& reason) {
        if (!txn || resources.empty()) {
            return true;
        }

        auto result = concurrency_control_policy->beforeAccess({
            txn->id,
            txnLabel(txn),
            type,
            resources,
            reason
        });

        if (result.granted) {
            return true;
        }
        if (result.deadlock) {
            logConcurrencyControl("deadlock detected; " + result.reason);
            abort(txn);
            return false;
        }
        if (result.canceled) {
            logConcurrencyControl(txnLabel(txn) + " access canceled; " + result.reason);
            txn_manager.abort(*txn);
            return false;
        }

        logConcurrencyControl(txnLabel(txn) + " access blocked; " + result.reason);
        return false;
    }

    bool acquireRestartUndoLock(TableId table_id, PageID page_id, size_t slot_id) {
        ConcurrencyControlResource resource{
            tupleResourceKey(table_id, page_id, slot_id),
            "tuple " + std::to_string(table_id) + "." +
            std::to_string(page_id) + "." + std::to_string(slot_id)
        };
        auto result = concurrency_control_policy->beforeAccess({
            RECOVERY_TXN_ID,
            "RECOVERY",
            AccessType::Write,
            {resource},
            "physiological restart undo"
        });
        if (result.granted) {
            return false;
        }
        if (!result.deadlock) {
            throw std::runtime_error(
                "Recovery could not reacquire restart undo lock: " + result.reason
            );
        }

        logConcurrencyControl("deadlock during restart undo; " + result.reason);
        for (int txn_id : result.cycle) {
            if (txn_id > 0) {
                concurrency_control_policy->cancel(txn_id);
            }
        }
        auto retry = concurrency_control_policy->beforeAccess({
            RECOVERY_TXN_ID,
            "RECOVERY",
            AccessType::Write,
            {resource},
            "physiological restart undo"
        });
        if (!retry.granted) {
            throw std::runtime_error("Recovery lock retry failed after deadlock.");
        }
        return true;
    }

    void releaseRestartUndoLocks() {
        concurrency_control_policy->abort(RECOVERY_TXN_ID);
    }

    std::vector<std::string> queryTableNames(const QueryComponents& components) {
        std::vector<std::string> table_names{components.baseTableName};
        for (const auto& join : components.joins) {
            if (std::find(table_names.begin(), table_names.end(), join.actualTableName) ==
                table_names.end()) {
                table_names.push_back(join.actualTableName);
            }
        }
        return table_names;
    }

    bool acquireStatementLocks(const TxnPtr& txn,
                               const StatementComponents& components) {
        if (!txn || (components.type != StatementType::INSERT &&
                     components.type != StatementType::UPDATE &&
                     components.type != StatementType::DELETE)) {
            return true;
        }
        return acquireTxnAccess(
            txn,
            AccessType::Write,
            {tableResource(components.tableName)},
            statementTypeName(components.type) + " " + components.tableName
        );
    }

    bool acquireQueryLocks(const TxnPtr& txn,
                           const QueryComponents& components) {
        if (!txn) {
            return true;
        }
        std::vector<ConcurrencyControlResource> resources;
        for (const auto& table_name : queryTableNames(components)) {
            resources.push_back(tableResource(table_name));
        }
        return acquireTxnAccess(
            txn,
            AccessType::Read,
            resources,
            "PROJECT query"
        );
    }

    static StatementComponents parseStatement(const std::string& statement,
                                              size_t line_number = 0) {
        std::regex begin_regex("^\\s*BEGIN\\s*$");
        std::regex commit_regex("^\\s*COMMIT\\s*$");
        std::regex abort_regex("^\\s*ABORT\\s*$");
        std::regex checkpoint_regex("^\\s*CHECKPOINT\\s*$");
        std::regex begin_checkpoint_regex("^\\s*BEGIN\\s+CHECKPOINT\\s*$");
        std::regex end_checkpoint_regex("^\\s*END\\s+CHECKPOINT\\s*$");
        std::regex image_copy_regex("^\\s*IMAGE\\s+COPY\\s*$");
        std::regex analyze_regex(
            "^\\s*ANALYZE(?:\\s+([A-Za-z_][A-Za-z0-9_]*))?\\s*$");
        std::regex savepoint_regex(
            "^\\s*SAVEPOINT\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*$");
        std::regex rollback_to_regex(
            "^\\s*ROLLBACK\\s+TO\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*$");
        std::regex insert_regex(
            "^\\s*INSERT\\s+([A-Za-z_][A-Za-z0-9_]*)\\|(.*)$");
        std::regex update_regex(
            "^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+(.+)\\s+"
            "WHERE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$");
        std::regex delete_regex(
            "^\\s*DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+"
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

        if (std::regex_match(statement, image_copy_regex)) {
            components.type = StatementType::IMAGE_COPY;
            return components;
        }

        if (std::regex_match(statement, matches, analyze_regex)) {
            components.type = StatementType::ANALYZE;
            if (matches[1].matched) {
                components.tableName = matches[1];
            }
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

        if (std::regex_match(statement, matches, delete_regex)) {
            components.type = StatementType::DELETE;
            components.tableName = matches[1];
            components.whereColumn = matches[2];
            components.whereValue = matches[3];
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
                          TableHeap* bulk_table = nullptr,
                          bool acquire_concurrency_control = true) {
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
        if (components.type == StatementType::IMAGE_COPY) {
            recovery_manager.createFuzzyImageCopy();
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
        if (components.type == StatementType::ANALYZE) {
            analyze(components.tableName, print_statement);
            return;
        }

        if (acquire_concurrency_control &&
            !acquireStatementLocks(txn, components)) {
            return;
        }

        std::lock_guard<std::mutex> execution_guard(execution_latch);
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
            bool txn_has_recovery =
                txn && recovery_manager.hasTxn(txn->id);
            UpdateOperator updateOp(
                table,
                static_cast<size_t>(where_column),
                where_value,
                std::move(assignments),
                (txn_has_recovery || recovery_manager.isActive()) ?
                    &recovery_manager :
                    nullptr
            );
            updateOp.setTxnContext(txn);
            updateOp.open();
            updateOp.next();
            updateOp.close();
            return;
        }

        if (components.type == StatementType::DELETE) {
            if (bulk_table != nullptr) {
                throw std::runtime_error("Bulk loading only supports INSERT.");
            }

            int where_column = findColumnIndex(metadata, components.whereColumn);
            auto where_value = parseFieldValue(
                metadata.schema.columns[where_column].type,
                components.whereValue,
                components.tableName,
                components.lineNumber
            );

            TableHeap table(metadata, buffer_manager);
            bool txn_has_recovery =
                txn && recovery_manager.hasTxn(txn->id);
            DeleteOperator deleteOp(
                table,
                static_cast<size_t>(where_column),
                where_value,
                (txn_has_recovery || recovery_manager.isActive()) ?
                    &recovery_manager :
                    nullptr
            );
            deleteOp.setTxnContext(txn);
            deleteOp.open();
            deleteOp.next();
            deleteOp.close();
            if (persist_metadata) {
                catalog.persistTableMetadata(metadata);
            }
            return;
        }

        if (components.type == StatementType::INSERT) {
            if (components.values.size() != metadata.schema.columns.size()) {
                throw std::runtime_error(
                    "Wrong field count for table " + components.tableName +
                    lineContext(line_number)
                );
            }
            bool txn_has_recovery =
                txn && recovery_manager.hasTxn(txn->id);

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

            InsertOperator insertOp(
                *table,
                (txn_has_recovery || recovery_manager.isActive()) ?
                    &recovery_manager :
                    nullptr
            );
            insertOp.setTxnContext(txn);
            insertOp.setTupleToInsert(std::move(tuple));
            insertOp.open();
            bool status = insertOp.next();
            insertOp.close();
            if (!status) {
                throw std::runtime_error(
                    "Tuple is too large to fit in a page for table " +
                    components.tableName + lineContext(line_number)
                );
            }
            if (persist_metadata) {
                catalog.persistTableMetadata(metadata);
            }
            return;
        }

        throw std::runtime_error("Unsupported data statement.");
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
        std::map<std::string, size_t> table_rows;
        std::map<std::string, long long> table_insert_us;
        buffer_manager.setDeferStableStorageForces(true);

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

            auto insert_start = std::chrono::steady_clock::now();
            executeStatement(
                "INSERT " + line,
                txn,
                line_number,
                print_statements,
                false,
                bulk_table->second.get(),
                false
            );
            auto insert_end = std::chrono::steady_clock::now();
            table_rows[table_name]++;
            table_insert_us[table_name] +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    insert_end - insert_start
                ).count();
        }

        auto flush_start = std::chrono::steady_clock::now();
        buffer_manager.flushAllPages("bulk load");
        auto flush_end = std::chrono::steady_clock::now();

        auto catalog_start = std::chrono::steady_clock::now();
        for (const auto& table_name : touched_tables) {
            catalog.persistTableMetadata(catalog.getTable(table_name));
        }
        auto catalog_end = std::chrono::steady_clock::now();

        auto stable_start = std::chrono::steady_clock::now();
        buffer_manager.forceDeferredDatabaseFileToStableStorage();
        buffer_manager.setDeferStableStorageForces(false);
        auto stable_end = std::chrono::steady_clock::now();

        std::cout << "Load profile:" << std::endl;
        for (const auto& table_name : touched_tables) {
            const auto& metadata = catalog.getTable(table_name);
            std::cout << "  " << table_name << ": "
                      << table_rows[table_name] << " rows, "
                      << metadata.page_ids.size() << " pages, "
                      << (table_insert_us[table_name] / 1000)
                      << " ms inserting" << std::endl;
        }
        std::cout << "  bulk flush: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         flush_end - flush_start
                     ).count()
                  << " ms" << std::endl;
        std::cout << "  catalog persist: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         catalog_end - catalog_start
                     ).count()
                  << " ms" << std::endl;
        std::cout << "  final database fsync: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         stable_end - stable_start
                     ).count()
                  << " ms" << std::endl;
    }

    QueryTable executeQuery(const std::string& query,
                            const TxnPtr& txn = nullptr,
                            bool print_tuples = true,
                            const std::vector<PhysicalJoinKind>& forced_join_kinds = {},
                            const std::vector<size_t>& final_sort_attrs = {}) {
        auto components = parseQuery(query);
        resolveQueryColumns(components, catalog);
        if (!acquireQueryLocks(txn, components)) {
            return {};
        }

        std::lock_guard<std::mutex> execution_guard(execution_latch);
        auto& metadata = catalog.getTable(components.baseTableName);
        auto queryColumns = deriveQueryColumns(components, metadata);
        (void)queryColumns;
        std::vector<PhysicalJoinKind> join_kinds;
        QueryComponents planned_components = components;
        std::shared_ptr<JoinPlanNode> planned_root;
        if (!components.joins.empty()) {
            if (!forced_join_kinds.empty()) {
                join_kinds = forced_join_kinds;
            } else {
                try {
                    auto cache_key = query + "#memo";
                    auto cached = planned_query_cache.find(cache_key);
                    PlannedQuery planned_query;
                    if (cached != planned_query_cache.end()) {
                        planned_query = cached->second;
                    } else {
                        auto stats = loadQueryTableStats(components, catalog);
                        Optimizer optimizer(stats);
                        planned_query = optimizer.optimize(components).plannedQuery;
                        planned_query_cache[cache_key] = planned_query;
                    }
                    planned_components = planned_query.components;
                    join_kinds = planned_query.physicalPlan.joinKinds;
                    planned_root = planned_query.planRoot;
                } catch (const std::exception&) {
                    join_kinds.clear();
                }
            }
        }
        if (print_tuples) {
            std::cout << "QUERY "
                      << (planned_root
                          ? joinPlanTreeString(planned_root)
                          : physicalPlanTreeString(planned_components, join_kinds))
                      << std::endl;
        }
        auto result = ::executeQuery(
            planned_components,
            catalog,
            buffer_manager,
            txn,
            print_tuples,
            join_kinds,
            final_sort_attrs,
            planned_root
        );
        return result;
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

};


void createJobTables(BuzzDB& db) {
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

    db.createTable("info_type", {
        {"id", INT},
        {"info", STRING}
    });

    db.createTable("role_type", {
        {"id", INT},
        {"role", STRING}
    });

    db.createTable("name", {
        {"id", INT},
        {"name", STRING},
        {"gender", STRING}
    });

    db.createTable("char_name", {
        {"id", INT},
        {"name", STRING}
    });

    db.createTable("keyword", {
        {"id", INT},
        {"keyword", STRING}
    });

    db.createTable("movie_companies", {
        {"id", INT},
        {"movie_id", INT},
        {"company_id", INT},
        {"company_type_id", INT},
        {"note", STRING}
    });

    db.createTable("movie_info", {
        {"id", INT},
        {"movie_id", INT},
        {"info_type_id", INT},
        {"info", STRING}
    });

    db.createTable("movie_info_idx", {
        {"id", INT},
        {"movie_id", INT},
        {"info_type_id", INT},
        {"info", STRING}
    });

    db.createTable("movie_keyword", {
        {"id", INT},
        {"movie_id", INT},
        {"keyword_id", INT}
    });

    db.createTable("cast_info", {
        {"id", INT},
        {"person_id", INT},
        {"movie_id", INT},
        {"person_role_id", INT},
        {"role_id", INT}
    });
}

// -----------------------------------------------------------------------------
// simulator-facing command API
// -----------------------------------------------------------------------------

struct Address {
    std::string id;

    explicit Address(std::string value = "") : id(std::move(value)) {}

    Address rootAddress() const { return *this; }
    std::string str() const { return id; }

    bool operator<(const Address& other) const { return id < other.id; }
    bool operator==(const Address& other) const { return id == other.id; }
};

std::ostream& operator<<(std::ostream& out, const Address& address) {
    out << address.str();
    return out;
}

struct CreateTableCommand {
    std::string table;
    std::vector<std::string> columns;
};

struct InsertRowCommand {
    std::string table;
    std::vector<std::string> values;
};

struct DeleteRowsCommand {
    std::string table;
    std::string column;
    std::string value;
};

struct UpdateRowsCommand {
    std::string table;
    std::string set_column;
    std::string set_value;
    std::string where_column;
    std::string where_value;
};

struct SelectAllCommand { std::string table; };

struct SelectWhereCommand {
    std::string table;
    std::string column;
    std::string value;
};

struct ExplainRouteCommand {
    std::string table;
    std::string primary_key;
    bool full_scan = false;
};

struct RoutedSQLCommand {
    std::string sql;
};

struct RoutedTransactionCommand {
    std::vector<std::string> statements;
};

struct QuerySQLCommand { std::string sql; };
struct CountRowsCommand { std::string table; };
struct CheckpointCommand {};

struct RegisterClusterNodeCommand {
    std::string node_id;
    std::string role;
    std::string status;
    int epoch = 1;
};

struct RegisterReplicaGroupCommand {
    std::string group_id;
    std::vector<std::string> voters;
    int config_version = 1;
    std::vector<std::string> learners = {};
};

struct RegisterRangeCommand {
    std::string range_id;
    std::string start_key;
    std::string end_key;
    std::string replica_group_id;
    int descriptor_version = 1;
    std::string status = "active";
};

struct SplitRangeCommand {
    std::string source_range_id;
    std::string split_key;
    std::string left_range_id;
    std::string right_range_id;
    int descriptor_version = 1;
};

struct SplitTableCommand {
    std::string table;
    std::string source_range_id;
    std::string left_range_id;
    std::string right_range_id;
    int descriptor_version = 1;
    std::string left_replica_group_id = "";
    std::string right_replica_group_id = "";
};

struct BootstrapClusterCommand {
    std::string cluster_id;
    std::string bootstrap_node_id;
    std::vector<std::string> seed_nodes;
    int epoch = 1;
};

struct DiscoverClusterNodeCommand {
    std::string cluster_id;
    std::string node_id;
    std::string role = "learner";
    std::string status = "discovered";
    int epoch = 1;
};

struct AddLearnerToGroupCommand {
    std::string group_id;
    std::string node_id;
    int config_version = 1;
};

struct MarkLearnerCaughtUpCommand {
    std::string group_id;
    std::string node_id;
    int config_version = 1;
};

struct BeginJointConfigCommand {
    std::string group_id;
    std::vector<std::string> new_voters;
    std::vector<std::string> old_quorum;
    std::vector<std::string> new_quorum;
    int config_version = 1;
};

struct FinalizeJointConfigCommand {
    std::string group_id;
    std::vector<std::string> final_voters;
    std::vector<std::string> old_quorum;
    std::vector<std::string> new_quorum;
    int config_version = 1;
};

struct ReadClusterIdentityCommand {};

struct ReadRangeOwnershipCommand {
    std::string table;
    bool active_only = true;
};

struct JoinReplicaGroupCommand {
    std::string group_id;
    std::vector<std::string> voters;
};

struct LeaveReplicaGroupCommand {
    std::string group_id;
};

struct MoveRangeCommand {
    std::string range_id;
    std::string target_replica_group_id;
};

struct PrepareRangeTransferCommand {
    std::string range_id;
    std::string target_replica_group_id;
    int transfer_epoch = 1;
};

struct CatchUpRangeTransferCommand {
    std::string range_id;
    int transfer_epoch = 1;
};

struct CommitRangeTransferCommand {
    std::string range_id;
    int transfer_epoch = 1;
};

struct ExportRangeRowsCommand {
    std::string table;
    std::string range_id;
    std::string start_key;
    std::string end_key;
};

struct ImportRangeRowsCommand {
    std::string table;
    std::string range_id;
    std::string target_replica_group_id;
    std::string start_key;
    std::string end_key;
    std::vector<std::vector<std::string>> rows;
};

struct DeleteRangeRowsCommand {
    std::string table;
    std::string range_id;
    std::string start_key;
    std::string end_key;
};

struct AbortRangeTransferCommand {
    std::string range_id;
    int transfer_epoch = 1;
};

struct QueryRangeConfigCommand {
    int config_num = -1;
};

struct ReadSystemCatalogCommand { std::string table; };

using Command = std::variant<
    CreateTableCommand,
    InsertRowCommand,
    DeleteRowsCommand,
    UpdateRowsCommand,
    SelectAllCommand,
    SelectWhereCommand,
    ExplainRouteCommand,
    RoutedSQLCommand,
    RoutedTransactionCommand,
    QuerySQLCommand,
    CountRowsCommand,
    CheckpointCommand,
    RegisterClusterNodeCommand,
    RegisterReplicaGroupCommand,
    RegisterRangeCommand,
    SplitRangeCommand,
    SplitTableCommand,
    BootstrapClusterCommand,
    DiscoverClusterNodeCommand,
    AddLearnerToGroupCommand,
    MarkLearnerCaughtUpCommand,
    BeginJointConfigCommand,
    FinalizeJointConfigCommand,
    ReadClusterIdentityCommand,
    ReadRangeOwnershipCommand,
    JoinReplicaGroupCommand,
    LeaveReplicaGroupCommand,
    MoveRangeCommand,
    PrepareRangeTransferCommand,
    CatchUpRangeTransferCommand,
    CommitRangeTransferCommand,
    ExportRangeRowsCommand,
    ImportRangeRowsCommand,
    DeleteRangeRowsCommand,
    AbortRangeTransferCommand,
    QueryRangeConfigCommand,
    ReadSystemCatalogCommand>;

bool operator==(const CreateTableCommand& lhs, const CreateTableCommand& rhs) {
    return lhs.table == rhs.table && lhs.columns == rhs.columns;
}
bool operator==(const InsertRowCommand& lhs, const InsertRowCommand& rhs) {
    return lhs.table == rhs.table && lhs.values == rhs.values;
}
bool operator==(const DeleteRowsCommand& lhs, const DeleteRowsCommand& rhs) {
    return lhs.table == rhs.table && lhs.column == rhs.column &&
           lhs.value == rhs.value;
}
bool operator==(const UpdateRowsCommand& lhs, const UpdateRowsCommand& rhs) {
    return lhs.table == rhs.table && lhs.set_column == rhs.set_column &&
           lhs.set_value == rhs.set_value &&
           lhs.where_column == rhs.where_column &&
           lhs.where_value == rhs.where_value;
}
bool operator==(const SelectAllCommand& lhs, const SelectAllCommand& rhs) {
    return lhs.table == rhs.table;
}
bool operator==(const SelectWhereCommand& lhs, const SelectWhereCommand& rhs) {
    return lhs.table == rhs.table && lhs.column == rhs.column &&
           lhs.value == rhs.value;
}
bool operator==(const ExplainRouteCommand& lhs,
                const ExplainRouteCommand& rhs) {
    return lhs.table == rhs.table && lhs.primary_key == rhs.primary_key &&
           lhs.full_scan == rhs.full_scan;
}
bool operator==(const RoutedSQLCommand& lhs, const RoutedSQLCommand& rhs) {
    return lhs.sql == rhs.sql;
}
bool operator==(const RoutedTransactionCommand& lhs,
                const RoutedTransactionCommand& rhs) {
    return lhs.statements == rhs.statements;
}
bool operator==(const QuerySQLCommand& lhs, const QuerySQLCommand& rhs) {
    return lhs.sql == rhs.sql;
}
bool operator==(const CountRowsCommand& lhs, const CountRowsCommand& rhs) {
    return lhs.table == rhs.table;
}
bool operator==(const CheckpointCommand&, const CheckpointCommand&) {
    return true;
}
bool operator==(const RegisterClusterNodeCommand& lhs,
                const RegisterClusterNodeCommand& rhs) {
    return lhs.node_id == rhs.node_id && lhs.role == rhs.role &&
           lhs.status == rhs.status && lhs.epoch == rhs.epoch;
}
bool operator==(const RegisterReplicaGroupCommand& lhs,
                const RegisterReplicaGroupCommand& rhs) {
    return lhs.group_id == rhs.group_id && lhs.voters == rhs.voters &&
           lhs.config_version == rhs.config_version &&
           lhs.learners == rhs.learners;
}
bool operator==(const RegisterRangeCommand& lhs,
                const RegisterRangeCommand& rhs) {
    return lhs.range_id == rhs.range_id && lhs.start_key == rhs.start_key &&
           lhs.end_key == rhs.end_key &&
           lhs.replica_group_id == rhs.replica_group_id &&
           lhs.descriptor_version == rhs.descriptor_version &&
           lhs.status == rhs.status;
}
bool operator==(const SplitRangeCommand& lhs,
                const SplitRangeCommand& rhs) {
    return lhs.source_range_id == rhs.source_range_id &&
           lhs.split_key == rhs.split_key &&
           lhs.left_range_id == rhs.left_range_id &&
           lhs.right_range_id == rhs.right_range_id &&
           lhs.descriptor_version == rhs.descriptor_version;
}
bool operator==(const SplitTableCommand& lhs,
                const SplitTableCommand& rhs) {
    return lhs.table == rhs.table &&
           lhs.source_range_id == rhs.source_range_id &&
           lhs.left_range_id == rhs.left_range_id &&
           lhs.right_range_id == rhs.right_range_id &&
           lhs.descriptor_version == rhs.descriptor_version &&
           lhs.left_replica_group_id == rhs.left_replica_group_id &&
           lhs.right_replica_group_id == rhs.right_replica_group_id;
}
bool operator==(const BootstrapClusterCommand& lhs,
                const BootstrapClusterCommand& rhs) {
    return lhs.cluster_id == rhs.cluster_id &&
           lhs.bootstrap_node_id == rhs.bootstrap_node_id &&
           lhs.seed_nodes == rhs.seed_nodes &&
           lhs.epoch == rhs.epoch;
}
bool operator==(const DiscoverClusterNodeCommand& lhs,
                const DiscoverClusterNodeCommand& rhs) {
    return lhs.cluster_id == rhs.cluster_id && lhs.node_id == rhs.node_id &&
           lhs.role == rhs.role && lhs.status == rhs.status &&
           lhs.epoch == rhs.epoch;
}
bool operator==(const AddLearnerToGroupCommand& lhs,
                const AddLearnerToGroupCommand& rhs) {
    return lhs.group_id == rhs.group_id && lhs.node_id == rhs.node_id &&
           lhs.config_version == rhs.config_version;
}
bool operator==(const MarkLearnerCaughtUpCommand& lhs,
                const MarkLearnerCaughtUpCommand& rhs) {
    return lhs.group_id == rhs.group_id && lhs.node_id == rhs.node_id &&
           lhs.config_version == rhs.config_version;
}
bool operator==(const BeginJointConfigCommand& lhs,
                const BeginJointConfigCommand& rhs) {
    return lhs.group_id == rhs.group_id &&
           lhs.new_voters == rhs.new_voters &&
           lhs.old_quorum == rhs.old_quorum &&
           lhs.new_quorum == rhs.new_quorum &&
           lhs.config_version == rhs.config_version;
}
bool operator==(const FinalizeJointConfigCommand& lhs,
                const FinalizeJointConfigCommand& rhs) {
    return lhs.group_id == rhs.group_id &&
           lhs.final_voters == rhs.final_voters &&
           lhs.old_quorum == rhs.old_quorum &&
           lhs.new_quorum == rhs.new_quorum &&
           lhs.config_version == rhs.config_version;
}
bool operator==(const ReadClusterIdentityCommand&,
                const ReadClusterIdentityCommand&) {
    return true;
}
bool operator==(const ReadRangeOwnershipCommand& lhs,
                const ReadRangeOwnershipCommand& rhs) {
    return lhs.table == rhs.table && lhs.active_only == rhs.active_only;
}
bool operator==(const JoinReplicaGroupCommand& lhs,
                const JoinReplicaGroupCommand& rhs) {
    return lhs.group_id == rhs.group_id && lhs.voters == rhs.voters;
}
bool operator==(const LeaveReplicaGroupCommand& lhs,
                const LeaveReplicaGroupCommand& rhs) {
    return lhs.group_id == rhs.group_id;
}
bool operator==(const MoveRangeCommand& lhs,
                const MoveRangeCommand& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.target_replica_group_id == rhs.target_replica_group_id;
}
bool operator==(const PrepareRangeTransferCommand& lhs,
                const PrepareRangeTransferCommand& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.target_replica_group_id == rhs.target_replica_group_id &&
           lhs.transfer_epoch == rhs.transfer_epoch;
}
bool operator==(const CatchUpRangeTransferCommand& lhs,
                const CatchUpRangeTransferCommand& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.transfer_epoch == rhs.transfer_epoch;
}
bool operator==(const CommitRangeTransferCommand& lhs,
                const CommitRangeTransferCommand& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.transfer_epoch == rhs.transfer_epoch;
}
bool operator==(const ExportRangeRowsCommand& lhs,
                const ExportRangeRowsCommand& rhs) {
    return lhs.table == rhs.table && lhs.range_id == rhs.range_id &&
           lhs.start_key == rhs.start_key && lhs.end_key == rhs.end_key;
}
bool operator==(const ImportRangeRowsCommand& lhs,
                const ImportRangeRowsCommand& rhs) {
    return lhs.table == rhs.table && lhs.range_id == rhs.range_id &&
           lhs.target_replica_group_id == rhs.target_replica_group_id &&
           lhs.start_key == rhs.start_key && lhs.end_key == rhs.end_key &&
           lhs.rows == rhs.rows;
}
bool operator==(const DeleteRangeRowsCommand& lhs,
                const DeleteRangeRowsCommand& rhs) {
    return lhs.table == rhs.table && lhs.range_id == rhs.range_id &&
           lhs.start_key == rhs.start_key && lhs.end_key == rhs.end_key;
}
bool operator==(const AbortRangeTransferCommand& lhs,
                const AbortRangeTransferCommand& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.transfer_epoch == rhs.transfer_epoch;
}
bool operator==(const QueryRangeConfigCommand& lhs,
                const QueryRangeConfigCommand& rhs) {
    return lhs.config_num == rhs.config_num;
}
bool operator==(const ReadSystemCatalogCommand& lhs,
                const ReadSystemCatalogCommand& rhs) {
    return lhs.table == rhs.table;
}

bool operator==(const Command& lhs, const Command& rhs) {
    return lhs.index() == rhs.index() &&
           std::visit(
               [](const auto& l, const auto& r) {
                   using L = std::decay_t<decltype(l)>;
                   using R = std::decay_t<decltype(r)>;
                   if constexpr (std::is_same_v<L, R>) {
                       return l == r;
                   }
                   return false;
               },
               lhs,
               rhs);
}

struct CreateTableOkResult {};
struct InsertOkResult {};
struct DeleteRowsResult { size_t count; };
struct UpdateRowsResult { size_t count; };
struct SelectAllResult {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};
struct RangeRowsResult {
    std::string table;
    std::string range_id;
    std::string start_key;
    std::string end_key;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};
struct RouteResult {
    std::string start_key;
    std::string end_key;
    std::vector<std::string> range_ids;
    std::vector<std::string> replica_group_ids;
    std::vector<int> descriptor_versions;
};
struct RangeConfigResult {
    int config_num = 0;
    std::vector<std::string> range_ids;
    std::vector<std::string> start_keys;
    std::vector<std::string> end_keys;
    std::vector<std::string> replica_group_ids;
};
struct TransactionOkResult {
    std::string range_id;
    size_t statement_count = 0;
};
struct QueryRowsResult { size_t count; };
struct CountRowsResult { size_t count; };
struct CheckpointOkResult {};
struct StatementOkResult {};
struct TableNotFoundResult {};
struct TableAlreadyExistsResult {};
struct SchemaMismatchResult {};
struct InvalidSchemaResult {};
struct ClusterRejectedResult {};
struct ConfigRejectedResult {};
struct RouteRejectedResult {};

using Result = std::variant<
    CreateTableOkResult,
    InsertOkResult,
    DeleteRowsResult,
    UpdateRowsResult,
    SelectAllResult,
    RangeRowsResult,
    RouteResult,
    RangeConfigResult,
    TransactionOkResult,
    QueryRowsResult,
    CountRowsResult,
    CheckpointOkResult,
    StatementOkResult,
    TableNotFoundResult,
    TableAlreadyExistsResult,
    SchemaMismatchResult,
    InvalidSchemaResult,
    ClusterRejectedResult,
    ConfigRejectedResult,
    RouteRejectedResult>;

bool operator==(const CreateTableOkResult&, const CreateTableOkResult&) { return true; }
bool operator==(const InsertOkResult&, const InsertOkResult&) { return true; }
bool operator==(const DeleteRowsResult& lhs, const DeleteRowsResult& rhs) { return lhs.count == rhs.count; }
bool operator==(const UpdateRowsResult& lhs, const UpdateRowsResult& rhs) { return lhs.count == rhs.count; }
bool operator==(const SelectAllResult& lhs, const SelectAllResult& rhs) { return lhs.columns == rhs.columns && lhs.rows == rhs.rows; }
bool operator==(const RangeRowsResult& lhs, const RangeRowsResult& rhs) {
    return lhs.table == rhs.table && lhs.range_id == rhs.range_id &&
           lhs.start_key == rhs.start_key && lhs.end_key == rhs.end_key &&
           lhs.columns == rhs.columns && lhs.rows == rhs.rows;
}
bool operator==(const RouteResult& lhs, const RouteResult& rhs) {
    return lhs.start_key == rhs.start_key && lhs.end_key == rhs.end_key &&
           lhs.range_ids == rhs.range_ids &&
           lhs.replica_group_ids == rhs.replica_group_ids &&
           lhs.descriptor_versions == rhs.descriptor_versions;
}
bool operator==(const RangeConfigResult& lhs,
                const RangeConfigResult& rhs) {
    return lhs.config_num == rhs.config_num &&
           lhs.range_ids == rhs.range_ids &&
           lhs.start_keys == rhs.start_keys &&
           lhs.end_keys == rhs.end_keys &&
           lhs.replica_group_ids == rhs.replica_group_ids;
}
bool operator==(const TransactionOkResult& lhs,
                const TransactionOkResult& rhs) {
    return lhs.range_id == rhs.range_id &&
           lhs.statement_count == rhs.statement_count;
}
bool operator==(const QueryRowsResult& lhs, const QueryRowsResult& rhs) { return lhs.count == rhs.count; }
bool operator==(const CountRowsResult& lhs, const CountRowsResult& rhs) { return lhs.count == rhs.count; }
bool operator==(const CheckpointOkResult&, const CheckpointOkResult&) { return true; }
bool operator==(const StatementOkResult&, const StatementOkResult&) { return true; }
bool operator==(const TableNotFoundResult&, const TableNotFoundResult&) { return true; }
bool operator==(const TableAlreadyExistsResult&, const TableAlreadyExistsResult&) { return true; }
bool operator==(const SchemaMismatchResult&, const SchemaMismatchResult&) { return true; }
bool operator==(const InvalidSchemaResult&, const InvalidSchemaResult&) { return true; }
bool operator==(const ClusterRejectedResult&, const ClusterRejectedResult&) { return true; }
bool operator==(const ConfigRejectedResult&, const ConfigRejectedResult&) { return true; }
bool operator==(const RouteRejectedResult&, const RouteRejectedResult&) { return true; }

bool operator==(const Result& lhs, const Result& rhs) {
    return lhs.index() == rhs.index() &&
           std::visit(
               [](const auto& l, const auto& r) {
                   using L = std::decay_t<decltype(l)>;
                   using R = std::decay_t<decltype(r)>;
                   if constexpr (std::is_same_v<L, R>) {
                       return l == r;
                   }
                   return false;
               },
               lhs,
               rhs);
}

std::string describeCommand(const Command& command) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, CreateTableCommand>) {
                out << "CreateTable(" << value.table << ", columns=[";
                for (size_t i = 0; i < value.columns.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.columns[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, InsertRowCommand>) {
                out << "InsertRow(" << value.table << ", values=[";
                for (size_t i = 0; i < value.values.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.values[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, DeleteRowsCommand>) {
                out << "DeleteRows(" << value.table << ", " << value.column << "=" << value.value << ")";
            } else if constexpr (std::is_same_v<T, UpdateRowsCommand>) {
                out << "UpdateRows(" << value.table << ", " << value.set_column << "=" << value.set_value
                    << " where " << value.where_column << "=" << value.where_value << ")";
            } else if constexpr (std::is_same_v<T, SelectAllCommand>) {
                out << "PROJECT * FROM " << value.table;
            } else if constexpr (std::is_same_v<T, SelectWhereCommand>) {
                out << "PROJECT * FROM " << value.table << " WHERE " << value.column << "=" << value.value;
            } else if constexpr (std::is_same_v<T, ExplainRouteCommand>) {
                out << "ExplainRoute(" << value.table;
                if (value.full_scan) {
                    out << ", full-scan";
                } else {
                    out << ", pk=" << value.primary_key;
                }
                out << ")";
            } else if constexpr (std::is_same_v<T, RoutedSQLCommand>) {
                out << "RoutedSQL(" << value.sql << ")";
            } else if constexpr (std::is_same_v<T, RoutedTransactionCommand>) {
                out << "RoutedTxn(statements=" << value.statements.size() << ")";
            } else if constexpr (std::is_same_v<T, QuerySQLCommand>) {
                out << "QuerySQL(" << value.sql << ")";
            } else if constexpr (std::is_same_v<T, CountRowsCommand>) {
                out << "CountRows(" << value.table << ")";
            } else if constexpr (std::is_same_v<T, CheckpointCommand>) {
                out << "Checkpoint";
            } else if constexpr (std::is_same_v<T, RegisterClusterNodeCommand>) {
                out << "RegisterClusterNode(" << value.node_id << ", "
                    << value.role << ", " << value.status
                    << ", epoch=" << value.epoch << ")";
            } else if constexpr (std::is_same_v<T, RegisterReplicaGroupCommand>) {
                out << "RegisterReplicaGroup(" << value.group_id
                    << ", config=" << value.config_version
                    << ", voters=[";
                for (size_t i = 0; i < value.voters.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.voters[i];
                }
                out << "], learners=[";
                for (size_t i = 0; i < value.learners.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.learners[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, RegisterRangeCommand>) {
                out << "RegisterRange(" << value.range_id << ", ["
                    << value.start_key << ", " << value.end_key
                    << ") -> " << value.replica_group_id
                    << ", version=" << value.descriptor_version
                    << ", status=" << value.status << ")";
            } else if constexpr (std::is_same_v<T, SplitRangeCommand>) {
                out << "SplitRange(" << value.source_range_id
                    << " at " << value.split_key
                    << " -> " << value.left_range_id
                    << ", " << value.right_range_id
                    << ", version=" << value.descriptor_version << ")";
            } else if constexpr (std::is_same_v<T, SplitTableCommand>) {
                out << "SplitTable(" << value.table
                    << ", source=" << value.source_range_id
                    << " -> " << value.left_range_id
                    << "@" << (value.left_replica_group_id.empty()
                                   ? "source-group"
                                   : value.left_replica_group_id)
                    << ", " << value.right_range_id
                    << "@" << (value.right_replica_group_id.empty()
                                   ? "source-group"
                                   : value.right_replica_group_id)
                    << ", version=" << value.descriptor_version << ")";
            } else if constexpr (std::is_same_v<T, BootstrapClusterCommand>) {
                out << "BootstrapCluster(" << value.cluster_id
                    << ", node=" << value.bootstrap_node_id
                    << ", epoch=" << value.epoch
                    << ", seeds=[";
                for (size_t i = 0; i < value.seed_nodes.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.seed_nodes[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, DiscoverClusterNodeCommand>) {
                out << "DiscoverClusterNode(" << value.cluster_id
                    << ", " << value.node_id
                    << ", " << value.role
                    << ", " << value.status
                    << ", epoch=" << value.epoch << ")";
            } else if constexpr (std::is_same_v<T, AddLearnerToGroupCommand>) {
                out << "AddLearnerToGroup(" << value.group_id
                    << ", " << value.node_id
                    << ", version=" << value.config_version << ")";
            } else if constexpr (std::is_same_v<T, MarkLearnerCaughtUpCommand>) {
                out << "MarkLearnerCaughtUp(" << value.group_id
                    << ", " << value.node_id
                    << ", version=" << value.config_version << ")";
            } else if constexpr (std::is_same_v<T, BeginJointConfigCommand>) {
                out << "BeginJointConfig(" << value.group_id
                    << ", version=" << value.config_version
                    << ", new_voters=[";
                for (size_t i = 0; i < value.new_voters.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.new_voters[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, FinalizeJointConfigCommand>) {
                out << "FinalizeJointConfig(" << value.group_id
                    << ", version=" << value.config_version
                    << ", final_voters=[";
                for (size_t i = 0; i < value.final_voters.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.final_voters[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, ReadClusterIdentityCommand>) {
                out << "ReadClusterIdentity";
            } else if constexpr (std::is_same_v<T, ReadRangeOwnershipCommand>) {
                out << "ReadRangeOwnership(" << value.table
                    << ", active_only="
                    << (value.active_only ? "true" : "false") << ")";
            } else if constexpr (std::is_same_v<T, JoinReplicaGroupCommand>) {
                out << "JoinReplicaGroup(" << value.group_id
                    << ", voters=[";
                for (size_t i = 0; i < value.voters.size(); ++i) {
                    if (i != 0) out << ", ";
                    out << value.voters[i];
                }
                out << "])";
            } else if constexpr (std::is_same_v<T, LeaveReplicaGroupCommand>) {
                out << "LeaveReplicaGroup(" << value.group_id << ")";
            } else if constexpr (std::is_same_v<T, MoveRangeCommand>) {
                out << "MoveRange(" << value.range_id
                    << " -> " << value.target_replica_group_id << ")";
            } else if constexpr (std::is_same_v<T, PrepareRangeTransferCommand>) {
                out << "PrepareRangeTransfer(" << value.range_id
                    << " -> " << value.target_replica_group_id
                    << ", epoch=" << value.transfer_epoch << ")";
            } else if constexpr (std::is_same_v<T, CatchUpRangeTransferCommand>) {
                out << "CatchUpRangeTransfer(" << value.range_id
                    << ", epoch=" << value.transfer_epoch << ")";
            } else if constexpr (std::is_same_v<T, CommitRangeTransferCommand>) {
                out << "CommitRangeTransfer(" << value.range_id
                    << ", epoch=" << value.transfer_epoch << ")";
            } else if constexpr (std::is_same_v<T, ExportRangeRowsCommand>) {
                out << "ExportRangeRows(" << value.table
                    << ", range=" << value.range_id << ")";
            } else if constexpr (std::is_same_v<T, ImportRangeRowsCommand>) {
                out << "ImportRangeRows(" << value.table
                    << ", range=" << value.range_id
                    << ", rows=" << value.rows.size() << ")";
            } else if constexpr (std::is_same_v<T, DeleteRangeRowsCommand>) {
                out << "DeleteRangeRows(" << value.table
                    << ", range=" << value.range_id << ")";
            } else if constexpr (std::is_same_v<T, AbortRangeTransferCommand>) {
                out << "AbortRangeTransfer(" << value.range_id
                    << ", epoch=" << value.transfer_epoch << ")";
            } else if constexpr (std::is_same_v<T, QueryRangeConfigCommand>) {
                out << "QueryRangeConfig(" << value.config_num << ")";
            } else if constexpr (std::is_same_v<T, ReadSystemCatalogCommand>) {
                out << "ReadSystemCatalog(" << value.table << ")";
            }
            return out.str();
        },
        command);
}

std::string describeResult(const Result& result) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, CreateTableOkResult>) out << "CreateTableOk";
            else if constexpr (std::is_same_v<T, InsertOkResult>) out << "InsertOk";
            else if constexpr (std::is_same_v<T, DeleteRowsResult>) out << "DeleteRows(" << value.count << ")";
            else if constexpr (std::is_same_v<T, UpdateRowsResult>) out << "UpdateRows(" << value.count << ")";
            else if constexpr (std::is_same_v<T, SelectAllResult>) out << "SelectAll(columns=" << value.columns.size() << ", rows=" << value.rows.size() << ")";
            else if constexpr (std::is_same_v<T, RangeRowsResult>) out << "RangeRows(table=" << value.table << ", range=" << value.range_id << ", rows=" << value.rows.size() << ")";
            else if constexpr (std::is_same_v<T, RouteResult>) out << "RouteResult(ranges=" << value.range_ids.size() << ")";
            else if constexpr (std::is_same_v<T, RangeConfigResult>) out << "RangeConfig(num=" << value.config_num << ", ranges=" << value.range_ids.size() << ")";
            else if constexpr (std::is_same_v<T, TransactionOkResult>) out << "TransactionOk(range=" << value.range_id << ", statements=" << value.statement_count << ")";
            else if constexpr (std::is_same_v<T, QueryRowsResult>) out << "QueryRows(" << value.count << ")";
            else if constexpr (std::is_same_v<T, CountRowsResult>) out << "CountRows(" << value.count << ")";
            else if constexpr (std::is_same_v<T, CheckpointOkResult>) out << "CheckpointOk";
            else if constexpr (std::is_same_v<T, StatementOkResult>) out << "StatementOk";
            else if constexpr (std::is_same_v<T, TableNotFoundResult>) out << "TableNotFound";
            else if constexpr (std::is_same_v<T, TableAlreadyExistsResult>) out << "TableAlreadyExists";
            else if constexpr (std::is_same_v<T, SchemaMismatchResult>) out << "SchemaMismatch";
            else if constexpr (std::is_same_v<T, InvalidSchemaResult>) out << "InvalidSchema";
            else if constexpr (std::is_same_v<T, ClusterRejectedResult>) out << "ClusterRejected";
            else if constexpr (std::is_same_v<T, ConfigRejectedResult>) out << "ConfigRejected";
            else if constexpr (std::is_same_v<T, RouteRejectedResult>) out << "RouteRejected";
            return out.str();
        },
        result);
}

class RangeTransferProtocol {
public:
    using Executor = std::function<Result(const Command&)>;

    RangeTransferProtocol(Executor catalog,
                          Executor source,
                          Executor target,
                          std::string table,
                          std::string range_id,
                          std::string target_replica_group_id,
                          std::string start_key,
                          std::string end_key,
                          int transfer_epoch)
        : catalog_(std::move(catalog)),
          source_(std::move(source)),
          target_(std::move(target)),
          table_(std::move(table)),
          range_id_(std::move(range_id)),
          target_replica_group_id_(std::move(target_replica_group_id)),
          start_key_(std::move(start_key)),
          end_key_(std::move(end_key)),
          transfer_epoch_(transfer_epoch) {}

    Result prepare() {
        Result prepared = catalog_(
            Command{PrepareRangeTransferCommand{
                range_id_, target_replica_group_id_, transfer_epoch_}});
        if (!std::holds_alternative<StatementOkResult>(prepared)) {
            return prepared;
        }
        return copySourceToTarget();
    }

    Result catchUp() {
        Result copied = copySourceToTarget();
        if (!std::holds_alternative<StatementOkResult>(copied)) {
            return copied;
        }
        return catalog_(
            Command{CatchUpRangeTransferCommand{range_id_, transfer_epoch_}});
    }

    Result commit() {
        Result committed = catalog_(
            Command{CommitRangeTransferCommand{range_id_, transfer_epoch_}});
        if (!std::holds_alternative<StatementOkResult>(committed)) {
            return committed;
        }
        Result deleted = source_(
            Command{DeleteRangeRowsCommand{
                table_, range_id_, start_key_, end_key_}});
        if (!std::holds_alternative<DeleteRowsResult>(deleted)) {
            return deleted;
        }
        return StatementOkResult{};
    }

    Result abort() {
        Result aborted = catalog_(
            Command{AbortRangeTransferCommand{range_id_, transfer_epoch_}});
        if (!std::holds_alternative<StatementOkResult>(aborted)) {
            return aborted;
        }
        Result deleted = target_(
            Command{DeleteRangeRowsCommand{
                table_, range_id_, start_key_, end_key_}});
        if (!std::holds_alternative<DeleteRowsResult>(deleted)) {
            return deleted;
        }
        return StatementOkResult{};
    }

private:
    Result copySourceToTarget() {
        Result exported = source_(
            Command{ExportRangeRowsCommand{
                table_, range_id_, start_key_, end_key_}});
        const auto* rows = std::get_if<RangeRowsResult>(&exported);
        if (rows == nullptr) return exported;
        Result imported = target_(
            Command{ImportRangeRowsCommand{
                table_,
                range_id_,
                target_replica_group_id_,
                start_key_,
                end_key_,
                rows->rows}});
        if (!std::holds_alternative<CountRowsResult>(imported)) {
            return imported;
        }
        return StatementOkResult{};
    }

    Executor catalog_;
    Executor source_;
    Executor target_;
    std::string table_;
    std::string range_id_;
    std::string target_replica_group_id_;
    std::string start_key_;
    std::string end_key_;
    int transfer_epoch_ = 1;
};

class ControlPlaneRuntime {
public:
    using Executor = RangeTransferProtocol::Executor;

    struct StepResult {
        bool advanced = false;
        std::string range_id;
        std::string status_before;
        std::string status_after;
        Result result = StatementOkResult{};
    };

    explicit ControlPlaneRuntime(Executor catalog)
        : catalog_(std::move(catalog)) {}

    void registerReplicaGroup(std::string group_id, Executor executor) {
        replica_groups_[std::move(group_id)] = std::move(executor);
    }

    StepResult tick() {
        std::vector<TransferRecord> transfers = activeTransfers();
        if (transfers.empty()) return StepResult{};
        std::sort(
            transfers.begin(),
            transfers.end(),
            [](const TransferRecord& lhs, const TransferRecord& rhs) {
                int lhs_priority = activeStatusPriority(lhs.status);
                int rhs_priority = activeStatusPriority(rhs.status);
                if (lhs_priority != rhs_priority) {
                    return lhs_priority < rhs_priority;
                }
                if (lhs.prepared_config_num != rhs.prepared_config_num) {
                    return lhs.prepared_config_num < rhs.prepared_config_num;
                }
                return lhs.range_id < rhs.range_id;
            });
        return advance(transfers.front());
    }

    size_t runUntilIdle(size_t max_steps = 1000) {
        size_t steps = 0;
        while (steps < max_steps) {
            StepResult step = tick();
            if (!step.advanced) break;
            ++steps;
        }
        return steps;
    }

private:
    struct TransferRecord {
        std::string range_id;
        std::string source_group_id;
        std::string target_group_id;
        int transfer_epoch = 0;
        int prepared_config_num = 0;
        std::string status;
    };

    struct RangeSpec {
        std::string table;
        std::string start_key;
        std::string end_key;
    };

    static int statusRank(const std::string& status) {
        if (status == "requested") return 0;
        if (status == "prepared") return 1;
        if (status == "caught_up") return 2;
        if (status == "committed") return 3;
        if (status == "aborted") return 4;
        return -1;
    }

    static int activeStatusPriority(const std::string& status) {
        int rank = statusRank(status);
        return rank >= 0 && rank <= 2 ? rank : 100;
    }

    static bool activeStatus(const std::string& status) {
        return status == "requested" ||
               status == "prepared" ||
               status == "caught_up";
    }

    static std::optional<size_t> columnIndex(const SelectAllResult& rows,
                                             const std::string& column) {
        for (size_t i = 0; i < rows.columns.size(); ++i) {
            if (rows.columns[i] == column) return i;
        }
        return std::nullopt;
    }

    static std::optional<std::string> valueAt(
        const SelectAllResult& rows,
        const std::vector<std::string>& row,
        const std::string& column) {
        auto index = columnIndex(rows, column);
        if (!index.has_value() || *index >= row.size()) {
            return std::nullopt;
        }
        return row[*index];
    }

    std::optional<SelectAllResult> readCatalogTable(
        const std::string& table) const {
        Result result = catalog_(Command{ReadSystemCatalogCommand{table}});
        if (const auto* rows = std::get_if<SelectAllResult>(&result)) {
            return *rows;
        }
        return std::nullopt;
    }

    std::vector<TransferRecord> activeTransfers() const {
        auto rows = readCatalogTable("__range_transfers");
        if (!rows.has_value()) return {};

        std::map<std::string, TransferRecord> latest;
        for (const auto& row : rows->rows) {
            auto range_id = valueAt(*rows, row, "range_id");
            auto source_group_id = valueAt(*rows, row, "source_group_id");
            auto target_group_id = valueAt(*rows, row, "target_group_id");
            auto epoch = valueAt(*rows, row, "transfer_epoch");
            auto config_num = valueAt(*rows, row, "prepared_config_num");
            auto status = valueAt(*rows, row, "status");
            if (!range_id.has_value() ||
                !source_group_id.has_value() ||
                !target_group_id.has_value() ||
                !epoch.has_value() ||
                !config_num.has_value() ||
                !status.has_value()) {
                continue;
            }

            TransferRecord record{
                *range_id,
                *source_group_id,
                *target_group_id,
                std::stoi(*epoch),
                std::stoi(*config_num),
                *status};
            auto it = latest.find(record.range_id);
            if (it == latest.end() ||
                record.transfer_epoch > it->second.transfer_epoch ||
                (record.transfer_epoch == it->second.transfer_epoch &&
                 statusRank(record.status) >= statusRank(it->second.status))) {
                latest[record.range_id] = std::move(record);
            }
        }

        std::vector<TransferRecord> transfers;
        for (const auto& [range_id, record] : latest) {
            (void)range_id;
            if (activeStatus(record.status)) transfers.push_back(record);
        }
        return transfers;
    }

    std::optional<RangeSpec> rangeSpecFor(
        const TransferRecord& transfer) const {
        auto rows = readCatalogTable("__range_ownership");
        if (!rows.has_value()) return std::nullopt;

        std::optional<RangeSpec> best;
        int best_version = -1;
        for (const auto& row : rows->rows) {
            auto range_id = valueAt(*rows, row, "range_id");
            auto table = valueAt(*rows, row, "table_name");
            auto index = valueAt(*rows, row, "index_name");
            auto start_key = valueAt(*rows, row, "start_key");
            auto end_key = valueAt(*rows, row, "end_key");
            auto version = valueAt(*rows, row, "owner_version");
            auto status = valueAt(*rows, row, "status");
            if (!range_id.has_value() ||
                *range_id != transfer.range_id ||
                !table.has_value() ||
                !index.has_value() ||
                !start_key.has_value() ||
                !end_key.has_value() ||
                !version.has_value() ||
                !status.has_value() ||
                *status != "active") {
                continue;
            }
            int owner_version = std::stoi(*version);
            RangeSpec candidate{
                *index == "primary" ? *table : "__index_entries",
                *start_key,
                *end_key};
            if (owner_version == transfer.prepared_config_num) {
                return candidate;
            }
            if (owner_version > best_version) {
                best_version = owner_version;
                best = std::move(candidate);
            }
        }
        return best;
    }

    StepResult advance(const TransferRecord& transfer) {
        StepResult step;
        step.range_id = transfer.range_id;
        step.status_before = transfer.status;
        step.result = ConfigRejectedResult{};

        auto spec = rangeSpecFor(transfer);
        auto source = replica_groups_.find(transfer.source_group_id);
        auto target = replica_groups_.find(transfer.target_group_id);
        if (!spec.has_value() ||
            source == replica_groups_.end() ||
            target == replica_groups_.end()) {
            return step;
        }

        RangeTransferProtocol protocol{
            catalog_,
            source->second,
            target->second,
            spec->table,
            transfer.range_id,
            transfer.target_group_id,
            spec->start_key,
            spec->end_key,
            transfer.transfer_epoch};

        if (transfer.status == "requested") {
            step.status_after = "prepared";
            step.result = protocol.prepare();
        } else if (transfer.status == "prepared") {
            step.status_after = "caught_up";
            step.result = protocol.catchUp();
        } else if (transfer.status == "caught_up") {
            step.status_after = "committed";
            step.result = protocol.commit();
        }
        step.advanced = std::holds_alternative<StatementOkResult>(step.result);
        return step;
    }

    Executor catalog_;
    std::map<std::string, Executor> replica_groups_;
};

std::string adapterTrim(const std::string& input) {
    if (input.empty()) {
        return "";
    }
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) ++start;
    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(start, end - start);
}

std::string adapterStripOptionalSemicolon(const std::string& input) {
    std::string trimmed = adapterTrim(input);
    if (!trimmed.empty() && trimmed.back() == ';') trimmed.pop_back();
    return adapterTrim(trimmed);
}

std::vector<std::string> adapterSplitCommaList(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, ',')) tokens.push_back(adapterTrim(token));
    return tokens;
}

std::vector<std::string> adapterSplitPipeLine(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, '|')) tokens.push_back(adapterTrim(token));
    return tokens;
}

std::string adapterLower(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return input;
}

FieldType adapterParseFieldTypeName(const std::string& input) {
    std::string type = adapterLower(adapterTrim(input));
    if (type == "int" || type == "integer") return INT;
    if (type == "float" || type == "real") return FLOAT;
    if (type == "string" || type == "text") return STRING;
    throw std::invalid_argument("Unknown field type: " + input);
}

ColumnSchema adapterParseColumnSpec(const std::string& spec) {
    size_t separator = spec.find(':');
    if (separator == std::string::npos) return ColumnSchema{adapterTrim(spec), STRING};
    return ColumnSchema{adapterTrim(spec.substr(0, separator)), adapterParseFieldTypeName(spec.substr(separator + 1))};
}

TableSchema adapterMakeTableSchema(const std::vector<std::string>& specs) {
    if (specs.empty()) throw std::invalid_argument("table schema must have columns");
    std::set<std::string> seen;
    TableSchema schema;
    for (const auto& spec : specs) {
        ColumnSchema column = adapterParseColumnSpec(spec);
        if (column.name.empty() || !seen.insert(column.name).second) {
            throw std::invalid_argument("invalid or duplicate column: " + column.name);
        }
        schema.columns.push_back(std::move(column));
    }
    return schema;
}

Command parseSQL(const std::string& sql) {
    std::string statement = adapterStripOptionalSemicolon(sql);
    if (std::regex_match(statement, std::regex("^\\s*CHECKPOINT\\s*$", std::regex_constants::icase))) {
        return CheckpointCommand{};
    }

    std::smatch matches;
    std::regex projectRegex(
        "^\\s*(PROJECT|SELECT)\\s+\\*\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)"
        "(?:\\s+WHERE\\s+\\{?([A-Za-z_][A-Za-z0-9_]*)\\}?\\s*=\\s*([^\\s]+))?\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, projectRegex)) {
        if (matches[3].matched) return SelectWhereCommand{matches[2], matches[3], matches[4]};
        return SelectAllCommand{matches[2]};
    }

    std::regex createRegex(
        "^\\s*CREATE\\s+TABLE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\((.*)\\)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, createRegex)) {
        return CreateTableCommand{matches[1], adapterSplitCommaList(matches[2])};
    }

    std::regex insertRegex(
        "^\\s*INSERT\\s+([A-Za-z_][A-Za-z0-9_]*)\\|(.*)$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, insertRegex)) {
        return InsertRowCommand{matches[1], adapterSplitPipeLine(matches[2])};
    }

    std::regex updateRegex(
        "^\\s*UPDATE\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+SET\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s+WHERE\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, updateRegex)) {
        return UpdateRowsCommand{matches[1], matches[2], matches[3], matches[4], matches[5]};
    }

    std::regex deleteRegex(
        "^\\s*DELETE\\s+FROM\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+WHERE\\s+"
        "([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*([^\\s]+)\\s*$",
        std::regex_constants::icase);
    if (std::regex_match(statement, matches, deleteRegex)) {
        return DeleteRowsCommand{matches[1], matches[2], matches[3]};
    }

    throw std::runtime_error("Unsupported SQL command: " + sql);
}

std::vector<Command> loadTupleFileCommands(std::istream& input, const std::string& source_name) {
    std::vector<Command> commands;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = adapterTrim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.find('|') == std::string::npos) {
            throw std::runtime_error("Malformed tuple row in " + source_name + " at line " + std::to_string(line_number));
        }
        commands.push_back(parseSQL("INSERT " + line));
    }
    return commands;
}

std::vector<Command> loadTupleFileCommands(const std::string& filename) {
    std::ifstream input(filename);
    if (!input) throw std::runtime_error("Unable to open tuple file: " + filename);
    return loadTupleFileCommands(input, filename);
}

std::string buzzdbCompanionFilename(const std::string& database_file, const std::string& extension) {
    std::filesystem::path path(database_file);
    std::filesystem::path parent = path.parent_path();
    std::string stem = path.stem().string();
    if (stem.empty()) stem = path.filename().string();
    return (parent / (stem + extension)).string();
}

std::string buzzdbImageFilename(const std::string& database_file) {
    std::filesystem::path path(database_file);
    std::filesystem::path parent = path.parent_path();
    std::string stem = path.stem().string();
    if (stem.empty()) stem = "buzzdb";
    return (parent / (stem + ".image.dat")).string();
}

std::string buzzdbImageMetadataFilename(const std::string& database_file) {
    std::filesystem::path path(database_file);
    std::filesystem::path parent = path.parent_path();
    std::string stem = path.stem().string();
    if (stem.empty()) stem = "buzzdb";
    return (parent / (stem + ".image.meta")).string();
}

void setBuzzDBFileBundle(const std::string& database_file) {
    database_filename = database_file;
    log_filename = buzzdbCompanionFilename(database_file, ".log");
    master_record_filename = buzzdbCompanionFilename(database_file, ".master");
    image_copy_filename = buzzdbImageFilename(database_file);
    image_copy_metadata_filename = buzzdbImageMetadataFilename(database_file);
}

class ScopedBuzzDBFileBundle {
public:
    explicit ScopedBuzzDBFileBundle(const std::string& database_file)
        : old_database_(database_filename), old_log_(log_filename),
          old_master_(master_record_filename), old_image_(image_copy_filename),
          old_image_meta_(image_copy_metadata_filename) {
        setBuzzDBFileBundle(database_file);
    }

    ~ScopedBuzzDBFileBundle() {
        database_filename = old_database_;
        log_filename = old_log_;
        master_record_filename = old_master_;
        image_copy_filename = old_image_;
        image_copy_metadata_filename = old_image_meta_;
    }

private:
    std::string old_database_;
    std::string old_log_;
    std::string old_master_;
    std::string old_image_;
    std::string old_image_meta_;
};

std::string makeScratchBuzzDBFile() {
    static size_t counter = 0;
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("buzzdb-core-" + std::to_string(::getpid()) + "-" + std::to_string(++counter));
    std::filesystem::create_directories(dir);
    return (dir / "buzzdb.dat").string();
}

std::vector<std::string> buzzdbBundleFiles(const std::string& database_file) {
    return {
        database_file,
        buzzdbCompanionFilename(database_file, ".log"),
        buzzdbCompanionFilename(database_file, ".master"),
        buzzdbImageFilename(database_file),
        buzzdbImageMetadataFilename(database_file)
    };
}

const std::vector<std::string>& systemCatalogTableNames() {
    static const std::vector<std::string> names{
        "__cluster_identity",
        "__cluster_nodes",
        "__replica_groups",
        "__tables",
        "__ranges",
        "__range_ownership",
        "__range_transfers",
        "__global_keys",
        "__schema_versions"
    };
    return names;
}

bool isSystemCatalogTable(const std::string& table) {
    const auto& names = systemCatalogTableNames();
    return std::find(names.begin(), names.end(), table) != names.end();
}

std::string catalogJoin(const std::vector<std::string>& values,
                        const std::string& separator = ",") {
    if (values.empty()) return "-";
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << separator;
        out << values[i];
    }
    return out.str();
}

std::vector<std::string> splitCatalogList(const std::string& value) {
    if (value.empty() || value == "-") return {};
    std::vector<std::string> out;
    std::string current;
    for (char ch : value) {
        if (ch == ',') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

std::vector<std::string> canonicalCatalogList(
    std::vector<std::string> values) {
    values.erase(
        std::remove_if(values.begin(), values.end(),
                       [](const std::string& value) {
                           return value.empty();
                       }),
        values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool catalogListContains(const std::vector<std::string>& values,
                         const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::vector<std::string> catalogListWithout(
    std::vector<std::string> values,
    const std::string& value) {
    values.erase(std::remove(values.begin(), values.end(), value),
                 values.end());
    return canonicalCatalogList(std::move(values));
}

std::vector<std::string> catalogListWith(std::vector<std::string> values,
                                         const std::string& value) {
    values.push_back(value);
    return canonicalCatalogList(std::move(values));
}

size_t catalogMajoritySize(const std::vector<std::string>& voters) {
    return voters.size() / 2 + 1;
}

bool catalogQuorumSatisfied(const std::vector<std::string>& voters,
                            const std::vector<std::string>& quorum) {
    auto unique_quorum = canonicalCatalogList(quorum);
    size_t count = 0;
    for (const auto& voter : unique_quorum) {
        if (catalogListContains(voters, voter)) ++count;
    }
    return !voters.empty() && count >= catalogMajoritySize(voters);
}

std::string keyEscape(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char ch : value) {
        if (ch == '/' || ch == '%' || std::isspace(ch)) {
            out << '%' << std::setw(2) << std::setfill('0')
                << static_cast<int>(ch) << std::setfill(' ');
        } else {
            out << static_cast<char>(ch);
        }
    }
    return out.str();
}

std::string tableRowPrefix(const std::string& table) {
    return "/table/" + keyEscape(table) + "/row/";
}

std::string tableRowPrefixEnd(const std::string& table) {
    return tableRowPrefix(table) + "~";
}

std::string tableRowKey(const std::string& table,
                        const std::string& primary_key) {
    return tableRowPrefix(table) + keyEscape(primary_key);
}

std::string defaultRangeIdForTable(const std::string& table) {
    return "range-" + keyEscape(table) + "-all";
}

struct RangeDescriptor {
    std::string range_id;
    std::string start_key;
    std::string end_key;
    std::string replica_group_id;
    int descriptor_version = 0;
    std::string status = "active";
};

bool keyInRange(const std::string& key, const RangeDescriptor& range) {
    return key >= range.start_key &&
           (range.end_key.empty() || key < range.end_key);
}

bool rangesOverlap(const std::string& start_key,
                   const std::string& end_key,
                   const RangeDescriptor& range) {
    bool left_before_right_end =
        range.end_key.empty() || start_key < range.end_key;
    bool right_after_left_start = end_key.empty() || range.start_key < end_key;
    return left_before_right_end && right_after_left_start;
}

std::vector<RangeDescriptor> sortRangeDescriptors(
    std::vector<RangeDescriptor> ranges) {
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const RangeDescriptor& lhs, const RangeDescriptor& rhs) {
            if (lhs.start_key != rhs.start_key) return lhs.start_key < rhs.start_key;
            if (lhs.end_key != rhs.end_key) return lhs.end_key < rhs.end_key;
            if (lhs.descriptor_version != rhs.descriptor_version) {
                return lhs.descriptor_version > rhs.descriptor_version;
            }
            return lhs.range_id < rhs.range_id;
        });
    return ranges;
}

std::vector<RangeDescriptor> latestRangeDescriptorsById(
    const std::vector<RangeDescriptor>& ranges) {
    std::map<std::string, RangeDescriptor> latest;
    for (const auto& range : ranges) {
        auto it = latest.find(range.range_id);
        if (it == latest.end() ||
            range.descriptor_version > it->second.descriptor_version) {
            latest[range.range_id] = range;
        }
    }

    std::vector<RangeDescriptor> current;
    current.reserve(latest.size());
    for (const auto& [range_id, range] : latest) {
        (void)range_id;
        current.push_back(range);
    }
    return sortRangeDescriptors(std::move(current));
}

std::vector<RangeDescriptor> latestActiveRangeDescriptors(
    const std::vector<RangeDescriptor>& ranges) {
    std::vector<RangeDescriptor> active;
    for (const auto& range : latestRangeDescriptorsById(ranges)) {
        if (range.status == "active") {
            active.push_back(range);
        }
    }
    return sortRangeDescriptors(std::move(active));
}

RouteResult routeResultFor(const std::string& start_key,
                           const std::string& end_key,
                           std::vector<RangeDescriptor> ranges) {
    ranges = sortRangeDescriptors(std::move(ranges));
    RouteResult result;
    result.start_key = start_key;
    result.end_key = end_key;
    for (const auto& range : ranges) {
        result.range_ids.push_back(range.range_id);
        result.replica_group_ids.push_back(range.replica_group_id);
        result.descriptor_versions.push_back(range.descriptor_version);
    }
    return result;
}

class BuzzDBCore {
public:
    BuzzDBCore()
        : database_file_(makeScratchBuzzDBFile()), owns_bundle_(true) {
        open();
    }

    explicit BuzzDBCore(std::string database_file)
        : database_file_(std::filesystem::absolute(database_file).string()),
          owns_bundle_(false) {
        std::filesystem::create_directories(std::filesystem::path(database_file_).parent_path());
        open();
    }

    BuzzDBCore(const BuzzDBCore& other)
        : database_file_(makeScratchBuzzDBFile()),
          owns_bundle_(true) {
        std::ostringstream sink;
        auto* old_buffer = std::cout.rdbuf(sink.rdbuf());
        try {
            const_cast<BuzzDBCore&>(other).flushForSnapshot();
            std::cout.rdbuf(old_buffer);
        } catch (...) {
            std::cout.rdbuf(old_buffer);
            throw;
        }
        copyBundle(other.database_file_, database_file_);
        open();
    }

    BuzzDBCore& operator=(const BuzzDBCore& other) {
        if (this == &other) return *this;
        cleanupOwnedBundle();
        database_file_ = makeScratchBuzzDBFile();
        owns_bundle_ = true;
        std::ostringstream sink;
        auto* old_buffer = std::cout.rdbuf(sink.rdbuf());
        try {
            const_cast<BuzzDBCore&>(other).flushForSnapshot();
            std::cout.rdbuf(old_buffer);
        } catch (...) {
            std::cout.rdbuf(old_buffer);
            throw;
        }
        copyBundle(other.database_file_, database_file_);
        open();
        return *this;
    }

    ~BuzzDBCore() { cleanupOwnedBundle(); }

    Result execute(const Command& command) {
        ScopedBuzzDBFileBundle scope(database_file_);
        try {
            return std::visit([&](const auto& value) -> Result { return executeOne(value); }, command);
        } catch (const std::out_of_range&) {
            return TableNotFoundResult{};
        } catch (const std::invalid_argument&) {
            return SchemaMismatchResult{};
        } catch (const std::runtime_error& error) {
            std::string message = error.what();
            if (message.find("No table found") != std::string::npos ||
                message.find("Unknown table") != std::string::npos) {
                return TableNotFoundResult{};
            }
            if (message.find("schema") != std::string::npos ||
                message.find("Wrong field count") != std::string::npos ||
                message.find("Unknown column") != std::string::npos ||
                message.find("Bad field value") != std::string::npos) {
                return SchemaMismatchResult{};
            }
            throw;
        }
    }

    std::string digest() const {
        ScopedBuzzDBFileBundle scope(database_file_);
        std::ostringstream out;
        out << "BuzzDB(tables=";
        auto names = const_cast<BuzzDBCore*>(this)->db_->userTableNames();
        out << names.size();
        for (const auto& name : names) {
            auto& metadata = const_cast<BuzzDBCore*>(this)->db_->catalog.getTable(name);
            out << ";" << name << "#rows=" << metadata.row_count;
        }
        out << ";pages=" << const_cast<BuzzDBCore*>(this)->db_->buffer_manager.getNumPages() << ")";
        return out.str();
    }

    bool isNewDatabase() const {
        ScopedBuzzDBFileBundle scope(database_file_);
        return db_->isNewDatabase();
    }

    size_t storagePageCount() const {
        ScopedBuzzDBFileBundle scope(database_file_);
        return db_->buffer_manager.getNumPages();
    }

    std::vector<std::string> tableNames() {
        ScopedBuzzDBFileBundle scope(database_file_);
        return db_->userTableNames();
    }

    std::string databaseFile() const { return database_file_; }
    std::string logFile() const { return buzzdbCompanionFilename(database_file_, ".log"); }
    std::string masterFile() const { return buzzdbCompanionFilename(database_file_, ".master"); }

    std::string subsystemSummary() const {
        ScopedBuzzDBFileBundle scope(database_file_);
        std::ostringstream out;
        out << "pages=" << db_->buffer_manager.getNumPages()
            << ",tables=" << const_cast<BuzzDBCore*>(this)->db_->userTableNames().size()
            << ",log_forces=" << db_->recovery_manager.getStableLogForces()
            << ",flushed_lsn=" << db_->recovery_manager.getFlushedLSN();
        return out.str();
    }

    void analyze(const std::string& table = "") {
        ScopedBuzzDBFileBundle scope(database_file_);
        db_->analyze(table, false);
    }

    void recover() {
        ScopedBuzzDBFileBundle scope(database_file_);
        db_->recovery_manager.recover();
    }

    void bootstrapJobDatabase(const std::string& data_file,
                              bool print_output = true) {
        if (!print_output) {
            std::ostringstream sink;
            auto* old_buffer = std::cout.rdbuf(sink.rdbuf());
            try {
                bootstrapJobDatabase(data_file, true);
                std::cout.rdbuf(old_buffer);
                return;
            } catch (...) {
                std::cout.rdbuf(old_buffer);
                throw;
            }
        }

        ScopedBuzzDBFileBundle scope(database_file_);
        bool seed_database = db_->isNewDatabase();
        createJobTables(*db_);
        bool should_load = seed_database;
        try {
            auto& title = db_->catalog.getTable("title");
            auto& movie_companies = db_->catalog.getTable("movie_companies");
            should_load = should_load ||
                          (title.row_count == 0 &&
                           movie_companies.row_count == 0);
        } catch (const std::exception&) {
            should_load = true;
        }
        if (should_load) {
            auto load_txn = db_->begin("LOAD");
            db_->loadDataFile(data_file, load_txn, false);
            db_->commit(load_txn);
        }
        db_->analyze("", false);
    }

private:
    static void copyBundle(const std::string& from_database_file,
                           const std::string& to_database_file) {
        std::filesystem::create_directories(std::filesystem::path(to_database_file).parent_path());
        auto from_files = buzzdbBundleFiles(from_database_file);
        auto to_files = buzzdbBundleFiles(to_database_file);
        for (size_t i = 0; i < from_files.size(); ++i) {
            std::error_code ec;
            if (std::filesystem::exists(from_files[i], ec)) {
                std::filesystem::copy_file(
                    from_files[i], to_files[i],
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) throw std::runtime_error("Unable to copy BuzzDB file bundle: " + ec.message());
            }
        }
    }

    void cleanupOwnedBundle() {
        if (!owns_bundle_ || database_file_.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(database_file_).parent_path(), ec);
    }

    void open() {
        ScopedBuzzDBFileBundle scope(database_file_);
        db_ = std::make_unique<BuzzDB>();
    }

    void flushForSnapshot() {
        ScopedBuzzDBFileBundle scope(database_file_);
        if (db_) db_->buffer_manager.flushAllPages("snapshot clone");
    }

    bool tableExists(const std::string& table) {
        auto names = db_->userTableNames();
        return std::find(names.begin(), names.end(), table) != names.end();
    }

    void createSystemCatalogTableIfMissing(
        const std::string& table,
        const std::vector<std::string>& columns) {
        if (tableExists(table)) return;
        auto schema = adapterMakeTableSchema(columns);
        db_->catalog.createTable(table, std::move(schema));
    }

    void ensureSystemCatalogTables() {
        createSystemCatalogTableIfMissing(
            "__cluster_identity",
            {"cluster_id:string", "epoch:int", "bootstrap_node_id:string",
             "seed_nodes:string"});
        createSystemCatalogTableIfMissing(
            "__cluster_nodes",
            {"node_id:string", "role:string", "status:string",
             "epoch:int"});
        createSystemCatalogTableIfMissing(
            "__replica_groups",
            {"group_id:string", "config_version:int", "voters:string",
             "learners:string", "caught_up_learners:string",
             "joint_old_voters:string", "joint_new_voters:string",
             "phase:string"});
        createSystemCatalogTableIfMissing(
            "__ranges",
            {"range_id:string", "start_key:string", "end_key:string",
             "replica_group_id:string", "descriptor_version:int",
             "status:string"});
        createSystemCatalogTableIfMissing(
            "__range_ownership",
            {"table_name:string", "index_name:string", "range_id:string",
             "start_key:string", "end_key:string",
             "replica_group_id:string", "owner_version:int",
             "status:string"});
        createSystemCatalogTableIfMissing(
            "__range_transfers",
            {"range_id:string", "source_group_id:string",
             "target_group_id:string", "transfer_epoch:int",
             "snapshot_key_count:int", "catchup_key_count:int",
             "prepared_config_num:int", "status:string"});
        createSystemCatalogTableIfMissing(
            "__global_keys",
            {"table_name:string", "primary_key:string", "row_key:string",
             "range_id:string", "replica_group_id:string",
             "descriptor_version:int", "status:string"});
        createSystemCatalogTableIfMissing(
            "__schema_versions",
            {"table_name:string", "schema_version:int"});
    }

    void insertSystemCatalogRows(const std::vector<InsertRowCommand>& rows) {
        if (rows.empty()) return;
        auto txn = db_->beginLoggedTxn("system-catalog");
        for (const auto& row : rows) {
            db_->executeStatement(insertStatement(row), txn, 0, false);
        }
        db_->commit(txn);
    }

    InsertRowCommand rangeOwnershipCatalogRow(
        const std::string& table,
        const std::string& range_id,
        const std::string& start_key,
        const std::string& end_key,
        const std::string& replica_group_id,
        int owner_version,
        const std::string& status) {
        return InsertRowCommand{
            "__range_ownership",
            {table,
             "primary",
             range_id,
             start_key,
             end_key,
             replica_group_id,
             std::to_string(owner_version),
             status}};
    }

    void recordUserTableCreated(const CreateTableCommand& command) {
        if (isSystemCatalogTable(command.table)) return;
        ensureSystemCatalogTables();
        std::string range_id = defaultRangeIdForTable(command.table);
        std::string start_key = tableRowPrefix(command.table);
        std::string end_key = tableRowPrefixEnd(command.table);
        insertSystemCatalogRows({
            InsertRowCommand{
                "__schema_versions",
                {command.table, "1"}},
            InsertRowCommand{
                "__ranges",
                {range_id,
                 start_key,
                 end_key,
                 "group-1",
                 "1",
                 "active"}},
            rangeOwnershipCatalogRow(
                command.table, range_id, start_key, end_key,
                "group-1", 1, "active")
        });
    }

    SelectAllResult selectSystemCatalogTable(const std::string& table) {
        Result result = executeOne(SelectAllCommand{table});
        if (const auto* rows = std::get_if<SelectAllResult>(&result)) {
            return *rows;
        }
        return SelectAllResult{};
    }

    std::optional<std::string> storedClusterId() {
        SelectAllResult rows =
            selectSystemCatalogTable("__cluster_identity");
        if (rows.rows.empty() || rows.rows.front().empty()) {
            return std::nullopt;
        }
        return rows.rows.front().front();
    }

    bool clusterNodeExists(const std::string& node_id) {
        SelectAllResult rows = selectSystemCatalogTable("__cluster_nodes");
        for (const auto& row : rows.rows) {
            if (!row.empty() && row.front() == node_id) return true;
        }
        return false;
    }

    struct ReplicaGroupConfigRecord {
        bool exists = false;
        std::string group_id;
        int config_version = 0;
        std::vector<std::string> voters;
        std::vector<std::string> learners;
        std::vector<std::string> caught_up_learners;
        std::vector<std::string> joint_old_voters;
        std::vector<std::string> joint_new_voters;
        std::string phase = "stable";
    };

    ReplicaGroupConfigRecord latestReplicaGroupConfig(
        const std::string& group_id) {
        SelectAllResult rows = selectSystemCatalogTable("__replica_groups");
        ReplicaGroupConfigRecord latest;
        for (const auto& row : rows.rows) {
            if (row.size() < 8 || row[0] != group_id) continue;
            int version = std::stoi(row[1]);
            if (!latest.exists || version > latest.config_version) {
                latest.exists = true;
                latest.group_id = row[0];
                latest.config_version = version;
                latest.voters = canonicalCatalogList(splitCatalogList(row[2]));
                latest.learners =
                    canonicalCatalogList(splitCatalogList(row[3]));
                latest.caught_up_learners =
                    canonicalCatalogList(splitCatalogList(row[4]));
                latest.joint_old_voters =
                    canonicalCatalogList(splitCatalogList(row[5]));
                latest.joint_new_voters =
                    canonicalCatalogList(splitCatalogList(row[6]));
                latest.phase = row[7];
            }
        }
        return latest;
    }

    void appendReplicaGroupConfig(const ReplicaGroupConfigRecord& config) {
        insertSystemCatalogRows({
            InsertRowCommand{
                "__replica_groups",
                {config.group_id,
                 std::to_string(config.config_version),
                 catalogJoin(canonicalCatalogList(config.voters)),
                 catalogJoin(canonicalCatalogList(config.learners)),
                 catalogJoin(canonicalCatalogList(
                     config.caught_up_learners)),
                 catalogJoin(canonicalCatalogList(
                     config.joint_old_voters)),
                 catalogJoin(canonicalCatalogList(
                     config.joint_new_voters)),
                 config.phase}}
        });
    }

    bool freshReplicaGroupVersion(const std::string& group_id,
                                  int config_version,
                                  ReplicaGroupConfigRecord* latest) {
        *latest = latestReplicaGroupConfig(group_id);
        return !latest->exists ||
               config_version > latest->config_version;
    }

    bool canEnterJointConfig(
        const ReplicaGroupConfigRecord& latest,
        const std::vector<std::string>& new_voters,
        const std::vector<std::string>& old_quorum,
        const std::vector<std::string>& new_quorum) {
        auto canonical_new_voters = canonicalCatalogList(new_voters);
        if (!latest.exists || latest.phase != "stable" ||
            canonical_new_voters.size() < catalogMajoritySize(latest.voters)) {
            return false;
        }
        for (const auto& voter : canonical_new_voters) {
            if (catalogListContains(latest.voters, voter)) continue;
            if (!catalogListContains(latest.caught_up_learners, voter)) {
                return false;
            }
        }
        return catalogQuorumSatisfied(latest.voters, old_quorum) &&
               catalogQuorumSatisfied(canonical_new_voters, new_quorum);
    }

    std::vector<RangeDescriptor> rangeDescriptors() {
        SelectAllResult rows = selectSystemCatalogTable("__ranges");
        std::vector<RangeDescriptor> ranges;
        for (const auto& row : rows.rows) {
            if (row.size() < 5) continue;
            ranges.push_back(RangeDescriptor{
                row[0],
                row[1],
                row[2],
                row[3],
                std::stoi(row[4]),
                row.size() >= 6 ? row[5] : "active"
            });
        }
        return ranges;
    }

    std::optional<RangeDescriptor> latestRangeDescriptorById(
        const std::string& range_id) {
        for (const auto& range : latestRangeDescriptorsById(
                 rangeDescriptors())) {
            if (range.range_id == range_id) return range;
        }
        return std::nullopt;
    }

    struct RangeOwnershipRecord {
        std::string table_name;
        std::string index_name;
        std::string range_id;
        std::string start_key;
        std::string end_key;
        std::string replica_group_id;
        int owner_version = 0;
        std::string status = "active";
    };

    std::vector<RangeOwnershipRecord> latestRangeOwnershipRecords(
        const std::string& table,
        bool active_only) {
        SelectAllResult rows = selectSystemCatalogTable("__range_ownership");
        std::map<std::string, RangeOwnershipRecord> latest;
        for (const auto& row : rows.rows) {
            if (row.size() < 8) continue;
            if (!table.empty() && row[0] != table) continue;
            RangeOwnershipRecord record{
                row[0],
                row[1],
                row[2],
                row[3],
                row[4],
                row[5],
                std::stoi(row[6]),
                row[7]};
            std::string separator(1, '\0');
            std::string key = record.table_name + separator +
                              record.index_name + separator +
                              record.range_id;
            auto it = latest.find(key);
            if (it == latest.end() ||
                record.owner_version >= it->second.owner_version) {
                latest[key] = record;
            }
        }

        std::vector<RangeOwnershipRecord> records;
        for (const auto& [key, record] : latest) {
            (void)key;
            if (!active_only || record.status == "active") {
                records.push_back(record);
            }
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const RangeOwnershipRecord& lhs,
               const RangeOwnershipRecord& rhs) {
                if (lhs.start_key != rhs.start_key) {
                    return lhs.start_key < rhs.start_key;
                }
                if (lhs.end_key != rhs.end_key) {
                    return lhs.end_key < rhs.end_key;
                }
                return lhs.range_id < rhs.range_id;
            });
        return records;
    }

    SelectAllResult rangeOwnershipView(const ReadRangeOwnershipCommand& command) {
        ensureSystemCatalogTables();
        std::vector<std::vector<std::string>> rows;
        for (const auto& record :
             latestRangeOwnershipRecords(command.table,
                                         command.active_only)) {
            rows.push_back({
                record.table_name,
                record.index_name,
                record.range_id,
                record.start_key,
                record.end_key,
                record.replica_group_id,
                std::to_string(record.owner_version),
                record.status});
        }
        return SelectAllResult{
            {"table_name", "index_name", "range_id", "start_key",
             "end_key", "replica_group_id", "owner_version", "status"},
            std::move(rows)};
    }

    int latestRangeConfigNumber() {
        SelectAllResult rows = selectSystemCatalogTable("__range_ownership");
        int latest = 0;
        for (const auto& row : rows.rows) {
            if (row.size() >= 7) {
                latest = std::max(latest, std::stoi(row[6]));
            }
        }
        return latest;
    }

    std::vector<RangeOwnershipRecord> rangeOwnershipRecordsAtConfig(
        int config_num) {
        SelectAllResult rows = selectSystemCatalogTable("__range_ownership");
        std::vector<RangeOwnershipRecord> records;
        for (const auto& row : rows.rows) {
            if (row.size() < 8 ||
                std::stoi(row[6]) != config_num ||
                row[7] != "active") {
                continue;
            }
            records.push_back(RangeOwnershipRecord{
                row[0],
                row[1],
                row[2],
                row[3],
                row[4],
                row[5],
                std::stoi(row[6]),
                row[7]});
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const RangeOwnershipRecord& lhs,
               const RangeOwnershipRecord& rhs) {
                if (lhs.start_key != rhs.start_key) {
                    return lhs.start_key < rhs.start_key;
                }
                return lhs.range_id < rhs.range_id;
            });
        return records;
    }

    RangeConfigResult rangeConfigResult(
        int config_num,
        const std::vector<RangeOwnershipRecord>& records) {
        RangeConfigResult result;
        result.config_num = config_num;
        for (const auto& record : records) {
            result.range_ids.push_back(record.range_id);
            result.start_keys.push_back(record.start_key);
            result.end_keys.push_back(record.end_key);
            result.replica_group_ids.push_back(record.replica_group_id);
        }
        return result;
    }

    std::vector<std::string> activeReplicaGroups(
        const std::vector<RangeOwnershipRecord>& records) {
        std::vector<std::string> groups;
        for (const auto& record : records) {
            groups.push_back(record.replica_group_id);
        }
        return canonicalCatalogList(std::move(groups));
    }

    std::map<std::string, std::string> balancedRangeAssignments(
        const std::vector<RangeOwnershipRecord>& records,
        std::vector<std::string> groups) {
        groups = canonicalCatalogList(std::move(groups));
        std::map<std::string, std::string> assignments;
        if (groups.empty()) return assignments;
        for (size_t i = 0; i < records.size(); ++i) {
            assignments[records[i].range_id] = groups[i % groups.size()];
        }
        return assignments;
    }

    void appendRangeConfigRows(
        int config_num,
        const std::vector<RangeOwnershipRecord>& records,
        const std::map<std::string, std::string>& assignments) {
        std::vector<InsertRowCommand> rows;
        for (const auto& record : records) {
            auto assignment = assignments.find(record.range_id);
            if (assignment == assignments.end()) {
                throw std::runtime_error("missing range assignment");
            }
            const std::string& group_id = assignment->second;
            rows.push_back(InsertRowCommand{
                "__ranges",
                {record.range_id,
                 record.start_key,
                 record.end_key,
                 group_id,
                 std::to_string(config_num),
                 "active"}});
            rows.push_back(
                rangeOwnershipCatalogRow(
                    record.table_name,
                    record.range_id,
                    record.start_key,
                    record.end_key,
                    group_id,
                    config_num,
                    "active"));
        }
        insertSystemCatalogRows(rows);
    }

    struct GlobalKeyRecord {
        std::string table_name;
        std::string primary_key;
        std::string row_key;
        std::string range_id;
        std::string replica_group_id;
        int descriptor_version = 0;
        std::string status = "active";
    };

    std::vector<GlobalKeyRecord> latestGlobalKeyRecords() {
        SelectAllResult rows = selectSystemCatalogTable("__global_keys");
        std::map<std::string, GlobalKeyRecord> latest;
        for (const auto& row : rows.rows) {
            if (row.size() < 6) continue;
            GlobalKeyRecord record{
                row[0],
                row[1],
                row[2],
                row[3],
                row[4],
                std::stoi(row[5]),
                row.size() >= 7 ? row[6] : "active"};
            std::string separator(1, '\0');
            std::string key = record.table_name + separator +
                              record.primary_key + separator +
                              record.row_key;
            auto it = latest.find(key);
            if (it == latest.end() ||
                record.descriptor_version >=
                    it->second.descriptor_version) {
                latest[key] = record;
            }
        }

        std::vector<GlobalKeyRecord> records;
        records.reserve(latest.size());
        for (const auto& [key, record] : latest) {
            (void)key;
            records.push_back(record);
        }
        std::sort(
            records.begin(),
            records.end(),
            [](const GlobalKeyRecord& lhs,
               const GlobalKeyRecord& rhs) {
                if (lhs.row_key != rhs.row_key) {
                    return lhs.row_key < rhs.row_key;
                }
                return lhs.primary_key < rhs.primary_key;
            });
        return records;
    }

    std::vector<GlobalKeyRecord> latestGlobalKeyRecordsForRangeId(
        const std::string& range_id) {
        std::vector<GlobalKeyRecord> records;
        for (const auto& record : latestGlobalKeyRecords()) {
            if (record.range_id == range_id &&
                record.status == "active") {
                records.push_back(record);
            }
        }
        return records;
    }

    std::vector<GlobalKeyRecord> latestGlobalKeyRecordsForRange(
        const std::string& table,
        const RangeDescriptor& range) {
        std::vector<GlobalKeyRecord> records;
        for (const auto& record : latestGlobalKeyRecords()) {
            if (record.table_name == table &&
                record.range_id == range.range_id &&
                record.status == "active" &&
                keyInRange(record.row_key, range)) {
                records.push_back(record);
            }
        }
        return records;
    }

    void appendGlobalKeyGroupRows(
        const std::string& range_id,
        const std::string& replica_group_id,
        int descriptor_version) {
        std::vector<InsertRowCommand> rows;
        for (const auto& record :
             latestGlobalKeyRecordsForRangeId(range_id)) {
            rows.push_back(
                InsertRowCommand{
                    "__global_keys",
                    {record.table_name,
                     record.primary_key,
                     record.row_key,
                     range_id,
                     replica_group_id,
                     std::to_string(descriptor_version),
                     "active"}});
        }
        insertSystemCatalogRows(rows);
    }

    struct RangeTransferRecord {
        std::string range_id;
        std::string source_group_id;
        std::string target_group_id;
        int transfer_epoch = 0;
        size_t snapshot_key_count = 0;
        size_t catchup_key_count = 0;
        int prepared_config_num = 0;
        std::string status = "prepared";
    };

    std::vector<RangeTransferRecord> rangeTransferRecords() {
        SelectAllResult rows = selectSystemCatalogTable("__range_transfers");
        std::vector<RangeTransferRecord> records;
        for (const auto& row : rows.rows) {
            if (row.size() < 8) continue;
            records.push_back(
                RangeTransferRecord{
                    row[0],
                    row[1],
                    row[2],
                    std::stoi(row[3]),
                    static_cast<size_t>(std::stoull(row[4])),
                    static_cast<size_t>(std::stoull(row[5])),
                    std::stoi(row[6]),
                    row[7]});
        }
        return records;
    }

    std::optional<RangeTransferRecord> latestRangeTransferRecord(
        const std::string& range_id) {
        std::optional<RangeTransferRecord> latest;
        for (const auto& record : rangeTransferRecords()) {
            if (record.range_id != range_id) continue;
            if (!latest.has_value() ||
                record.transfer_epoch >= latest->transfer_epoch) {
                latest = record;
            }
        }
        return latest;
    }

    void appendRangeTransferRecord(const RangeTransferRecord& record) {
        insertSystemCatalogRows({
            InsertRowCommand{
                "__range_transfers",
                {record.range_id,
                 record.source_group_id,
                 record.target_group_id,
                 std::to_string(record.transfer_epoch),
                 std::to_string(record.snapshot_key_count),
                 std::to_string(record.catchup_key_count),
                 std::to_string(record.prepared_config_num),
                 record.status}}
        });
    }

    bool rangeTransferInFlight(const RangeTransferRecord& record) {
        return record.status == "requested" ||
               record.status == "prepared" ||
               record.status == "caught_up";
    }

    int nextRangeTransferEpoch(const std::string& range_id) {
        auto previous = latestRangeTransferRecord(range_id);
        return previous.has_value() ? previous->transfer_epoch + 1 : 1;
    }

    std::optional<RangeOwnershipRecord> ownershipRecordForRange(
        const std::vector<RangeOwnershipRecord>& records,
        const std::string& range_id) {
        for (const auto& record : records) {
            if (record.range_id == range_id) return record;
        }
        return std::nullopt;
    }

    std::optional<RangeTransferRecord> buildRangeTransferRequest(
        const std::vector<RangeOwnershipRecord>& current,
        const std::string& range_id,
        const std::string& target_group_id,
        int config_num) {
        auto source = ownershipRecordForRange(current, range_id);
        if (!source.has_value() ||
            source->replica_group_id == target_group_id) {
            return std::nullopt;
        }
        auto target = latestReplicaGroupConfig(target_group_id);
        if (!target.exists || target.phase != "stable") {
            return std::nullopt;
        }
        auto previous = latestRangeTransferRecord(range_id);
        if (previous.has_value() && rangeTransferInFlight(*previous)) {
            return std::nullopt;
        }
        return RangeTransferRecord{
            range_id,
            source->replica_group_id,
            target_group_id,
            nextRangeTransferEpoch(range_id),
            0,
            0,
            config_num,
            "requested"};
    }

    bool appendRangeTransferRequest(
        const std::vector<RangeOwnershipRecord>& current,
        const std::string& range_id,
        const std::string& target_group_id,
        int config_num) {
        auto request = buildRangeTransferRequest(
            current, range_id, target_group_id, config_num);
        if (!request.has_value()) return false;
        appendRangeTransferRecord(*request);
        return true;
    }

    bool appendRangeTransferRequests(
        const std::vector<RangeOwnershipRecord>& records,
        const std::map<std::string, std::string>& assignments,
        int config_num) {
        std::vector<RangeTransferRecord> requests;
        for (const auto& record : records) {
            auto assignment = assignments.find(record.range_id);
            if (assignment == assignments.end()) {
                throw std::runtime_error("missing range assignment");
            }
            if (assignment->second == record.replica_group_id) {
                continue;
            }
            auto request = buildRangeTransferRequest(
                records, record.range_id, assignment->second, config_num);
            if (!request.has_value()) return false;
            requests.push_back(*request);
        }
        if (requests.empty()) return false;
        for (const auto& request : requests) {
            appendRangeTransferRecord(request);
        }
        return true;
    }

    std::vector<RangeDescriptor> matchingPointRanges(
        const std::string& key) {
        std::vector<RangeDescriptor> matches;
        for (const auto& range : latestActiveRangeDescriptors(
                 rangeDescriptors())) {
            if (keyInRange(key, range)) matches.push_back(range);
        }
        std::sort(
            matches.begin(),
            matches.end(),
            [](const RangeDescriptor& lhs, const RangeDescriptor& rhs) {
                if (lhs.descriptor_version != rhs.descriptor_version) {
                    return lhs.descriptor_version > rhs.descriptor_version;
                }
                size_t lhs_width = lhs.end_key.size() + lhs.start_key.size();
                size_t rhs_width = rhs.end_key.size() + rhs.start_key.size();
                if (lhs_width != rhs_width) return lhs_width > rhs_width;
                return lhs.range_id < rhs.range_id;
            });
        return matches;
    }

    std::vector<RangeDescriptor> matchingScanRanges(
        const std::string& start_key,
        const std::string& end_key) {
        std::vector<RangeDescriptor> matches;
        for (const auto& range : latestActiveRangeDescriptors(
                 rangeDescriptors())) {
            if (rangesOverlap(start_key, end_key, range)) {
                matches.push_back(range);
            }
        }
        return sortRangeDescriptors(std::move(matches));
    }

    RouteResult explainRoute(const ExplainRouteCommand& command) {
        std::string start_key = command.full_scan
                                    ? tableRowPrefix(command.table)
                                    : tableRowKey(command.table,
                                                  command.primary_key);
        std::string end_key = command.full_scan
                                  ? tableRowPrefixEnd(command.table)
                                  : start_key + "\x7F";
        std::vector<RangeDescriptor> matches =
            command.full_scan ? matchingScanRanges(start_key, end_key)
                              : matchingPointRanges(start_key);
        return routeResultFor(start_key, end_key, std::move(matches));
    }

    bool routesToExactlyOneRange(const ExplainRouteCommand& command) {
        RouteResult route = explainRoute(command);
        return route.range_ids.size() == 1 &&
               route.replica_group_ids.size() == 1 &&
               route.descriptor_versions.size() == 1;
    }

    std::optional<std::string> primaryKeyColumnName(
        const std::string& table) {
        if (!tableExists(table)) return std::nullopt;
        auto& metadata = db_->catalog.getTable(table);
        if (metadata.schema.columns.empty()) return std::nullopt;
        return metadata.schema.columns.front().name;
    }

    std::optional<std::string> primaryKeyValueFromInsert(
        const InsertRowCommand& command) {
        if (!tableExists(command.table) || command.values.empty()) {
            return std::nullopt;
        }
        auto& metadata = db_->catalog.getTable(command.table);
        if (metadata.schema.columns.empty()) return std::nullopt;
        return command.values.front();
    }

    bool isPrimaryKeyColumn(const std::string& table,
                            const std::string& column) {
        auto primary_key = primaryKeyColumnName(table);
        return primary_key.has_value() && *primary_key == column;
    }

    std::optional<std::string> singleRangeForParsedCommand(
        const Command& parsed) {
        RouteResult route;
        if (const auto* insert = std::get_if<InsertRowCommand>(&parsed)) {
            auto primary_key = primaryKeyValueFromInsert(*insert);
            if (!primary_key.has_value()) {
                return std::nullopt;
            }
            route = explainRoute(
                ExplainRouteCommand{insert->table,
                                    *primary_key,
                                    false});
        } else if (const auto* select =
                       std::get_if<SelectWhereCommand>(&parsed)) {
            if (!isPrimaryKeyColumn(select->table, select->column)) {
                return std::nullopt;
            }
            route = explainRoute(
                ExplainRouteCommand{select->table, select->value, false});
        } else if (const auto* update =
                       std::get_if<UpdateRowsCommand>(&parsed)) {
            if (!isPrimaryKeyColumn(update->table, update->where_column) ||
                isPrimaryKeyColumn(update->table, update->set_column)) {
                return std::nullopt;
            }
            route = explainRoute(
                ExplainRouteCommand{update->table,
                                    update->where_value,
                                    false});
        } else if (const auto* remove =
                       std::get_if<DeleteRowsCommand>(&parsed)) {
            if (!isPrimaryKeyColumn(remove->table, remove->column)) {
                return std::nullopt;
            }
            route = explainRoute(
                ExplainRouteCommand{remove->table, remove->value, false});
        } else if (const auto* scan = std::get_if<SelectAllCommand>(&parsed)) {
            if (!tableExists(scan->table)) return std::nullopt;
            route = explainRoute(ExplainRouteCommand{scan->table, "", true});
        } else {
            return std::nullopt;
        }

        if (route.range_ids.size() != 1) return std::nullopt;
        return route.range_ids.front();
    }

    Result executeRoutedParsedCommand(const Command& parsed) {
        if (const auto* insert = std::get_if<InsertRowCommand>(&parsed)) {
            if (!tableExists(insert->table)) return TableNotFoundResult{};
            auto primary_key = primaryKeyValueFromInsert(*insert);
            if (!primary_key.has_value()) return SchemaMismatchResult{};
            if (!routesToExactlyOneRange(
                    ExplainRouteCommand{
                        insert->table, *primary_key, false})) {
                return RouteRejectedResult{};
            }
            return executeOne(*insert);
        }

        if (const auto* select = std::get_if<SelectWhereCommand>(&parsed)) {
            if (!tableExists(select->table)) return TableNotFoundResult{};
            if (!isPrimaryKeyColumn(select->table, select->column)) {
                return RouteRejectedResult{};
            }
            if (!routesToExactlyOneRange(
                    ExplainRouteCommand{
                        select->table, select->value, false})) {
                return RouteRejectedResult{};
            }
            return executeOne(*select);
        }

        if (const auto* update = std::get_if<UpdateRowsCommand>(&parsed)) {
            if (!tableExists(update->table)) return TableNotFoundResult{};
            if (!isPrimaryKeyColumn(update->table, update->where_column) ||
                isPrimaryKeyColumn(update->table, update->set_column)) {
                return RouteRejectedResult{};
            }
            if (!routesToExactlyOneRange(
                    ExplainRouteCommand{
                        update->table, update->where_value, false})) {
                return RouteRejectedResult{};
            }
            return executeOne(*update);
        }

        if (const auto* remove = std::get_if<DeleteRowsCommand>(&parsed)) {
            if (!tableExists(remove->table)) return TableNotFoundResult{};
            if (!isPrimaryKeyColumn(remove->table, remove->column)) {
                return RouteRejectedResult{};
            }
            if (!routesToExactlyOneRange(
                    ExplainRouteCommand{
                        remove->table, remove->value, false})) {
                return RouteRejectedResult{};
            }
            return executeOne(*remove);
        }

        if (const auto* scan = std::get_if<SelectAllCommand>(&parsed)) {
            if (!tableExists(scan->table)) return TableNotFoundResult{};
            if (!routesToExactlyOneRange(
                    ExplainRouteCommand{scan->table, "", true})) {
                return RouteRejectedResult{};
            }
            return executeOne(*scan);
        }

        return RouteRejectedResult{};
    }

    Result executeInTxn(const Command& parsed, const TxnPtr& txn) {
        if (const auto* insert = std::get_if<InsertRowCommand>(&parsed)) {
            db_->executeStatement(insertStatement(*insert), txn, 0, false);
            return InsertOkResult{};
        }
        if (const auto* select = std::get_if<SelectWhereCommand>(&parsed)) {
            return selectWhere(select->table, select->column,
                               select->value, txn);
        }
        if (const auto* update = std::get_if<UpdateRowsCommand>(&parsed)) {
            size_t count = selectWhere(update->table,
                                       update->where_column,
                                       update->where_value,
                                       txn).rows.size();
            db_->executeStatement(updateStatement(*update), txn, 0, false);
            return UpdateRowsResult{count};
        }
        if (const auto* remove = std::get_if<DeleteRowsCommand>(&parsed)) {
            size_t count = selectWhere(remove->table,
                                       remove->column,
                                       remove->value,
                                       txn).rows.size();
            db_->executeStatement(deleteStatement(*remove), txn, 0, false);
            return DeleteRowsResult{count};
        }
        if (const auto* scan = std::get_if<SelectAllCommand>(&parsed)) {
            auto rows = db_->executeQuery(projectAllQuery(scan->table),
                                          txn,
                                          false);
            return queryResultToSelectAll(scan->table, rows);
        }
        return RouteRejectedResult{};
    }

    Result executeOne(const RoutedTransactionCommand& command) {
        if (command.statements.empty()) return RouteRejectedResult{};

        std::vector<Command> parsed;
        parsed.reserve(command.statements.size());
        std::optional<std::string> transaction_range;
        try {
            for (const auto& statement : command.statements) {
                parsed.push_back(parseSQL(statement));
                auto range = singleRangeForParsedCommand(parsed.back());
                if (!range.has_value()) return RouteRejectedResult{};
                if (!transaction_range.has_value()) {
                    transaction_range = *range;
                } else if (*transaction_range != *range) {
                    return RouteRejectedResult{};
                }
            }
        } catch (const std::exception&) {
            return RouteRejectedResult{};
        }

        auto txn = db_->beginLoggedTxn("routed-txn");
        std::vector<InsertRowCommand> catalog_rows;
        try {
            for (const auto& parsed_command : parsed) {
                Result result = executeInTxn(parsed_command, txn);
                if (std::holds_alternative<RouteRejectedResult>(result)) {
                    db_->abort(txn);
                    return RouteRejectedResult{};
                }
                if (const auto* insert =
                        std::get_if<InsertRowCommand>(&parsed_command)) {
                    auto catalog_row = globalKeyCatalogRow(*insert);
                    if (catalog_row.has_value()) {
                        catalog_rows.push_back(*catalog_row);
                    }
                }
                const auto* remove =
                    std::get_if<DeleteRowsCommand>(&parsed_command);
                const auto* deleted = std::get_if<DeleteRowsResult>(&result);
                if (remove != nullptr && deleted != nullptr &&
                    deleted->count > 0) {
                    auto catalog_row = globalKeyDeleteCatalogRow(*remove);
                    if (catalog_row.has_value()) {
                        catalog_rows.push_back(*catalog_row);
                    }
                }
            }
            executeSystemCatalogRowsInTxn(catalog_rows, txn);
            db_->commit(txn);
        } catch (const std::exception&) {
            db_->abort(txn);
            return SchemaMismatchResult{};
        }

        return TransactionOkResult{
            transaction_range.value_or("-"),
            command.statements.size()};
    }

    RangeDescriptor bestRangeForKey(const std::string& key) {
        auto matches = matchingPointRanges(key);
        if (matches.empty()) {
            return RangeDescriptor{"-", "-", "-", "-", 0};
        }
        return matches.front();
    }

    std::optional<InsertRowCommand> globalKeyCatalogRow(
        const InsertRowCommand& command) {
        if (isSystemCatalogTable(command.table)) {
            return std::nullopt;
        }
        auto primary_key = primaryKeyValueFromInsert(command);
        if (!primary_key.has_value()) return std::nullopt;
        ensureSystemCatalogTables();
        std::string row_key = tableRowKey(command.table, *primary_key);
        RangeDescriptor range = bestRangeForKey(row_key);
        return InsertRowCommand{
            "__global_keys",
            {command.table,
             *primary_key,
             row_key,
             range.range_id,
             range.replica_group_id,
             std::to_string(range.descriptor_version),
             "active"}};
    }

    std::optional<InsertRowCommand> globalKeyDeleteCatalogRow(
        const DeleteRowsCommand& command) {
        if (isSystemCatalogTable(command.table) ||
            !isPrimaryKeyColumn(command.table, command.column)) {
            return std::nullopt;
        }
        ensureSystemCatalogTables();
        std::string primary_key = command.value;
        std::string row_key = tableRowKey(command.table, primary_key);
        RangeDescriptor range = bestRangeForKey(row_key);
        return InsertRowCommand{
            "__global_keys",
            {command.table,
             primary_key,
             row_key,
             range.range_id,
             range.replica_group_id,
             std::to_string(range.descriptor_version),
             "deleted"}};
    }

    void executeSystemCatalogRowsInTxn(
        const std::vector<InsertRowCommand>& rows,
        const TxnPtr& txn) {
        for (const auto& row : rows) {
            db_->executeStatement(insertStatement(row), txn, 0, false);
        }
    }

    SelectAllResult tableCatalogView() {
        std::vector<std::vector<std::string>> rows;
        auto names = db_->userTableNames();
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            auto& metadata = db_->catalog.getTable(name);
            rows.push_back({
                std::to_string(metadata.table_id),
                metadata.name,
                std::to_string(metadata.row_count)
            });
        }
        return SelectAllResult{
            {"table_id", "table_name", "row_count"},
            std::move(rows)
        };
    }

    std::string insertStatement(const InsertRowCommand& command) const {
        std::ostringstream out;
        out << "INSERT " << command.table;
        for (const auto& value : command.values) out << "|" << value;
        return out.str();
    }

    std::string updateStatement(const UpdateRowsCommand& command) const {
        return "UPDATE " + command.table + " SET " + command.set_column + "=" +
               command.set_value + " WHERE " + command.where_column + "=" +
               command.where_value;
    }

    std::string deleteStatement(const DeleteRowsCommand& command) const {
        return "DELETE FROM " + command.table + " WHERE " + command.column + "=" + command.value;
    }

    std::string projectAllQuery(const std::string& table) const {
        return "PROJECT * FROM " + table;
    }

    std::string projectWhereQuery(const std::string& table,
                                  const std::string& column,
                                  const std::string& value) const {
        return "PROJECT * FROM " + table + " WHERE {" + column + "}=" + value;
    }

    std::vector<std::string> columnNames(const std::string& table) {
        auto& metadata = db_->catalog.getTable(table);
        std::vector<std::string> columns;
        for (const auto& column : metadata.schema.columns) columns.push_back(column.name);
        return columns;
    }

    SelectAllResult queryResultToSelectAll(const std::string& table,
                                           const QueryTable& query_table) {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(query_table.size());
        for (const auto& row : query_table) {
            std::vector<std::string> values;
            values.reserve(row.fields.size());
            for (const auto& field : row.fields) values.push_back(fieldToString(*field));
            rows.push_back(std::move(values));
        }
        return SelectAllResult{columnNames(table), std::move(rows)};
    }

    SelectAllResult selectWhere(const std::string& table,
                                const std::string& column,
                                const std::string& value,
                                const TxnPtr& txn = nullptr) {
        auto rows = db_->executeQuery(projectWhereQuery(table, column, value), txn, false);
        return queryResultToSelectAll(table, rows);
    }

    std::optional<size_t> columnIndex(
        const std::vector<std::string>& columns,
        const std::string& column) const {
        auto it = std::find(columns.begin(), columns.end(), column);
        if (it == columns.end()) return std::nullopt;
        return static_cast<size_t>(std::distance(columns.begin(), it));
    }

    bool rowBelongsToRange(const std::string& table,
                           const std::vector<std::string>& columns,
                           const std::vector<std::string>& row,
                           const std::string& range_id,
                           const std::string& start_key,
                           const std::string& end_key) const {
        auto range_column = columnIndex(columns, "range_id");
        if (range_column.has_value()) {
            if (*range_column >= row.size() || row[*range_column] != range_id) {
                return false;
            }
            auto status_column = columnIndex(columns, "status");
            return !status_column.has_value() || *status_column >= row.size() ||
                   row[*status_column] == "active";
        }
        if (row.empty()) return false;
        std::string row_key = tableRowKey(table, row.front());
        return row_key >= start_key && (end_key.empty() || row_key < end_key);
    }

    Result executeOne(const ExportRangeRowsCommand& command) {
        if (!tableExists(command.table) || command.range_id.empty()) {
            return TableNotFoundResult{};
        }
        SelectAllResult all = queryResultToSelectAll(
            command.table, db_->executeQuery("PROJECT * FROM " + command.table,
                                             nullptr, false));
        RangeRowsResult result{
            command.table,
            command.range_id,
            command.start_key,
            command.end_key,
            all.columns,
            {}};
        for (const auto& row : all.rows) {
            if (rowBelongsToRange(command.table,
                                  all.columns,
                                  row,
                                  command.range_id,
                                  command.start_key,
                                  command.end_key)) {
                result.rows.push_back(row);
            }
        }
        return result;
    }

    Result executeOne(const DeleteRangeRowsCommand& command) {
        if (!tableExists(command.table) || command.range_id.empty()) {
            return TableNotFoundResult{};
        }
        SelectAllResult all = queryResultToSelectAll(
            command.table, db_->executeQuery("PROJECT * FROM " + command.table,
                                             nullptr, false));
        auto range_column = columnIndex(all.columns, "range_id");
        if (range_column.has_value()) {
            Result result =
                executeOne(DeleteRowsCommand{command.table,
                                             "range_id",
                                             command.range_id});
            if (const auto* count = std::get_if<DeleteRowsResult>(&result)) {
                return *count;
            }
            return result;
        }
        auto primary_column = all.columns.empty()
                                  ? std::optional<size_t>{}
                                  : std::optional<size_t>{0};
        if (!primary_column.has_value()) return SchemaMismatchResult{};
        size_t deleted = 0;
        for (const auto& row : all.rows) {
            if (!rowBelongsToRange(command.table,
                                   all.columns,
                                   row,
                                   command.range_id,
                                   command.start_key,
                                   command.end_key) ||
                *primary_column >= row.size()) {
                continue;
            }
            Result result = executeOne(
                DeleteRowsCommand{command.table,
                                  all.columns[*primary_column],
                                  row[*primary_column]});
            if (const auto* count = std::get_if<DeleteRowsResult>(&result)) {
                deleted += count->count;
            } else if (!std::holds_alternative<TableNotFoundResult>(result)) {
                return result;
            }
        }
        return DeleteRowsResult{deleted};
    }

    Result executeOne(const ImportRangeRowsCommand& command) {
        if (!tableExists(command.table) || command.range_id.empty()) {
            return TableNotFoundResult{};
        }
        Result cleared = executeOne(
            DeleteRangeRowsCommand{command.table,
                                   command.range_id,
                                   command.start_key,
                                   command.end_key});
        if (!std::holds_alternative<DeleteRowsResult>(cleared)) return cleared;

        std::vector<std::string> columns = columnNames(command.table);
        auto range_column = columnIndex(columns, "range_id");
        auto group_column = columnIndex(columns, "replica_group_id");
        size_t inserted = 0;
        for (auto row : command.rows) {
            if (range_column.has_value() && *range_column < row.size()) {
                row[*range_column] = command.range_id;
            }
            if (group_column.has_value() && *group_column < row.size()) {
                row[*group_column] = command.target_replica_group_id;
            }
            Result result = executeOne(InsertRowCommand{command.table, row});
            if (!std::holds_alternative<InsertOkResult>(result)) return result;
            ++inserted;
        }
        return CountRowsResult{inserted};
    }

    QueryRowsResult executeQueryRowCount(const std::string& sql) {
        auto components = parseQuery(sql);
        std::vector<PhysicalJoinKind> join_kinds(
            components.joins.size(),
            PhysicalJoinKind::HashJoin);
        auto rows = db_->executeQuery(sql, nullptr, false, join_kinds);
        return QueryRowsResult{rows.size()};
    }

    Result executeOne(const CreateTableCommand& command) {
        TableSchema schema;
        try {
            schema = adapterMakeTableSchema(command.columns);
        } catch (const std::exception&) {
            return InvalidSchemaResult{};
        }
        try {
            auto result = db_->catalog.createTable(command.table, std::move(schema));
            if (!result.created) return TableAlreadyExistsResult{};
            recordUserTableCreated(command);
            return CreateTableOkResult{};
        } catch (const std::exception&) {
            return InvalidSchemaResult{};
        }
    }

    Result executeOne(const InsertRowCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        try {
            auto catalog_row = globalKeyCatalogRow(command);
            auto txn = db_->beginLoggedTxn("sim-insert");
            db_->executeStatement(insertStatement(command), txn, 0, false);
            if (catalog_row.has_value()) {
                executeSystemCatalogRowsInTxn({*catalog_row}, txn);
            }
            db_->commit(txn);
            return InsertOkResult{};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const DeleteRowsCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        try {
            size_t count = selectWhere(command.table, command.column, command.value).rows.size();
            std::optional<InsertRowCommand> catalog_row;
            if (count > 0) {
                catalog_row = globalKeyDeleteCatalogRow(command);
            }
            auto txn = db_->beginLoggedTxn("sim-delete");
            db_->executeStatement(deleteStatement(command), txn, 0, false);
            if (catalog_row.has_value()) {
                executeSystemCatalogRowsInTxn({*catalog_row}, txn);
            }
            db_->commit(txn);
            return DeleteRowsResult{count};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const UpdateRowsCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        if (!isSystemCatalogTable(command.table) &&
            isPrimaryKeyColumn(command.table, command.set_column)) {
            return RouteRejectedResult{};
        }
        try {
            size_t count = selectWhere(command.table, command.where_column, command.where_value).rows.size();
            auto txn = db_->beginLoggedTxn("sim-update");
            db_->executeStatement(updateStatement(command), txn, 0, false);
            db_->commit(txn);
            return UpdateRowsResult{count};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const SelectAllCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        try {
            auto rows = db_->executeQuery(projectAllQuery(command.table), nullptr, false);
            return queryResultToSelectAll(command.table, rows);
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const SelectWhereCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        try {
            return selectWhere(command.table, command.column, command.value);
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const ExplainRouteCommand& command) {
        return explainRoute(command);
    }

    Result executeOne(const RoutedSQLCommand& command) {
        try {
            return executeRoutedParsedCommand(parseSQL(command.sql));
        } catch (const std::exception&) {
            return RouteRejectedResult{};
        }
    }

    Result executeOne(const QuerySQLCommand& command) {
        try {
            return executeQueryRowCount(command.sql);
        } catch (const std::out_of_range&) {
            return TableNotFoundResult{};
        } catch (const std::exception&) {
            return SchemaMismatchResult{};
        }
    }

    Result executeOne(const CountRowsCommand& command) {
        if (!tableExists(command.table)) return TableNotFoundResult{};
        auto& metadata = db_->catalog.getTable(command.table);
        return CountRowsResult{metadata.row_count};
    }

    Result executeOne(const CheckpointCommand&) {
        db_->recovery_manager.checkpoint();
        return CheckpointOkResult{};
    }

    Result executeOne(const RegisterClusterNodeCommand& command) {
        ensureSystemCatalogTables();
        insertSystemCatalogRows({
            InsertRowCommand{
                "__cluster_nodes",
                {command.node_id, command.role, command.status,
                 std::to_string(command.epoch)}}
        });
        return StatementOkResult{};
    }

    Result executeOne(const RegisterReplicaGroupCommand& command) {
        ensureSystemCatalogTables();
        if (!storedClusterId().has_value()) return ConfigRejectedResult{};
        ReplicaGroupConfigRecord latest;
        if (!freshReplicaGroupVersion(command.group_id,
                                      command.config_version,
                                      &latest) ||
            latest.exists) {
            return ConfigRejectedResult{};
        }
        auto voters = canonicalCatalogList(command.voters);
        if (voters.empty()) return ConfigRejectedResult{};
        ReplicaGroupConfigRecord config;
        config.exists = true;
        config.group_id = command.group_id;
        config.config_version = command.config_version;
        config.voters = voters;
        config.learners = canonicalCatalogList(command.learners);
        config.phase = "stable";
        appendReplicaGroupConfig(config);
        return StatementOkResult{};
    }

    Result executeOne(const AddLearnerToGroupCommand& command) {
        ensureSystemCatalogTables();
        ReplicaGroupConfigRecord latest;
        if (!freshReplicaGroupVersion(command.group_id,
                                      command.config_version,
                                      &latest) ||
            !latest.exists || latest.phase != "stable" ||
            !clusterNodeExists(command.node_id) ||
            catalogListContains(latest.voters, command.node_id) ||
            catalogListContains(latest.learners, command.node_id)) {
            return ConfigRejectedResult{};
        }
        latest.config_version = command.config_version;
        latest.learners = catalogListWith(latest.learners, command.node_id);
        appendReplicaGroupConfig(latest);
        return StatementOkResult{};
    }

    Result executeOne(const MarkLearnerCaughtUpCommand& command) {
        ensureSystemCatalogTables();
        ReplicaGroupConfigRecord latest;
        if (!freshReplicaGroupVersion(command.group_id,
                                      command.config_version,
                                      &latest) ||
            !latest.exists || latest.phase != "stable" ||
            !catalogListContains(latest.learners, command.node_id)) {
            return ConfigRejectedResult{};
        }
        latest.config_version = command.config_version;
        latest.caught_up_learners =
            catalogListWith(latest.caught_up_learners, command.node_id);
        appendReplicaGroupConfig(latest);
        return StatementOkResult{};
    }

    Result executeOne(const BeginJointConfigCommand& command) {
        ensureSystemCatalogTables();
        ReplicaGroupConfigRecord latest;
        if (!freshReplicaGroupVersion(command.group_id,
                                      command.config_version,
                                      &latest) ||
            !canEnterJointConfig(latest, command.new_voters,
                                 command.old_quorum,
                                 command.new_quorum)) {
            return ConfigRejectedResult{};
        }
        auto new_voters = canonicalCatalogList(command.new_voters);
        ReplicaGroupConfigRecord config = latest;
        config.config_version = command.config_version;
        config.joint_old_voters = latest.voters;
        config.joint_new_voters = new_voters;
        config.phase = "joint";
        appendReplicaGroupConfig(config);
        return StatementOkResult{};
    }

    Result executeOne(const FinalizeJointConfigCommand& command) {
        ensureSystemCatalogTables();
        ReplicaGroupConfigRecord latest;
        if (!freshReplicaGroupVersion(command.group_id,
                                      command.config_version,
                                      &latest) ||
            !latest.exists || latest.phase != "joint") {
            return ConfigRejectedResult{};
        }
        auto final_voters = canonicalCatalogList(command.final_voters);
        if (final_voters != latest.joint_new_voters ||
            !catalogQuorumSatisfied(latest.joint_old_voters,
                                    command.old_quorum) ||
            !catalogQuorumSatisfied(latest.joint_new_voters,
                                    command.new_quorum)) {
            return ConfigRejectedResult{};
        }
        ReplicaGroupConfigRecord config;
        config.exists = true;
        config.group_id = command.group_id;
        config.config_version = command.config_version;
        config.voters = final_voters;
        config.learners = latest.learners;
        config.caught_up_learners = latest.caught_up_learners;
        for (const auto& voter : final_voters) {
            config.learners = catalogListWithout(config.learners, voter);
            config.caught_up_learners =
                catalogListWithout(config.caught_up_learners, voter);
        }
        config.phase = "stable";
        appendReplicaGroupConfig(config);
        return StatementOkResult{};
    }

    Result executeOne(const RegisterRangeCommand& command) {
        ensureSystemCatalogTables();
        if (command.range_id.empty() ||
            command.start_key.empty() ||
            command.replica_group_id.empty() ||
            command.descriptor_version <= 0 ||
            (!command.end_key.empty() &&
             command.end_key <= command.start_key) ||
            (command.status != "active" &&
             command.status != "superseded")) {
            return ConfigRejectedResult{};
        }

        auto group = latestReplicaGroupConfig(command.replica_group_id);
        if (!group.exists || group.phase != "stable") {
            return ConfigRejectedResult{};
        }

        auto previous = latestRangeDescriptorById(command.range_id);
        if (previous.has_value()) {
            if (command.descriptor_version <=
                previous->descriptor_version) {
                return ConfigRejectedResult{};
            }
            if (command.status == "active" &&
                previous->status == "active") {
                return ConfigRejectedResult{};
            }
        }

        if (command.status == "active") {
            for (const auto& range : latestActiveRangeDescriptors(
                     rangeDescriptors())) {
                if (range.range_id != command.range_id &&
                    rangesOverlap(command.start_key,
                                  command.end_key,
                                  range)) {
                    return ConfigRejectedResult{};
                }
            }
        }

        insertSystemCatalogRows({
            InsertRowCommand{
                "__ranges",
                {command.range_id, command.start_key, command.end_key,
                 command.replica_group_id,
                 std::to_string(command.descriptor_version),
                 command.status}}
        });
        return StatementOkResult{};
    }

    Result executeOne(const SplitRangeCommand& command) {
        ensureSystemCatalogTables();
        if (command.source_range_id.empty() ||
            command.left_range_id.empty() ||
            command.right_range_id.empty() ||
            command.left_range_id == command.right_range_id ||
            command.left_range_id == command.source_range_id ||
            command.right_range_id == command.source_range_id) {
            return ConfigRejectedResult{};
        }

        std::optional<RangeDescriptor> source;
        for (const auto& range : latestRangeDescriptorsById(
                 rangeDescriptors())) {
            if (range.range_id == command.source_range_id) {
                source = range;
                break;
            }
        }
        if (!source.has_value() || source->status != "active" ||
            command.descriptor_version <= source->descriptor_version ||
            command.split_key <= source->start_key ||
            (!source->end_key.empty() &&
             command.split_key >= source->end_key)) {
            return ConfigRejectedResult{};
        }

        for (const auto& range : latestRangeDescriptorsById(
                 rangeDescriptors())) {
            if ((range.range_id == command.left_range_id ||
                 range.range_id == command.right_range_id) &&
                range.status == "active") {
                return ConfigRejectedResult{};
            }
        }

        insertSystemCatalogRows({
            InsertRowCommand{
                "__ranges",
                {source->range_id,
                 source->start_key,
                 source->end_key,
                 source->replica_group_id,
                 std::to_string(command.descriptor_version),
                 "superseded"}},
            InsertRowCommand{
                "__ranges",
                {command.left_range_id,
                 source->start_key,
                 command.split_key,
                 source->replica_group_id,
                 std::to_string(command.descriptor_version),
                 "active"}},
            InsertRowCommand{
                "__ranges",
                {command.right_range_id,
                 command.split_key,
                 source->end_key,
                 source->replica_group_id,
                 std::to_string(command.descriptor_version),
                 "active"}}
        });
        return StatementOkResult{};
    }

    Result executeOne(const SplitTableCommand& command) {
        ensureSystemCatalogTables();
        if (!tableExists(command.table)) return TableNotFoundResult{};
        if (command.table.empty() ||
            command.source_range_id.empty() ||
            command.left_range_id.empty() ||
            command.right_range_id.empty() ||
            command.left_range_id == command.right_range_id ||
            command.left_range_id == command.source_range_id ||
            command.right_range_id == command.source_range_id) {
            return ConfigRejectedResult{};
        }

        auto source = latestRangeDescriptorById(command.source_range_id);
        if (!source.has_value() || source->status != "active" ||
            command.descriptor_version <= source->descriptor_version ||
            source->start_key < tableRowPrefix(command.table) ||
            source->end_key > tableRowPrefixEnd(command.table)) {
            return ConfigRejectedResult{};
        }

        for (const auto& range : latestRangeDescriptorsById(
                 rangeDescriptors())) {
            if ((range.range_id == command.left_range_id ||
                 range.range_id == command.right_range_id) &&
                range.status == "active") {
                return ConfigRejectedResult{};
            }
        }

        auto records =
            latestGlobalKeyRecordsForRange(command.table, *source);
        if (records.size() < 2) return ConfigRejectedResult{};

        std::string left_group =
            command.left_replica_group_id.empty()
                ? source->replica_group_id
                : command.left_replica_group_id;
        std::string right_group =
            command.right_replica_group_id.empty()
                ? source->replica_group_id
                : command.right_replica_group_id;
        if (!command.left_replica_group_id.empty()) {
            auto group = latestReplicaGroupConfig(left_group);
            if (!group.exists || group.phase != "stable") {
                return ConfigRejectedResult{};
            }
        }
        if (!command.right_replica_group_id.empty()) {
            auto group = latestReplicaGroupConfig(right_group);
            if (!group.exists || group.phase != "stable") {
                return ConfigRejectedResult{};
            }
        }

        size_t split_index = records.size() / 2;
        std::string split_key = records[split_index].row_key;
        if (split_key <= source->start_key ||
            (!source->end_key.empty() && split_key >= source->end_key)) {
            return ConfigRejectedResult{};
        }

        std::vector<InsertRowCommand> catalog_rows{
            InsertRowCommand{
                "__ranges",
                {source->range_id,
                 source->start_key,
                 source->end_key,
                 source->replica_group_id,
                 std::to_string(command.descriptor_version),
                 "superseded"}},
            InsertRowCommand{
                "__ranges",
                {command.left_range_id,
                 source->start_key,
                 split_key,
                 left_group,
                 std::to_string(command.descriptor_version),
                 "active"}},
            InsertRowCommand{
                "__ranges",
                {command.right_range_id,
                 split_key,
                 source->end_key,
                 right_group,
                 std::to_string(command.descriptor_version),
                 "active"}}
        };
        catalog_rows.push_back(
            rangeOwnershipCatalogRow(
                command.table,
                source->range_id,
                source->start_key,
                source->end_key,
                source->replica_group_id,
                command.descriptor_version,
                "superseded"));
        catalog_rows.push_back(
            rangeOwnershipCatalogRow(
                command.table,
                command.left_range_id,
                source->start_key,
                split_key,
                left_group,
                command.descriptor_version,
                "active"));
        catalog_rows.push_back(
            rangeOwnershipCatalogRow(
                command.table,
                command.right_range_id,
                split_key,
                source->end_key,
                right_group,
                command.descriptor_version,
                "active"));

        for (const auto& record : records) {
            bool goes_left = record.row_key < split_key;
            std::string range_id = goes_left ? command.left_range_id
                                             : command.right_range_id;
            std::string replica_group_id = goes_left ? left_group
                                                     : right_group;
            catalog_rows.push_back(
                InsertRowCommand{
                    "__global_keys",
                    {record.table_name,
                     record.primary_key,
                     record.row_key,
                     range_id,
                     replica_group_id,
                     std::to_string(command.descriptor_version),
                     "active"}});
        }

        insertSystemCatalogRows(catalog_rows);
        return StatementOkResult{};
    }

    Result executeOne(const BootstrapClusterCommand& command) {
        ensureSystemCatalogTables();
        auto existing_cluster = storedClusterId();
        if (existing_cluster.has_value()) {
            if (*existing_cluster == command.cluster_id) {
                return StatementOkResult{};
            }
            return ClusterRejectedResult{};
        }

        std::vector<InsertRowCommand> rows{
            InsertRowCommand{
                "__cluster_identity",
                {command.cluster_id, std::to_string(command.epoch),
                 command.bootstrap_node_id,
                 catalogJoin(command.seed_nodes)}}};
        if (!clusterNodeExists(command.bootstrap_node_id)) {
            rows.push_back(InsertRowCommand{
                "__cluster_nodes",
                {command.bootstrap_node_id, "voter", "active",
                 std::to_string(command.epoch)}});
        }
        insertSystemCatalogRows(rows);
        return StatementOkResult{};
    }

    Result executeOne(const DiscoverClusterNodeCommand& command) {
        ensureSystemCatalogTables();
        auto existing_cluster = storedClusterId();
        if (!existing_cluster.has_value() ||
            *existing_cluster != command.cluster_id) {
            return ClusterRejectedResult{};
        }
        if (clusterNodeExists(command.node_id)) {
            return StatementOkResult{};
        }
        std::string role = command.role.empty() ? "learner" : command.role;
        std::string status =
            command.status.empty() ? "discovered" : command.status;
        insertSystemCatalogRows({
            InsertRowCommand{
                "__cluster_nodes",
                {command.node_id, role, status,
                 std::to_string(command.epoch)}}
        });
        return StatementOkResult{};
    }

    Result executeOne(const ReadClusterIdentityCommand&) {
        return executeOne(ReadSystemCatalogCommand{"__cluster_identity"});
    }

    Result executeOne(const ReadRangeOwnershipCommand& command) {
        return rangeOwnershipView(command);
    }

    Result executeOne(const JoinReplicaGroupCommand& command) {
        ensureSystemCatalogTables();
        int latest_config = latestRangeConfigNumber();
        if (!storedClusterId().has_value() ||
            latest_config == 0 ||
            command.group_id.empty()) {
            return ConfigRejectedResult{};
        }
        auto voters = canonicalCatalogList(command.voters);
        if (voters.empty()) return ConfigRejectedResult{};

        std::vector<RangeOwnershipRecord> current =
            rangeOwnershipRecordsAtConfig(latest_config);
        if (current.empty()) return ConfigRejectedResult{};
        auto groups = activeReplicaGroups(current);
        if (catalogListContains(groups, command.group_id)) {
            return ConfigRejectedResult{};
        }

        ReplicaGroupConfigRecord previous =
            latestReplicaGroupConfig(command.group_id);
        if (previous.exists && previous.phase != "stable") {
            return ConfigRejectedResult{};
        }
        ReplicaGroupConfigRecord config;
        config.exists = true;
        config.group_id = command.group_id;
        config.config_version =
            previous.exists ? previous.config_version + 1 : 1;
        config.voters = voters;
        config.phase = "stable";
        appendReplicaGroupConfig(config);

        groups.push_back(command.group_id);
        std::map<std::string, std::string> assignments =
            balancedRangeAssignments(current, std::move(groups));
        appendRangeTransferRequests(current, assignments, latest_config);
        return StatementOkResult{};
    }

    Result executeOne(const LeaveReplicaGroupCommand& command) {
        ensureSystemCatalogTables();
        int latest_config = latestRangeConfigNumber();
        if (latest_config == 0 || command.group_id.empty()) {
            return ConfigRejectedResult{};
        }
        std::vector<RangeOwnershipRecord> current =
            rangeOwnershipRecordsAtConfig(latest_config);
        auto groups = activeReplicaGroups(current);
        if (!catalogListContains(groups, command.group_id) ||
            groups.size() <= 1) {
            return ConfigRejectedResult{};
        }
        groups = catalogListWithout(std::move(groups), command.group_id);
        std::map<std::string, std::string> assignments =
            balancedRangeAssignments(current, std::move(groups));
        appendRangeTransferRequests(current, assignments, latest_config);
        return StatementOkResult{};
    }

    Result executeOne(const MoveRangeCommand& command) {
        ensureSystemCatalogTables();
        int latest_config = latestRangeConfigNumber();
        if (latest_config == 0 ||
            command.range_id.empty() ||
            command.target_replica_group_id.empty()) {
            return ConfigRejectedResult{};
        }
        std::vector<RangeOwnershipRecord> current =
            rangeOwnershipRecordsAtConfig(latest_config);

        if (!appendRangeTransferRequest(current,
                                        command.range_id,
                                        command.target_replica_group_id,
                                        latest_config)) {
            return ConfigRejectedResult{};
        }
        return StatementOkResult{};
    }

    Result executeOne(const PrepareRangeTransferCommand& command) {
        ensureSystemCatalogTables();
        int latest_config = latestRangeConfigNumber();
        if (latest_config == 0 ||
            command.range_id.empty() ||
            command.target_replica_group_id.empty() ||
            command.transfer_epoch <= 0) {
            return ConfigRejectedResult{};
        }

        auto previous = latestRangeTransferRecord(command.range_id);
        if (previous.has_value()) {
            if (command.transfer_epoch < previous->transfer_epoch) {
                return ConfigRejectedResult{};
            }
            if (command.transfer_epoch == previous->transfer_epoch) {
                if (previous->status != "requested" ||
                    previous->target_group_id !=
                        command.target_replica_group_id) {
                    return ConfigRejectedResult{};
                }
            } else if (rangeTransferInFlight(*previous)) {
                return ConfigRejectedResult{};
            }
        }

        std::vector<RangeOwnershipRecord> current =
            rangeOwnershipRecordsAtConfig(latest_config);
        std::optional<RangeOwnershipRecord> source =
            ownershipRecordForRange(current, command.range_id);
        if (!source.has_value() ||
            source->replica_group_id == command.target_replica_group_id) {
            return ConfigRejectedResult{};
        }
        if (previous.has_value() &&
            command.transfer_epoch == previous->transfer_epoch &&
            previous->source_group_id != source->replica_group_id) {
            return ConfigRejectedResult{};
        }

        auto target =
            latestReplicaGroupConfig(command.target_replica_group_id);
        if (!target.exists || target.phase != "stable") {
            return ConfigRejectedResult{};
        }

        appendRangeTransferRecord(
            RangeTransferRecord{
                command.range_id,
                source->replica_group_id,
                command.target_replica_group_id,
                command.transfer_epoch,
                latestGlobalKeyRecordsForRangeId(command.range_id).size(),
                0,
                latest_config,
                "prepared"});
        return StatementOkResult{};
    }

    Result executeOne(const CatchUpRangeTransferCommand& command) {
        ensureSystemCatalogTables();
        auto transfer = latestRangeTransferRecord(command.range_id);
        int latest_config = latestRangeConfigNumber();
        if (!transfer.has_value() ||
            transfer->transfer_epoch != command.transfer_epoch ||
            transfer->status != "prepared") {
            return ConfigRejectedResult{};
        }
        auto source = ownershipRecordForRange(
            rangeOwnershipRecordsAtConfig(latest_config), command.range_id);
        if (!source.has_value() ||
            source->replica_group_id != transfer->source_group_id) {
            return ConfigRejectedResult{};
        }
        size_t current_keys =
            latestGlobalKeyRecordsForRangeId(command.range_id).size();
        transfer->catchup_key_count =
            current_keys > transfer->snapshot_key_count
                ? current_keys - transfer->snapshot_key_count
                : 0;
        transfer->prepared_config_num = latest_config;
        transfer->status = "caught_up";
        appendRangeTransferRecord(*transfer);
        return StatementOkResult{};
    }

    Result executeOne(const CommitRangeTransferCommand& command) {
        ensureSystemCatalogTables();
        auto transfer = latestRangeTransferRecord(command.range_id);
        int latest_config = latestRangeConfigNumber();
        if (!transfer.has_value() ||
            transfer->transfer_epoch != command.transfer_epoch ||
            transfer->status != "caught_up") {
            return ConfigRejectedResult{};
        }

        std::vector<RangeOwnershipRecord> current =
            rangeOwnershipRecordsAtConfig(latest_config);
        std::map<std::string, std::string> assignments;
        bool found = false;
        for (const auto& record : current) {
            assignments[record.range_id] = record.replica_group_id;
            if (record.range_id == command.range_id &&
                record.replica_group_id == transfer->source_group_id) {
                found = true;
                assignments[record.range_id] = transfer->target_group_id;
            }
        }
        if (!found) return ConfigRejectedResult{};

        int committed_config = latest_config + 1;
        appendRangeConfigRows(committed_config, current, assignments);
        appendGlobalKeyGroupRows(command.range_id,
                                 transfer->target_group_id,
                                 committed_config);
        transfer->prepared_config_num = latest_config;
        transfer->status = "committed";
        appendRangeTransferRecord(*transfer);
        return StatementOkResult{};
    }

    Result executeOne(const AbortRangeTransferCommand& command) {
        ensureSystemCatalogTables();
        auto transfer = latestRangeTransferRecord(command.range_id);
        if (!transfer.has_value() ||
            transfer->transfer_epoch != command.transfer_epoch ||
            (transfer->status != "requested" &&
             transfer->status != "prepared" &&
             transfer->status != "caught_up")) {
            return ConfigRejectedResult{};
        }
        transfer->status = "aborted";
        appendRangeTransferRecord(*transfer);
        return StatementOkResult{};
    }

    Result executeOne(const QueryRangeConfigCommand& command) {
        ensureSystemCatalogTables();
        int latest_config = latestRangeConfigNumber();
        if (latest_config == 0) return ConfigRejectedResult{};
        int requested = command.config_num;
        int target = requested < 0 || requested > latest_config
                         ? latest_config
                         : requested;
        std::vector<RangeOwnershipRecord> records =
            rangeOwnershipRecordsAtConfig(target);
        if (records.empty()) return ConfigRejectedResult{};
        return rangeConfigResult(target, records);
    }

    Result executeOne(const ReadSystemCatalogCommand& command) {
        if (!isSystemCatalogTable(command.table)) return TableNotFoundResult{};
        ensureSystemCatalogTables();
        if (command.table == "__tables") {
            return tableCatalogView();
        }
        return executeOne(SelectAllCommand{command.table});
    }

    std::string database_file_;
    bool owns_bundle_ = false;
    std::unique_ptr<BuzzDB> db_;
};

template <typename Fn>
auto runWithSuppressedStdout(Fn fn) -> decltype(fn()) {
    std::ostringstream sink;
    auto* old_buffer = std::cout.rdbuf(sink.rdbuf());
    try {
        auto result = fn();
        std::cout.rdbuf(old_buffer);
        return result;
    } catch (...) {
        std::cout.rdbuf(old_buffer);
        throw;
    }
}

struct ClientRequest {
    int client_id = 0;
    int request_id = 0;
    Command command;
};

struct ClientReply {
    int client_id = 0;
    int request_id = 0;
    Result result;
};

bool addressEmpty(const Address& address) {
    return address.str().empty();
}

struct ClientForward {
    Address client;
    ClientRequest request{};
};

struct RequestVote {
    int term = 0;
    Address candidate;
    int last_log_index = 0;
    int last_log_term = 0;
};

struct VoteReply {
    int term = 0;
    Address voter;
    bool granted = false;
};

struct ConsensusLogEntryMessage {
    int index = 0;
    int term = 0;
    int client_id = 0;
    int request_id = 0;
    Command command = CreateTableCommand{"", {""}};
};

struct AppendEntries {
    int term = 0;
    Address leader;
    int prev_log_index = 0;
    int prev_log_term = 0;
    std::vector<ConsensusLogEntryMessage> entries;
    int leader_commit = 0;
    int compacted_through = 0;
};

struct AppendReply {
    int term = 0;
    Address replica;
    int match_index = 0;
    int applied_index = 0;
    bool success = false;
};

struct InstallSnapshot {
    int term = 0;
    Address leader;
    int last_included_index = 0;
    int last_included_term = 0;
    int leader_commit = 0;
    BuzzDBCore db;
    std::map<std::string, Result> session_results;
    std::map<std::string, int> execution_count;
};

struct SnapshotReply {
    int term = 0;
    Address replica;
    int last_included_index = 0;
    bool success = false;
};

struct CompactLog {
    int through_index = 0;
};

using Message = std::variant<
    ClientRequest,
    ClientReply,
    ClientForward,
    RequestVote,
    VoteReply,
    AppendEntries,
    AppendReply,
    InstallSnapshot,
    SnapshotReply,
    CompactLog>;

struct ElectionTimer {
    int generation = 0;
};

struct AppendRetryTimer {
    int generation = 0;
};

using Timer = std::variant<ElectionTimer, AppendRetryTimer>;

std::string requestKey(int client_id, int request_id) {
    return std::to_string(client_id) + ":" + std::to_string(request_id);
}

bool finalizesConfigWithoutReplica(const Command& command,
                                   const Address& replica) {
    const auto* finalize =
        std::get_if<FinalizeJointConfigCommand>(&command);
    if (finalize == nullptr) return false;
    auto voters = canonicalCatalogList(finalize->final_voters);
    return !catalogListContains(voters, replica.rootAddress().str());
}

std::string describeMessage(const Message& message) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ClientRequest>) {
                out << "ClientRequest(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeCommand(value.command) << ")";
            } else if constexpr (std::is_same_v<T, ClientReply>) {
                out << "ClientReply(client=" << value.client_id
                    << ", req=" << value.request_id
                    << ", " << describeResult(value.result) << ")";
            } else if constexpr (std::is_same_v<T, ClientForward>) {
                out << "ClientForward(client_addr=" << value.client
                    << ", client=" << value.request.client_id
                    << ", req=" << value.request.request_id
                    << ", " << describeCommand(value.request.command) << ")";
            } else if constexpr (std::is_same_v<T, RequestVote>) {
                out << "RequestVote(term=" << value.term
                    << ", candidate=" << value.candidate
                    << ", last=(" << value.last_log_index
                    << "," << value.last_log_term << "))";
            } else if constexpr (std::is_same_v<T, VoteReply>) {
                out << "VoteReply(term=" << value.term
                    << ", voter=" << value.voter
                    << ", granted=" << (value.granted ? "true" : "false")
                    << ")";
            } else if constexpr (std::is_same_v<T, AppendEntries>) {
                out << "AppendEntries(term=" << value.term
                    << ", leader=" << value.leader
                    << ", prev=(" << value.prev_log_index
                    << "," << value.prev_log_term << ")"
                    << ", entries=" << value.entries.size()
                    << ", commit=" << value.leader_commit
                    << ", compacted=" << value.compacted_through << ")";
            } else if constexpr (std::is_same_v<T, AppendReply>) {
                out << "AppendReply(term=" << value.term
                    << ", replica=" << value.replica
                    << ", match=" << value.match_index
                    << ", applied=" << value.applied_index
                    << ", success=" << (value.success ? "true" : "false")
                    << ")";
            } else if constexpr (std::is_same_v<T, InstallSnapshot>) {
                out << "InstallSnapshot(term=" << value.term
                    << ", leader=" << value.leader
                    << ", last=(" << value.last_included_index
                    << "," << value.last_included_term << ")"
                    << ", commit=" << value.leader_commit << ")";
            } else if constexpr (std::is_same_v<T, SnapshotReply>) {
                out << "SnapshotReply(term=" << value.term
                    << ", replica=" << value.replica
                    << ", last=" << value.last_included_index
                    << ", success=" << (value.success ? "true" : "false")
                    << ")";
            } else if constexpr (std::is_same_v<T, CompactLog>) {
                out << "CompactLog(through=" << value.through_index << ")";
            }
            return out.str();
        },
        message);
}

std::string describeTimer(const Timer& timer) {
    return std::visit(
        [](const auto& value) {
            std::ostringstream out;
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ElectionTimer>) {
                out << "ElectionTimer(gen=" << value.generation << ")";
            } else if constexpr (std::is_same_v<T, AppendRetryTimer>) {
                out << "AppendRetryTimer(gen=" << value.generation << ")";
            }
            return out.str();
        },
        timer);
}

struct MessageEnvelope {
    uint64_t id = 0;
    Address from;
    Address to;
    Message message;
};

struct TimerEnvelope {
    uint64_t id = 0;
    Address to;
    Timer timer;
    uint64_t min_delay_ms = 0;
    uint64_t max_delay_ms = 0;
    uint64_t set_order = 0;
};

struct EventRef {
    enum class Kind { Message, Timer };

    Kind kind;
    uint64_t id;

    std::string key() const {
        return std::string(kind == Kind::Message ? "m#" : "tm#") +
               std::to_string(id);
    }
};

namespace ScenarioAddress {

Address client1() {
    return Address("client1");
}

}  // namespace ScenarioAddress

class SearchState;

struct SearchSettings {
    bool deliver_timers = true;
    bool network_active = true;
    std::map<std::pair<Address, Address>, bool> link_active;
    std::map<Address, bool> sender_active;
    std::map<Address, bool> receiver_active;
    std::map<Address, bool> timers_active;
    std::map<Address, bool> node_active;

    SearchSettings& networkActive(bool active) {
        network_active = active;
        return *this;
    }

    SearchSettings& linkActive(Address from, Address to, bool active) {
        link_active[{from.rootAddress(), to.rootAddress()}] = active;
        return *this;
    }

    SearchSettings& senderActive(Address from, bool active) {
        sender_active[from.rootAddress()] = active;
        return *this;
    }

    SearchSettings& receiverActive(Address to, bool active) {
        receiver_active[to.rootAddress()] = active;
        return *this;
    }

    SearchSettings& nodeActive(Address node, bool active) {
        node = node.rootAddress();
        node_active[node] = active;
        sender_active[node] = active;
        receiver_active[node] = active;
        timers_active[node] = active;
        return *this;
    }

    SearchSettings& deliverTimers(bool active) {
        deliver_timers = active;
        return *this;
    }

    SearchSettings& timerActive(Address address, bool active) {
        timers_active[address.rootAddress()] = active;
        return *this;
    }

    SearchSettings& partition(
        const std::vector<std::vector<Address>>& groups) {
        for (size_t i = 0; i < groups.size(); ++i) {
            for (size_t j = 0; j < groups.size(); ++j) {
                bool active = i == j;
                for (const auto& from : groups[i]) {
                    for (const auto& to : groups[j]) {
                        linkActive(from, to, active);
                    }
                }
            }
        }
        return *this;
    }

    SearchSettings& resetNetwork() {
        network_active = true;
        link_active.clear();
        sender_active.clear();
        receiver_active.clear();
        node_active.clear();
        return *this;
    }

    bool shouldDeliver(const MessageEnvelope& envelope) const {
        Address from = envelope.from.rootAddress();
        Address to = envelope.to.rootAddress();
        if (!activeNode(from) || !activeNode(to)) {
            return false;
        }

        auto link = link_active.find({from, to});
        if (link != link_active.end()) {
            return link->second;
        }

        auto sender = sender_active.find(from);
        if (sender != sender_active.end()) {
            return sender->second;
        }

        auto receiver = receiver_active.find(to);
        if (receiver != receiver_active.end()) {
            return receiver->second;
        }

        return from == to || network_active;
    }

    bool deliverTimers(const Address& address) const {
        Address root = address.rootAddress();
        if (!activeNode(root)) {
            return false;
        }
        auto timer = timers_active.find(root);
        if (timer != timers_active.end()) {
            return timer->second;
        }
        return deliver_timers;
    }

private:
    bool activeNode(const Address& address) const {
        auto node = node_active.find(address.rootAddress());
        return node == node_active.end() || node->second;
    }
};

class NodeContext {
public:
    NodeContext(SearchState& state, Address self)
        : state_(state), self_(std::move(self)) {}

    void send(const Address& to, Message message);
    void setTimer(Timer timer, uint64_t delay_ms);
    void setTimer(Timer timer, uint64_t min_delay_ms, uint64_t max_delay_ms);

private:
    SearchState& state_;
    Address self_;
};

class Node {
public:
    explicit Node(Address address) : address_(std::move(address)) {}
    virtual ~Node() = default;

    const Address& address() const {
        return address_;
    }

    virtual void init(NodeContext& ctx) {
        (void) ctx;
    }

    virtual void onMessage(NodeContext& ctx,
                           const Address& from,
                           const Message& message) = 0;

    virtual void onTimer(NodeContext& ctx, const Timer& timer) {
        (void) ctx;
        (void) timer;
    }

    virtual std::unique_ptr<Node> clone() const = 0;
    virtual std::string digest() const = 0;
    virtual std::string describe() const = 0;

private:
    Address address_;
};

class SearchState {
public:
    SearchState() = default;

    SearchState(const SearchState& other)
        : network_(other.network_),
          timers_(other.timers_),
          depth_(other.depth_),
          next_message_id_(other.next_message_id_),
          next_timer_id_(other.next_timer_id_),
          next_timer_order_(other.next_timer_order_),
          exception_(other.exception_) {
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
    }

    SearchState& operator=(const SearchState& other) {
        if (this == &other) {
            return *this;
        }
        nodes_.clear();
        for (const auto& entry : other.nodes_) {
            nodes_.emplace(entry.first, entry.second->clone());
        }
        network_ = other.network_;
        timers_ = other.timers_;
        depth_ = other.depth_;
        next_message_id_ = other.next_message_id_;
        next_timer_id_ = other.next_timer_id_;
        next_timer_order_ = other.next_timer_order_;
        exception_ = other.exception_;
        return *this;
    }

    void addNode(std::unique_ptr<Node> node) {
        Address address = node->address().rootAddress();
        nodes_[address] = std::move(node);
        timers_[address];
        NodeContext ctx(*this, address);
        nodes_[address]->init(ctx);
    }

    void send(const Address& from, const Address& to, Message message) {
        MessageEnvelope envelope{
            next_message_id_++,
            from,
            to,
            std::move(message)
        };
        network_.push_back(std::move(envelope));
    }

    void setTimer(const Address& to,
                  Timer timer,
                  uint64_t min_delay_ms,
                  uint64_t max_delay_ms) {
        TimerEnvelope envelope{
            next_timer_id_++,
            to,
            std::move(timer),
            min_delay_ms,
            max_delay_ms,
            next_timer_order_++
        };
        timers_[to.rootAddress()].push_back(envelope);
    }

    std::vector<EventRef> events(
        const SearchSettings& settings = SearchSettings{}) const {
        std::vector<EventRef> out;
        for (const auto& message : network_) {
            if (nodes_.find(message.to.rootAddress()) != nodes_.end() &&
                settings.shouldDeliver(message)) {
                out.push_back({EventRef::Kind::Message, message.id});
            }
        }

        for (const auto& entry : timers_) {
            if (!settings.deliverTimers(entry.first)) {
                continue;
            }
            uint64_t min_seen_max = 0;
            bool have_min = false;
            for (const auto& timer : entry.second) {
                if (have_min && timer.min_delay_ms >= min_seen_max) {
                    continue;
                }
                out.push_back({EventRef::Kind::Timer, timer.id});
                if (!have_min || timer.max_delay_ms < min_seen_max) {
                    min_seen_max = timer.max_delay_ms;
                    have_min = true;
                }
            }
        }
        return out;
    }

    std::optional<SearchState> stepEvent(
        const EventRef& event,
        const SearchSettings& settings = SearchSettings{}) const {
        auto available = events(settings);
        auto match = std::find_if(
            available.begin(),
            available.end(),
            [&](const EventRef& candidate) {
                return candidate.kind == event.kind && candidate.id == event.id;
            });
        if (match == available.end()) {
            return std::nullopt;
        }
        SearchState next(*this);
        next.depth_++;

        try {
            if (event.kind == EventRef::Kind::Message) {
                auto message = std::find_if(
                    next.network_.begin(),
                    next.network_.end(),
                    [&](const MessageEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (message == next.network_.end()) {
                    return std::nullopt;
                }

                auto node = next.nodes_.find(message->to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, message->to.rootAddress());
                node->second->onMessage(ctx, message->from, message->message);
                return next;
            }

            for (auto& entry : next.timers_) {
                auto timer = std::find_if(
                    entry.second.begin(),
                    entry.second.end(),
                    [&](const TimerEnvelope& envelope) {
                        return envelope.id == event.id;
                    });
                if (timer == entry.second.end()) {
                    continue;
                }

                TimerEnvelope timer_copy = *timer;
                entry.second.erase(timer);
                auto node = next.nodes_.find(timer_copy.to.rootAddress());
                if (node == next.nodes_.end()) {
                    return std::nullopt;
                }

                NodeContext ctx(next, timer_copy.to.rootAddress());
                node->second->onTimer(ctx, timer_copy.timer);
                return next;
            }
        } catch (const std::exception& e) {
            next.exception_ = e.what();
            return next;
        }

        return std::nullopt;
    }

    template <typename T>
    const T* nodeAs(const Address& address) const {
        auto it = nodes_.find(address.rootAddress());
        if (it == nodes_.end()) {
            return nullptr;
        }
        return dynamic_cast<const T*>(it->second.get());
    }

    const std::vector<MessageEnvelope>& network() const {
        return network_;
    }

    const std::map<Address, std::vector<TimerEnvelope>>& timers() const {
        return timers_;
    }

    const std::string& exception() const {
        return exception_;
    }

    std::string digest() const {
        std::ostringstream out;
        out << "depth=" << depth_;
        for (const auto& entry : nodes_) {
            out << ";node(" << entry.first.str() << ")=" << entry.second->digest();
        }
        out << ";network=";
        for (const auto& message : network_) {
            out << "{" << message.id << ":" << message.from.str()
                << ">" << message.to.str() << ":" << describeMessage(message.message)
                << "}";
        }
        out << ";timers=";
        for (const auto& entry : timers_) {
            for (const auto& timer : entry.second) {
                out << "{" << timer.id << ":" << timer.to.str()
                    << ":" << describeTimer(timer.timer) << "}";
            }
        }
        return out.str();
    }

private:
    std::map<Address, std::unique_ptr<Node>> nodes_;
    std::vector<MessageEnvelope> network_;
    std::map<Address, std::vector<TimerEnvelope>> timers_;
    int depth_ = 0;
    uint64_t next_message_id_ = 1;
    uint64_t next_timer_id_ = 1;
    uint64_t next_timer_order_ = 1;
    std::string exception_;
};

void NodeContext::send(const Address& to, Message message) {
    state_.send(self_, to, std::move(message));
}

void NodeContext::setTimer(Timer timer, uint64_t delay_ms) {
    setTimer(std::move(timer), delay_ms, delay_ms);
}

void NodeContext::setTimer(Timer timer,
                           uint64_t min_delay_ms,
                           uint64_t max_delay_ms) {
    state_.setTimer(self_, std::move(timer), min_delay_ms, max_delay_ms);
}

class TestRunner {
public:
    template <typename Fn>
    void test(const std::string& name, Fn fn) {
        try {
            fn();
            ++passed_;
            std::cout << "[PASS] " << name << std::endl;
        } catch (const std::exception& e) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": " << e.what() << std::endl;
        }
    }

    void check(bool condition, const std::string& message) const {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    int finish() const {
        std::cout << "Tests: " << passed_ << " passed, " << failed_
                  << " failed" << std::endl;
        return failed_ == 0 ? 0 : 1;
    }

private:
    int passed_ = 0;
    int failed_ = 0;
};

std::vector<std::string> realTitleSchemaColumns() {
    return {"id:int", "title:string", "kind_id:int", "production_year:int"};
}

CreateTableCommand createTitleTableCommand() {
    return CreateTableCommand{"title", realTitleSchemaColumns()};
}

std::vector<std::string> requireTupleLinesFromFile(
    const std::string& data_file,
    const std::string& table,
    size_t count) {
    std::vector<std::string> rows;
    std::ifstream input(data_file);
    if (!input) {
        throw std::runtime_error("Unable to open tuple file: " + data_file);
    }

    std::string line;
    const std::string prefix = table + "|";
    while (std::getline(input, line)) {
        line = adapterTrim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.rfind(prefix, 0) == 0) {
            rows.push_back(line);
            if (rows.size() == count) {
                return rows;
            }
        }
    }
    throw std::runtime_error(
        "Tuple file " + data_file + " does not contain " +
        std::to_string(count) + " rows for table " + table);
}

std::vector<std::string> requireAllTupleLinesFromFile(
    const std::string& data_file,
    const std::string& table) {
    std::ifstream input(data_file);
    if (!input) {
        throw std::runtime_error("Unable to open tuple file: " + data_file);
    }

    std::vector<std::string> rows;
    std::string line;
    const std::string prefix = table + "|";
    while (std::getline(input, line)) {
        line = adapterTrim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.rfind(prefix, 0) == 0) {
            rows.push_back(line);
        }
    }
    if (rows.empty()) {
        throw std::runtime_error(
            "Tuple file " + data_file + " does not contain rows for table " +
            table);
    }
    return rows;
}

std::vector<std::string> tupleValuesFromLine(
    const std::string& tuple_line,
    const std::string& table) {
    std::vector<std::string> fields = adapterSplitPipeLine(tuple_line);
    if (fields.empty() || fields.front() != table) {
        throw std::runtime_error("Expected tuple for table " + table +
                                 ": " + tuple_line);
    }
    fields.erase(fields.begin());
    return fields;
}

std::string joinTupleLines(const std::vector<std::string>& rows) {
    std::ostringstream out;
    for (const auto& row : rows) {
        out << row << "\n";
    }
    return out.str();
}

std::string replacementProductionYear(const std::string& current_year) {
    return current_year == "1999" ? "2000" : "1999";
}

std::string formatSelectAllResult(const SelectAllResult& result) {
    const auto& columns = result.columns;
    const auto& rows = result.rows;
    std::vector<size_t> widths;
    widths.reserve(columns.size());
    for (const auto& column : columns) {
        widths.push_back(column.size());
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    std::ostringstream out;
    out << "  ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) {
            out << " | ";
        }
        out << std::left << std::setw(static_cast<int>(widths[i]))
            << columns[i];
    }
    out << "\n";

    out << "  ";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) {
            out << "-+-";
        }
        out << std::string(widths[i], '-');
    }
    out << "\n";

    for (const auto& row : rows) {
        out << "  ";
        for (size_t i = 0; i < row.size(); ++i) {
            if (i != 0) {
                out << " | ";
            }
            out << std::left << std::setw(static_cast<int>(widths[i]))
                << row[i];
        }
        out << "\n";
    }
    return out.str();
}

void printLocalBuzzDBTrace(const std::string& data_file) {
    std::cout << "\nTrace: real v104 BuzzDB core through simulator commands" << std::endl;
    std::vector<std::string> title_rows =
        requireTupleLinesFromFile(data_file, "title", 2);
    std::vector<std::string> first_title =
        tupleValuesFromLine(title_rows[0], "title");
    std::vector<std::string> second_title =
        tupleValuesFromLine(title_rows[1], "title");
    const std::string updated_year =
        replacementProductionYear(first_title[3]);

    BuzzDBCore db;
    std::vector<Command> commands{createTitleTableCommand()};
    std::istringstream tuple_stream(joinTupleLines(title_rows));
    auto load_commands = loadTupleFileCommands(tuple_stream, data_file + ":title");
    commands.insert(commands.end(), load_commands.begin(), load_commands.end());
    commands.push_back(parseSQL("UPDATE title SET production_year=" +
                                updated_year + " WHERE id=" +
                                first_title[0]));
    commands.push_back(parseSQL("DELETE FROM title WHERE id=" +
                                second_title[0]));
    commands.push_back(CountRowsCommand{"title"});

    std::cout << "  input: " << data_file << std::endl;
    for (const auto& command : commands) {
        Result result = db.execute(command);
        std::cout << "  " << describeCommand(command) << " -> "
                  << describeResult(result) << std::endl;
    }

    Result query_result = db.execute(parseSQL("PROJECT * FROM title"));
    std::cout << "  PROJECT * FROM title -> "
              << describeResult(query_result) << std::endl;
    if (const auto* rows = std::get_if<SelectAllResult>(&query_result)) {
        std::cout << formatSelectAllResult(*rows);
    }
    std::cout << "  files: " << db.databaseFile() << ", " << db.logFile()
              << ", " << db.masterFile() << std::endl;
    std::cout << "  subsystems: " << db.subsystemSummary()
              << "\n" << std::endl;
}

std::string defaultImdbInputFile() {
    std::filesystem::path source_path(__FILE__);
    if (source_path.is_absolute()) {
        std::filesystem::path candidate = source_path.parent_path() / "imdb.txt";
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    if (std::filesystem::exists("imdb.txt")) {
        return "imdb.txt";
    }
    return "imdb.txt";
}

void printV104BootstrapTrace(const std::string& data_file) {
    std::cout << "Trace: v104-style bootstrap from an IMDB tuple file" << std::endl;
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("buzzdb-v133-bootstrap-trace-" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    try {
        BuzzDBCore db((dir / "buzzdb.dat").string());
        db.bootstrapJobDatabase(data_file);
        std::cout << "  input: " << data_file << std::endl;
        std::cout << "  generated: " << db.databaseFile() << std::endl;
        std::cout << "  storage pages: " << db.storagePageCount() << std::endl;
        auto table_names = db.tableNames();
        std::cout << "  user tables: " << table_names.size() << std::endl;
        for (size_t i = 0; i < std::min<size_t>(table_names.size(), 5); ++i) {
            std::cout << "    " << table_names[i] << " -> "
                      << describeResult(db.execute(
                             CountRowsCommand{table_names[i]})) << std::endl;
        }
        std::cout << "\n" << std::endl;
    } catch (const std::exception& error) {
        std::cout << "  skipped: " << error.what() << "\n" << std::endl;
    }
    std::filesystem::remove_all(dir);
}

std::vector<Address> quorumReplicaAddresses(size_t count) {
    std::vector<Address> replicas;
    for (size_t i = 1; i <= count; ++i) {
        replicas.emplace_back("server" + std::to_string(i));
    }
    return replicas;
}

enum class ConsensusRole { Follower, Candidate, Leader };

std::string consensusRoleName(ConsensusRole role) {
    switch (role) {
        case ConsensusRole::Follower:
            return "follower";
        case ConsensusRole::Candidate:
            return "candidate";
        case ConsensusRole::Leader:
            return "leader";
    }
    return "unknown";
}

enum class ConsensusSlotStatus { Empty, Accepted, Chosen, Cleared };

std::string consensusSlotStatusName(ConsensusSlotStatus status) {
    switch (status) {
        case ConsensusSlotStatus::Empty:
            return "EMPTY";
        case ConsensusSlotStatus::Accepted:
            return "ACCEPTED";
        case ConsensusSlotStatus::Chosen:
            return "CHOSEN";
        case ConsensusSlotStatus::Cleared:
            return "CLEARED";
    }
    return "UNKNOWN";
}

struct ConsensusLogEntry {
    int index = 0;
    int term = 0;
    int client_id = 0;
    int request_id = 0;
    Command command = CreateTableCommand{"", {""}};
    ConsensusSlotStatus status = ConsensusSlotStatus::Accepted;
};

bool sameConsensusEntry(const ConsensusLogEntry& lhs,
                        const ConsensusLogEntry& rhs) {
    return lhs.index == rhs.index &&
           lhs.term == rhs.term &&
           lhs.client_id == rhs.client_id &&
           lhs.request_id == rhs.request_id &&
           lhs.command == rhs.command;
}

struct PendingConsensusClient {
    Address client;
    int index = 0;
};

class ConsensusReplica final : public Node {
public:
    ConsensusReplica(Address address, std::vector<Address> replicas)
        : Node(std::move(address)),
          replicas_(normalizeReplicas(std::move(replicas), this->address())),
          db_() {
        for (const auto& replica : replicas_) {
            match_index_[replica] = 0;
            next_index_[replica] = 1;
            follower_applied_index_[replica] = 0;
        }
    }

    void init(NodeContext& ctx) override {
        resetElectionTimer(ctx);
    }

    void onMessage(NodeContext& ctx,
                   const Address& from,
                   const Message& message) override {
        if (const auto* request = std::get_if<RequestVote>(&message)) {
            handleRequestVote(ctx, from, *request);
            return;
        }
        if (const auto* reply = std::get_if<VoteReply>(&message)) {
            handleVoteReply(ctx, *reply);
            return;
        }
        if (const auto* append = std::get_if<AppendEntries>(&message)) {
            handleAppendEntries(ctx, from, *append);
            return;
        }
        if (const auto* reply = std::get_if<AppendReply>(&message)) {
            handleAppendReply(ctx, *reply);
            return;
        }
        if (const auto* snapshot = std::get_if<InstallSnapshot>(&message)) {
            handleInstallSnapshot(ctx, from, *snapshot);
            return;
        }
        if (const auto* reply = std::get_if<SnapshotReply>(&message)) {
            handleSnapshotReply(ctx, *reply);
            return;
        }
        if (const auto* request = std::get_if<ClientRequest>(&message)) {
            handleClientRequest(ctx, from.rootAddress(), *request);
            return;
        }
        if (const auto* forward = std::get_if<ClientForward>(&message)) {
            handleClientRequest(ctx, forward->client.rootAddress(),
                                forward->request);
            return;
        }
        if (const auto* compact = std::get_if<CompactLog>(&message)) {
            handleCompactLog(ctx, *compact);
            return;
        }
    }

    void onTimer(NodeContext& ctx, const Timer& timer) override {
        if (const auto* election = std::get_if<ElectionTimer>(&timer)) {
            if (election->generation != election_generation_ ||
                role_ == ConsensusRole::Leader) {
                return;
            }
            startElection(ctx);
            return;
        }
        const auto* append_retry = std::get_if<AppendRetryTimer>(&timer);
        if (append_retry == nullptr ||
            append_retry->generation != append_retry_generation_ ||
            role_ != ConsensusRole::Leader) {
            return;
        }
        sendAppendEntriesToPeers(ctx);
        resetAppendRetryTimer(ctx);
    }

    std::unique_ptr<Node> clone() const override {
        return std::make_unique<ConsensusReplica>(*this);
    }

    std::string roleName() const { return consensusRoleName(role_); }
    size_t majoritySize() const { return replicas_.size() / 2 + 1; }
    int commitIndex() const { return commit_index_; }

    Result executeReadOnlyForTest(const Command& command) const {
        return runWithSuppressedStdout([&] {
            BuzzDBCore copy = db_;
            return copy.execute(command);
        });
    }

    std::string digest() const override {
        std::ostringstream out;
        out << "ConsensusReplica(role=" << roleName()
            << ", term=" << current_term_
            << ", leader=" << leader_
            << ", voted_for=" << voted_for_
            << ", votes=" << votes_.size()
            << ", commit=" << commit_index_
            << ", applied=" << applied_index_
            << ", snap=(" << last_included_index_
            << "," << last_included_term_ << ")"
            << ", first=" << first_non_cleared_
            << ", pending=" << pending_by_index_.size()
            << ", log=";
        for (const auto& entry : log_) {
            out << "{" << entry.first
                << ":" << entry.second.term
                << ":" << consensusSlotStatusName(entry.second.status)
                << ":" << entry.second.client_id
                << ":" << entry.second.request_id
                << ":" << describeCommand(entry.second.command) << "}";
        }
        out << ")";
        return out.str();
    }

    std::string describe() const override {
        return digest();
    }

private:
    static std::vector<Address> normalizeReplicas(std::vector<Address> replicas,
                                                  const Address& self) {
        for (auto& replica : replicas) {
            replica = replica.rootAddress();
        }
        if (std::find(replicas.begin(), replicas.end(), self.rootAddress()) ==
            replicas.end()) {
            replicas.push_back(self.rootAddress());
        }
        std::sort(replicas.begin(), replicas.end());
        replicas.erase(std::unique(replicas.begin(), replicas.end()),
                       replicas.end());
        return replicas;
    }

    std::vector<Address> peers() const {
        std::vector<Address> out;
        for (const auto& replica : replicas_) {
            if (!(replica == address().rootAddress())) {
                out.push_back(replica);
            }
        }
        return out;
    }

    bool hasMajority(const std::set<Address>& responders) const {
        return responders.size() >= majoritySize();
    }

    bool hasMajority(size_t count) const {
        return count >= majoritySize();
    }

    void resetElectionTimer(NodeContext& ctx) {
        ctx.setTimer(ElectionTimer{++election_generation_}, 10, 20);
    }

    void resetAppendRetryTimer(NodeContext& ctx) {
        ctx.setTimer(AppendRetryTimer{++append_retry_generation_}, 4, 6);
    }

    void stepDownTo(int term, Address leader = Address()) {
        if (term > current_term_) {
            current_term_ = term;
            voted_for_ = Address();
        }
        role_ = ConsensusRole::Follower;
        votes_.clear();
        if (!addressEmpty(leader)) {
            leader_ = leader.rootAddress();
        }
    }

    void stepDownAfterMembershipRemoval() {
        role_ = ConsensusRole::Follower;
        leader_ = Address();
        votes_.clear();
    }

    int lastLogIndex() const {
        return log_.empty() ? last_included_index_
                            : std::max(last_included_index_,
                                       log_.rbegin()->first);
    }

    int lastLogTerm() const {
        return termAt(lastLogIndex());
    }

    int termAt(int index) const {
        if (index == 0) {
            return 0;
        }
        if (index == last_included_index_) {
            return last_included_term_;
        }
        auto it = log_.find(index);
        return it == log_.end() ? 0 : it->second.term;
    }

    bool logUpToDate(int last_log_index, int last_log_term) const {
        if (last_log_term != lastLogTerm()) {
            return last_log_term > lastLogTerm();
        }
        return last_log_index >= lastLogIndex();
    }

    void startElection(NodeContext& ctx) {
        role_ = ConsensusRole::Candidate;
        current_term_++;
        voted_for_ = address().rootAddress();
        leader_ = Address();
        votes_.clear();
        votes_.insert(address().rootAddress());

        for (const auto& peer : peers()) {
            ctx.send(peer, RequestVote{
                current_term_,
                address().rootAddress(),
                lastLogIndex(),
                lastLogTerm()
            });
        }
        if (hasMajority(votes_)) {
            becomeLeader(ctx);
        }
    }

    void becomeLeader(NodeContext& ctx) {
        role_ = ConsensusRole::Leader;
        leader_ = address().rootAddress();
        match_index_[address().rootAddress()] = lastLogIndex();
        for (const auto& peer : peers()) {
            next_index_[peer.rootAddress()] = lastLogIndex() + 1;
            match_index_[peer.rootAddress()] = 0;
        }
        sendAppendEntriesToPeers(ctx);
        resetAppendRetryTimer(ctx);
    }

    void sendAppendEntriesToPeers(NodeContext& ctx) {
        for (const auto& peer : peers()) {
            sendAppendEntriesTo(ctx, peer);
        }
    }

    void sendAppendEntriesTo(NodeContext& ctx, const Address& peer) {
        Address root = peer.rootAddress();
        int next = next_index_.count(root) == 0 ? lastLogIndex() + 1
                                                : next_index_[root];
        if (next < 1) {
            next = 1;
        }
        if (next <= last_included_index_) {
            sendInstallSnapshotTo(ctx, root);
            return;
        }
        int prev = next - 1;
        std::vector<ConsensusLogEntryMessage> entries;
        for (int index = next; index <= lastLogIndex(); ++index) {
            auto it = log_.find(index);
            if (it == log_.end()) {
                continue;
            }
            entries.push_back(ConsensusLogEntryMessage{
                it->second.index,
                it->second.term,
                it->second.client_id,
                it->second.request_id,
                it->second.command
            });
        }
        ctx.send(root, AppendEntries{
            current_term_,
            address().rootAddress(),
            prev,
            termAt(prev),
            entries,
            commit_index_,
            last_included_index_
        });
    }

    void sendInstallSnapshotTo(NodeContext& ctx, const Address& peer) {
        ctx.send(peer.rootAddress(), InstallSnapshot{
            current_term_,
            address().rootAddress(),
            last_included_index_,
            last_included_term_,
            commit_index_,
            db_,
            session_results_,
            execution_count_
        });
    }

    void handleRequestVote(NodeContext& ctx,
                           const Address& from,
                           const RequestVote& request) {
        if (request.term < current_term_) {
            ctx.send(from, VoteReply{
                current_term_,
                address().rootAddress(),
                false
            });
            return;
        }
        if (request.term > current_term_) {
            stepDownTo(request.term);
        }

        bool can_vote = addressEmpty(voted_for_) ||
                        voted_for_ == request.candidate.rootAddress();
        bool up_to_date =
            logUpToDate(request.last_log_index, request.last_log_term);
        if (can_vote && up_to_date) {
            role_ = ConsensusRole::Follower;
            voted_for_ = request.candidate.rootAddress();
            leader_ = Address();
        }

        ctx.send(from, VoteReply{
            current_term_,
            address().rootAddress(),
            can_vote && up_to_date
        });
    }

    void handleVoteReply(NodeContext& ctx, const VoteReply& reply) {
        if (reply.term > current_term_) {
            stepDownTo(reply.term);
            return;
        }
        if (role_ != ConsensusRole::Candidate ||
            reply.term != current_term_ ||
            !reply.granted) {
            return;
        }
        votes_.insert(reply.voter.rootAddress());
        if (hasMajority(votes_)) {
            becomeLeader(ctx);
        }
    }

    void handleClientRequest(NodeContext& ctx,
                             const Address& client,
                             const ClientRequest& request) {
        if (role_ != ConsensusRole::Leader) {
            if (!addressEmpty(leader_) &&
                !(leader_ == address().rootAddress())) {
                ctx.send(leader_, ClientForward{client, request});
            }
            return;
        }

        std::string key = requestKey(request.client_id, request.request_id);
        auto cached = session_results_.find(key);
        if (cached != session_results_.end()) {
            ctx.send(client, ClientReply{
                request.client_id,
                request.request_id,
                cached->second
            });
            return;
        }
        if (pending_by_request_.count(key) != 0) {
            return;
        }

        ConsensusLogEntry entry{
            lastLogIndex() + 1,
            current_term_,
            request.client_id,
            request.request_id,
            request.command,
            ConsensusSlotStatus::Accepted
        };
        log_[entry.index] = entry;
        match_index_[address().rootAddress()] = entry.index;
        pending_by_index_[entry.index] = PendingConsensusClient{client,
                                                                entry.index};
        pending_by_request_[key] = entry.index;

        sendAppendEntriesToPeers(ctx);
        resetAppendRetryTimer(ctx);
        advanceCommitIndex(ctx);
    }

    void handleAppendEntries(NodeContext& ctx,
                             const Address& from,
                             const AppendEntries& append) {
        if (append.term < current_term_) {
            ctx.send(from, AppendReply{
                current_term_,
                address().rootAddress(),
                lastLogIndex(),
                applied_index_,
                false
            });
            return;
        }
        if (append.term > current_term_ ||
            role_ != ConsensusRole::Follower ||
            !(leader_ == append.leader.rootAddress())) {
            stepDownTo(append.term, append.leader);
        }

        if (append.prev_log_index > 0 &&
            termAt(append.prev_log_index) != append.prev_log_term) {
            ctx.send(from, AppendReply{
                current_term_,
                address().rootAddress(),
                std::min(lastLogIndex(), append.prev_log_index - 1),
                applied_index_,
                false
            });
            return;
        }

        for (const auto& message_entry : append.entries) {
            if (message_entry.index <= last_included_index_) {
                continue;
            }
            auto existing = log_.find(message_entry.index);
            if (existing != log_.end()) {
                ConsensusLogEntry incoming{
                    message_entry.index,
                    message_entry.term,
                    message_entry.client_id,
                    message_entry.request_id,
                    message_entry.command,
                    existing->second.status
                };
                if (existing->second.status == ConsensusSlotStatus::Chosen &&
                    !sameConsensusEntry(existing->second, incoming)) {
                    ctx.send(from, AppendReply{
                        current_term_,
                        address().rootAddress(),
                        message_entry.index - 1,
                        applied_index_,
                        false
                    });
                    return;
                }
                if (!sameConsensusEntry(existing->second, incoming)) {
                    auto erase_from = log_.lower_bound(message_entry.index);
                    log_.erase(erase_from, log_.end());
                    break;
                }
            }
        }

        for (const auto& message_entry : append.entries) {
            if (message_entry.index <= last_included_index_) {
                continue;
            }
            if (log_.count(message_entry.index) != 0) {
                continue;
            }
            log_[message_entry.index] = ConsensusLogEntry{
                message_entry.index,
                message_entry.term,
                message_entry.client_id,
                message_entry.request_id,
                message_entry.command,
                ConsensusSlotStatus::Accepted
            };
        }

        if (append.leader_commit > commit_index_) {
            commit_index_ = std::min(append.leader_commit, lastLogIndex());
            markChosenThrough(commit_index_);
            applyCommitted(ctx);
        }
        compactThrough(std::min(append.compacted_through, applied_index_));

        int match = append.entries.empty()
                        ? append.prev_log_index
                        : append.entries.back().index;
        match = std::max(match, last_included_index_);
        ctx.send(from, AppendReply{
            current_term_,
            address().rootAddress(),
            match,
            applied_index_,
            true
        });
    }

    void handleAppendReply(NodeContext& ctx, const AppendReply& reply) {
        if (reply.term > current_term_) {
            stepDownTo(reply.term);
            return;
        }
        if (role_ != ConsensusRole::Leader ||
            reply.term != current_term_ ||
            addressEmpty(reply.replica)) {
            return;
        }

        Address replica = reply.replica.rootAddress();
        follower_applied_index_[replica] =
            std::max(follower_applied_index_[replica], reply.applied_index);
        if (!reply.success) {
            int next = next_index_.count(replica) == 0 ? lastLogIndex() + 1
                                                       : next_index_[replica];
            next_index_[replica] = std::max(1, std::min(next - 1,
                                                        reply.match_index + 1));
            sendAppendEntriesTo(ctx, replica);
            return;
        }

        match_index_[replica] =
            std::max(match_index_[replica], reply.match_index);
        next_index_[replica] = match_index_[replica] + 1;
        advanceCommitIndex(ctx);
        if (next_index_[replica] <= lastLogIndex()) {
            sendAppendEntriesTo(ctx, replica);
        }
    }

    void handleInstallSnapshot(NodeContext& ctx,
                               const Address& from,
                               const InstallSnapshot& snapshot) {
        if (snapshot.term < current_term_) {
            ctx.send(from, SnapshotReply{
                current_term_,
                address().rootAddress(),
                last_included_index_,
                false
            });
            return;
        }
        if (snapshot.term > current_term_ ||
            role_ != ConsensusRole::Follower ||
            !(leader_ == snapshot.leader.rootAddress())) {
            stepDownTo(snapshot.term, snapshot.leader);
        }
        if (snapshot.last_included_index <= last_included_index_) {
            ctx.send(from, SnapshotReply{
                current_term_,
                address().rootAddress(),
                last_included_index_,
                true
            });
            return;
        }

        db_ = snapshot.db;
        session_results_ = snapshot.session_results;
        execution_count_ = snapshot.execution_count;
        last_included_index_ = snapshot.last_included_index;
        last_included_term_ = snapshot.last_included_term;
        first_non_cleared_ = last_included_index_ + 1;
        commit_index_ = std::max(commit_index_, snapshot.last_included_index);
        commit_index_ = std::max(commit_index_,
                                 std::min(snapshot.leader_commit,
                                          snapshot.last_included_index));
        applied_index_ = std::max(applied_index_, snapshot.last_included_index);
        follower_applied_index_[address().rootAddress()] = applied_index_;

        auto erase_to = log_.upper_bound(last_included_index_);
        log_.erase(log_.begin(), erase_to);
        ctx.send(from, SnapshotReply{
            current_term_,
            address().rootAddress(),
            last_included_index_,
            true
        });
    }

    void handleSnapshotReply(NodeContext& ctx, const SnapshotReply& reply) {
        if (reply.term > current_term_) {
            stepDownTo(reply.term);
            return;
        }
        if (role_ != ConsensusRole::Leader ||
            reply.term != current_term_ ||
            addressEmpty(reply.replica) ||
            !reply.success) {
            return;
        }
        Address replica = reply.replica.rootAddress();
        match_index_[replica] =
            std::max(match_index_[replica], reply.last_included_index);
        next_index_[replica] = match_index_[replica] + 1;
        follower_applied_index_[replica] =
            std::max(follower_applied_index_[replica],
                     reply.last_included_index);
        advanceCommitIndex(ctx);
        if (next_index_[replica] <= lastLogIndex()) {
            sendAppendEntriesTo(ctx, replica);
        }
    }

    void handleCompactLog(NodeContext& ctx, const CompactLog& compact) {
        if (role_ != ConsensusRole::Leader) {
            return;
        }
        int stable = std::min(compact.through_index, applied_index_);
        compactThrough(stable);
        sendAppendEntriesToPeers(ctx);
    }

    void advanceCommitIndex(NodeContext& ctx) {
        for (int index = lastLogIndex(); index > commit_index_; --index) {
            auto it = log_.find(index);
            if (it == log_.end() || it->second.term != current_term_) {
                continue;
            }

            size_t count = 0;
            for (const auto& replica : replicas_) {
                auto match = match_index_.find(replica.rootAddress());
                if (match != match_index_.end() && match->second >= index) {
                    ++count;
                }
            }
            if (!hasMajority(count)) {
                continue;
            }

            commit_index_ = index;
            markChosenThrough(commit_index_);
            applyCommitted(ctx);
            sendAppendEntriesToPeers(ctx);
            return;
        }
    }

    void compactThrough(int index) {
        if (index <= last_included_index_ || index <= 0) {
            return;
        }
        index = std::min(index, applied_index_);
        if (index <= last_included_index_) {
            return;
        }
        last_included_term_ = termAt(index);
        last_included_index_ = index;
        first_non_cleared_ = last_included_index_ + 1;
        auto erase_to = log_.upper_bound(last_included_index_);
        log_.erase(log_.begin(), erase_to);
    }

    void markChosenThrough(int index) {
        for (int slot = first_non_cleared_; slot <= index; ++slot) {
            auto it = log_.find(slot);
            if (it != log_.end()) {
                it->second.status = ConsensusSlotStatus::Chosen;
            }
        }
    }

    void applyCommitted(NodeContext& ctx) {
        while (applied_index_ < commit_index_) {
            int next = applied_index_ + 1;
            auto entry_it = log_.find(next);
            if (entry_it == log_.end() ||
                entry_it->second.status != ConsensusSlotStatus::Chosen) {
                return;
            }

            ConsensusLogEntry entry = entry_it->second;
            std::string key = requestKey(entry.client_id, entry.request_id);
            Result result = StatementOkResult{};
            auto cached = session_results_.find(key);
            if (cached != session_results_.end()) {
                result = cached->second;
            } else {
                result = runWithSuppressedStdout([&] {
                    return db_.execute(entry.command);
                });
                session_results_[key] = result;
                execution_count_[key]++;
            }

            if (result == Result{StatementOkResult{}} &&
                finalizesConfigWithoutReplica(entry.command, address())) {
                stepDownAfterMembershipRemoval();
            }

            applied_index_ = entry.index;
            follower_applied_index_[address().rootAddress()] = applied_index_;
            auto pending = pending_by_index_.find(entry.index);
            if (pending != pending_by_index_.end()) {
                ctx.send(pending->second.client, ClientReply{
                    entry.client_id,
                    entry.request_id,
                    result
                });
                pending_by_request_.erase(key);
                pending_by_index_.erase(pending);
            }
        }
    }

    std::vector<Address> replicas_;
    ConsensusRole role_ = ConsensusRole::Follower;
    int current_term_ = 0;
    Address voted_for_;
    Address leader_;
    int election_generation_ = 0;
    int append_retry_generation_ = 0;
    std::set<Address> votes_;
    int commit_index_ = 0;
    int applied_index_ = 0;
    int first_non_cleared_ = 1;
    int last_included_index_ = 0;
    int last_included_term_ = 0;
    BuzzDBCore db_;
    std::map<int, ConsensusLogEntry> log_;
    std::map<Address, int> match_index_;
    std::map<Address, int> next_index_;
    std::map<Address, int> follower_applied_index_;
    std::map<int, PendingConsensusClient> pending_by_index_;
    std::map<std::string, int> pending_by_request_;
    std::map<std::string, Result> session_results_;
    std::map<std::string, int> execution_count_;
};

const MessageEnvelope* messageForEvent(const SearchState& state,
                                       const EventRef& event) {
    if (event.kind != EventRef::Kind::Message) {
        return nullptr;
    }
    for (const auto& envelope : state.network()) {
        if (envelope.id == event.id) {
            return &envelope;
        }
    }
    return nullptr;
}

const TimerEnvelope* timerForEvent(const SearchState& state,
                                   const EventRef& event) {
    if (event.kind != EventRef::Kind::Timer) {
        return nullptr;
    }
    for (const auto& entry : state.timers()) {
        for (const auto& timer : entry.second) {
            if (timer.id == event.id) {
                return &timer;
            }
        }
    }
    return nullptr;
}

EventRef requireMessageEvent(
    const SearchState& state,
    const std::function<bool(const MessageEnvelope&)>& predicate,
    const std::string& label,
    const SearchSettings& settings = SearchSettings{}) {
    for (const auto& event : state.events(settings)) {
        const auto* message = messageForEvent(state, event);
        if (message != nullptr && predicate(*message)) {
            return event;
        }
    }
    throw std::runtime_error("missing message event: " + label);
}

EventRef requireTimerEvent(
    const SearchState& state,
    const std::function<bool(const TimerEnvelope&)>& predicate,
    const std::string& label,
    const SearchSettings& settings = SearchSettings{}) {
    for (const auto& event : state.events(settings)) {
        const auto* timer = timerForEvent(state, event);
        if (timer != nullptr && predicate(*timer)) {
            return event;
        }
    }
    throw std::runtime_error("missing timer event: " + label);
}

SearchState stepRequired(const SearchState& state,
                         const EventRef& event,
                         const SearchSettings& settings = SearchSettings{}) {
    auto next = state.stepEvent(event, settings);
    if (!next) {
        throw std::runtime_error("event was not deliverable: " + event.key());
    }
    if (!next->exception().empty()) {
        throw std::runtime_error(
            "event " + event.key() + " raised: " + next->exception());
    }
    return *next;
}

SearchState consensusClusterState(const std::vector<Address>& replicas) {
    SearchState state;
    for (const auto& replica : replicas) {
        state.addNode(std::make_unique<ConsensusReplica>(replica, replicas));
    }
    return state;
}

EventRef requireElectionTimerFor(const SearchState& state,
                                 const Address& replica,
                                 const SearchSettings& settings =
                                     SearchSettings{}) {
    return requireTimerEvent(
        state,
        [&](const TimerEnvelope& timer) {
            return timer.to.rootAddress() == replica.rootAddress() &&
                   std::get_if<ElectionTimer>(&timer.timer) != nullptr;
        },
        "election timer for " + replica.str(),
        settings);
}

SearchState electConsensusLeader(SearchState state,
                                 const Address& candidate,
                                 const std::vector<Address>& voters,
                                 const SearchSettings& settings =
                                     SearchSettings{}) {
    state = stepRequired(
        state,
        requireElectionTimerFor(state, candidate, settings),
        settings);

    for (const auto& voter : voters) {
        state = stepRequired(
            state,
            requireMessageEvent(
                state,
                [&](const MessageEnvelope& envelope) {
                    const auto* vote =
                        std::get_if<RequestVote>(&envelope.message);
                    return vote != nullptr &&
                           vote->candidate == candidate.rootAddress() &&
                           envelope.from.rootAddress() ==
                               candidate.rootAddress() &&
                           envelope.to.rootAddress() == voter.rootAddress();
                },
                "request vote " + candidate.str() + " -> " + voter.str(),
                settings),
            settings);
    }

    for (const auto& voter : voters) {
        state = stepRequired(
            state,
            requireMessageEvent(
                state,
                [&](const MessageEnvelope& envelope) {
                    const auto* reply =
                        std::get_if<VoteReply>(&envelope.message);
                    return reply != nullptr &&
                           reply->granted &&
                           reply->voter == voter.rootAddress() &&
                           envelope.from.rootAddress() == voter.rootAddress() &&
                           envelope.to.rootAddress() ==
                               candidate.rootAddress();
                },
                "vote reply " + voter.str() + " -> " + candidate.str(),
                settings),
            settings);
    }
    return state;
}

SearchState deliverConsensusCommandToQuorum(
    SearchState state,
    const Address& leader,
    const Address& client,
    int client_id,
    int request_id,
    const Command& command,
    const std::vector<Address>& acceptors,
    const SearchSettings& settings = SearchSettings{}) {
    state.send(client, leader, ClientRequest{client_id, request_id, command});
    state = stepRequired(
        state,
        requireMessageEvent(
            state,
            [&](const MessageEnvelope& envelope) {
                const auto* request =
                    std::get_if<ClientRequest>(&envelope.message);
                return request != nullptr &&
                       request->client_id == client_id &&
                       request->request_id == request_id &&
                       envelope.from.rootAddress() == client.rootAddress() &&
                       envelope.to.rootAddress() == leader.rootAddress();
            },
            "consensus client request",
            settings),
        settings);

    int slot = 0;
    for (const auto& acceptor : acceptors) {
        EventRef accept_event = requireMessageEvent(
            state,
            [&](const MessageEnvelope& envelope) {
                const auto* append =
                    std::get_if<AppendEntries>(&envelope.message);
                return append != nullptr &&
                       !append->entries.empty() &&
                       append->entries.back().client_id == client_id &&
                       append->entries.back().request_id == request_id &&
                       envelope.from.rootAddress() == leader.rootAddress() &&
                       envelope.to.rootAddress() == acceptor.rootAddress();
            },
            "append entry " + leader.str() + " -> " + acceptor.str(),
            settings);
        const auto* envelope = messageForEvent(state, accept_event);
        const auto* append =
            std::get_if<AppendEntries>(&envelope->message);
        slot = append->entries.back().index;
        state = stepRequired(state, accept_event, settings);
    }

    for (const auto& acceptor : acceptors) {
        state = stepRequired(
            state,
            requireMessageEvent(
                state,
                [&](const MessageEnvelope& envelope) {
                    const auto* reply =
                        std::get_if<AppendReply>(&envelope.message);
                    return reply != nullptr &&
                           reply->success &&
                           reply->match_index == slot &&
                           reply->replica == acceptor.rootAddress() &&
                           envelope.from.rootAddress() ==
                               acceptor.rootAddress() &&
                           envelope.to.rootAddress() == leader.rootAddress();
                },
                "append reply " + acceptor.str(),
                settings),
            settings);
    }

    for (const auto& acceptor : acceptors) {
        state = stepRequired(
            state,
            requireMessageEvent(
                state,
                [&](const MessageEnvelope& envelope) {
                    const auto* append =
                        std::get_if<AppendEntries>(&envelope.message);
                    return append != nullptr &&
                           append->leader == leader.rootAddress() &&
                           append->leader_commit >= slot &&
                           envelope.from.rootAddress() ==
                               leader.rootAddress() &&
                           envelope.to.rootAddress() ==
                               acceptor.rootAddress();
                },
                "commit append " + leader.str() + " -> " + acceptor.str(),
                settings),
            settings);
    }
    return state;
}

const SelectAllResult* catalogRowsOn(const ConsensusReplica* node,
                                     const std::string& table,
                                     Result* storage) {
    if (node == nullptr) return nullptr;
    *storage = node->executeReadOnlyForTest(ReadSystemCatalogCommand{table});
    return std::get_if<SelectAllResult>(storage);
}

size_t catalogRowCountOn(const ConsensusReplica* node,
                         const std::string& table) {
    Result result = TableNotFoundResult{};
    const auto* rows = catalogRowsOn(node, table, &result);
    return rows == nullptr ? 0 : rows->rows.size();
}

std::optional<size_t> catalogColumnIndex(const SelectAllResult& rows,
                                         const std::string& column) {
    auto it = std::find(rows.columns.begin(), rows.columns.end(), column);
    if (it == rows.columns.end()) return std::nullopt;
    return static_cast<size_t>(std::distance(rows.columns.begin(), it));
}

bool selectAllHasRow(
    const SelectAllResult& rows,
    const std::map<std::string, std::string>& expected) {
    std::map<std::string, size_t> column_indexes;
    for (const auto& [column, value] : expected) {
        (void)value;
        auto index = catalogColumnIndex(rows, column);
        if (!index.has_value()) return false;
        column_indexes[column] = *index;
    }
    for (const auto& row : rows.rows) {
        bool matches = true;
        for (const auto& [column, value] : expected) {
            size_t index = column_indexes[column];
            if (index >= row.size() || row[index] != value) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }
    return false;
}

bool catalogHasRowOn(
    const ConsensusReplica* node,
    const std::string& table,
    const std::map<std::string, std::string>& expected) {
    Result result = TableNotFoundResult{};
    const auto* rows = catalogRowsOn(node, table, &result);
    if (rows == nullptr) return false;
    return selectAllHasRow(*rows, expected);
}

std::optional<std::string> selectAllValue(
    const SelectAllResult& rows,
    const std::vector<std::string>& row,
    const std::string& column) {
    auto index = catalogColumnIndex(rows, column);
    if (!index.has_value() || *index >= row.size()) return std::nullopt;
    return row[*index];
}

size_t currentGlobalKeyCountOn(const ConsensusReplica* node,
                               const std::string& range_id) {
    Result storage = TableNotFoundResult{};
    const auto* rows = catalogRowsOn(node, "__global_keys", &storage);
    if (rows == nullptr) return size_t{0};
    auto table_column = catalogColumnIndex(*rows, "table_name");
    auto primary_column = catalogColumnIndex(*rows, "primary_key");
    auto row_key_column = catalogColumnIndex(*rows, "row_key");
    auto range_column = catalogColumnIndex(*rows, "range_id");
    auto version_column = catalogColumnIndex(*rows, "descriptor_version");
    auto status_column = catalogColumnIndex(*rows, "status");
    if (!table_column.has_value() ||
        !primary_column.has_value() ||
        !row_key_column.has_value() ||
        !range_column.has_value() ||
        !version_column.has_value()) {
        return size_t{0};
    }

    struct LatestGlobalKey {
        std::string range_id;
        int version = 0;
        std::string status = "active";
    };
    std::map<std::string, LatestGlobalKey> latest;
    std::string separator(1, '\0');
    for (const auto& row : rows->rows) {
        if (*table_column >= row.size() ||
            *primary_column >= row.size() ||
            *row_key_column >= row.size() ||
            *range_column >= row.size() ||
            *version_column >= row.size()) {
            continue;
        }
        std::string key = row[*table_column] + separator +
                          row[*primary_column] + separator +
                          row[*row_key_column];
        int version = std::stoi(row[*version_column]);
        std::string status = "active";
        if (status_column.has_value() && *status_column < row.size()) {
            status = row[*status_column];
        }
        auto it = latest.find(key);
        if (it == latest.end() || version >= it->second.version) {
            latest[key] = LatestGlobalKey{row[*range_column],
                                          version,
                                          status};
        }
    }

    size_t count = 0;
    for (const auto& [key, record] : latest) {
        (void)key;
        if (record.range_id == range_id && record.status == "active") {
            ++count;
        }
    }
    return count;
}

Command bootstrapClusterACommand() {
    return BootstrapClusterCommand{
        "cluster-A", "server1", {"server1", "server2", "server3"}, 1};
}

Command registerGroup1Command() {
    return RegisterReplicaGroupCommand{
        "group-1", {"server1", "server2", "server3"}, 1};
}

SearchState electedConsensusState(const std::vector<Address>& replicas,
                                  const Address& leader) {
    SearchState state = consensusClusterState(replicas);
    return electConsensusLeader(state, leader, {replicas[1], replicas[2]});
}

SearchState buildInitialReplicaGroupState(
    const std::vector<Address>& replicas,
    const Address& leader,
    const Address& client,
    int client_id) {
    SearchState state = electedConsensusState(replicas, leader);
    state = deliverConsensusCommandToQuorum(
        state, leader, client, client_id, 1,
        bootstrapClusterACommand(), {replicas[1], replicas[2]});
    state = deliverConsensusCommandToQuorum(
        state, leader, client, client_id, 2,
        registerGroup1Command(), {replicas[1], replicas[2]});
    return state;
}

void printPartitionedSQLTrace(const std::string& data_file) {
    std::cout << "Trace: physical range movement with snapshot catch-up" << std::endl;

    struct TraceGroup {
        std::string group_id;
        std::vector<Address> replicas;
        Address leader;
        Address client;
        SearchState state;
        int client_id = 0;
        int next_request_id = 1;
    };

    auto prefixed = [](const std::string& prefix) {
        std::vector<Address> replicas;
        for (size_t i = 1; i <= 3; ++i) {
            replicas.emplace_back(prefix + std::to_string(i));
        }
        return replicas;
    };
    auto makeGroup = [&](const std::string& group_id,
                         const std::string& prefix,
                         int client_id) {
        TraceGroup group;
        group.group_id = group_id;
        group.replicas = prefixed(prefix);
        group.leader = group.replicas.front();
        group.client = Address("client-" + group_id);
        group.state = electedConsensusState(group.replicas, group.leader);
        group.client_id = client_id;
        return group;
    };
    auto replyFor = [](const SearchState& state,
                       int client_id,
                       int request_id) -> std::optional<Result> {
        for (auto it = state.network().rbegin();
             it != state.network().rend(); ++it) {
            const auto* reply = std::get_if<ClientReply>(&it->message);
            if (reply != nullptr &&
                reply->client_id == client_id &&
                reply->request_id == request_id) {
                return reply->result;
            }
        }
        return std::nullopt;
    };
    auto commit = [&](TraceGroup& group, const Command& command) {
        int request_id = group.next_request_id++;
        group.state = deliverConsensusCommandToQuorum(
            group.state,
            group.leader,
            group.client,
            group.client_id,
            request_id,
            command,
            {group.replicas[1], group.replicas[2]});
        auto reply = replyFor(group.state, group.client_id, request_id);
        if (!reply.has_value()) {
            throw std::runtime_error("missing trace reply for " +
                                     describeCommand(command));
        }
        return *reply;
    };
    auto leaderNode = [](const TraceGroup& group) {
        const auto* node = group.state.nodeAs<ConsensusReplica>(group.leader);
        if (node == nullptr) {
            throw std::runtime_error("missing trace leader");
        }
        return node;
    };

    std::vector<std::string> title_rows =
        requireTupleLinesFromFile(data_file, "title", 16);
    std::vector<std::pair<std::string, std::vector<std::string>>> all_titles;
    for (const auto& row : title_rows) {
        all_titles.push_back({row, tupleValuesFromLine(row, "title")});
    }
    if (all_titles.size() < 9) {
        throw std::runtime_error(
            "Need enough title rows for range transfer trace.");
    }
    std::vector<std::pair<std::string, std::vector<std::string>>> titles(
        all_titles.begin(), all_titles.begin() + 8);
    std::vector<std::pair<std::string, std::vector<std::string>>>
        sorted_titles = titles;
    std::sort(
        sorted_titles.begin(),
        sorted_titles.end(),
        [](const auto& lhs, const auto& rhs) {
            return tableRowKey("title", lhs.second.front()) <
                   tableRowKey("title", rhs.second.front());
        });
    size_t split_index = sorted_titles.size() / 2;
    std::string left_range = defaultRangeIdForTable("title") + "-left";
    std::string right_range = defaultRangeIdForTable("title") + "-right";
    std::string split_key =
        tableRowKey("title", sorted_titles[split_index].second.front());

    std::set<std::string> initial_title_ids;
    for (const auto& title : titles) {
        initial_title_ids.insert(title.second.front());
    }
    std::pair<std::string, std::vector<std::string>> catchup_title;
    for (size_t i = 8; i < all_titles.size(); ++i) {
        if (all_titles[i].second.empty() ||
            initial_title_ids.count(all_titles[i].second.front()) != 0) {
            continue;
        }
        if (tableRowKey("title", all_titles[i].second.front()) < split_key) {
            catchup_title = all_titles[i];
            break;
        }
    }
    if (catchup_title.second.empty()) {
        throw std::runtime_error(
            "Need a post-snapshot title row in the left range.");
    }
    std::string primary_key = catchup_title.second.front();

    TraceGroup catalog = makeGroup("group-catalog", "catalog", 1);
    TraceGroup source = makeGroup("group-1", "source", 2);
    TraceGroup target = makeGroup("group-2", "target", 3);

    auto routeFor = [&](const std::string& key) {
        Result result = leaderNode(catalog)->executeReadOnlyForTest(
            ExplainRouteCommand{"title", key, false});
        const auto* route = std::get_if<RouteResult>(&result);
        if (route == nullptr || route->range_ids.empty() ||
            route->replica_group_ids.empty()) {
            throw std::runtime_error("missing trace route for " + key);
        }
        return *route;
    };
    auto recordCatalogKey = [&](const std::vector<std::string>& row) {
        const std::string& key = row.front();
        RouteResult route = routeFor(key);
        Result recorded = commit(
            catalog,
            InsertRowCommand{
                "__global_keys",
                {"title",
                 key,
                 tableRowKey("title", key),
                 route.range_ids.front(),
                 route.replica_group_ids.front(),
                 std::to_string(route.descriptor_versions.front()),
                 "active"}});
        if (!(recorded == Result{InsertOkResult{}})) {
            throw std::runtime_error("unable to record trace catalog key");
        }
    };
    auto selectAllTitles = [&](const TraceGroup& group) {
        Result result = leaderNode(group)->executeReadOnlyForTest(
            SelectAllCommand{"title"});
        const auto* rows = std::get_if<SelectAllResult>(&result);
        if (rows == nullptr) return SelectAllResult{};
        return *rows;
    };
    auto leftRows = [&](const TraceGroup& group) {
        SelectAllResult all = selectAllTitles(group);
        SelectAllResult filtered{all.columns, {}};
        for (const auto& row : all.rows) {
            auto id = selectAllValue(all, row, "id");
            if (id.has_value() && tableRowKey("title", *id) < split_key) {
                filtered.rows.push_back(row);
            }
        }
        return filtered;
    };
    auto requireStatementOk = [](const Result& result,
                                 const std::string& step) {
        if (!std::holds_alternative<StatementOkResult>(result)) {
            throw std::runtime_error("range transfer trace failed at " + step +
                                     ": " + describeResult(result));
        }
    };

    commit(catalog, bootstrapClusterACommand());
    commit(catalog, registerGroup1Command());
    commit(catalog,
           RegisterReplicaGroupCommand{
               target.group_id, {"target1", "target2", "target3"}, 1});
    commit(catalog, createTitleTableCommand());
    commit(source, createTitleTableCommand());
    commit(target, createTitleTableCommand());
    for (const auto& title : titles) {
        commit(source, parseSQL("INSERT " + title.first));
        recordCatalogKey(title.second);
    }
    commit(catalog,
           SplitTableCommand{"title",
                             defaultRangeIdForTable("title"),
                             left_range,
                             right_range,
                             2});

    size_t source_left_before = leftRows(source).rows.size();
    commit(catalog,
           MoveRangeCommand{left_range, target.group_id});
    ControlPlaneRuntime runtime{
        [&](const Command& command) { return commit(catalog, command); }
    };
    runtime.registerReplicaGroup(
        source.group_id,
        [&](const Command& command) { return commit(source, command); }
    );
    runtime.registerReplicaGroup(
        target.group_id,
        [&](const Command& command) { return commit(target, command); }
    );
    requireStatementOk(runtime.tick().result, "prepare");
    size_t snapshot_copied = leftRows(target).rows.size();
    commit(source, parseSQL("INSERT " + catchup_title.first));
    recordCatalogKey(catchup_title.second);
    requireStatementOk(runtime.tick().result, "catch-up");
    size_t catchup_copied = leftRows(target).rows.size();
    requireStatementOk(runtime.tick().result, "commit");

    const auto* catalog_leader = leaderNode(catalog);
    Result route_result = catalog_leader->executeReadOnlyForTest(
        ExplainRouteCommand{"title", primary_key, false});
    const auto* route = std::get_if<RouteResult>(&route_result);
    Result config_result =
        catalog_leader->executeReadOnlyForTest(QueryRangeConfigCommand{-1});
    const auto* config = std::get_if<RangeConfigResult>(&config_result);

    SelectAllResult target_row =
        std::get<SelectAllResult>(leaderNode(target)->executeReadOnlyForTest(
            SelectWhereCommand{"title", "id", primary_key}));

    std::cout << "  catalog_leader=" << catalog.leader
              << ", source_leader=" << source.leader
              << ", target_leader=" << target.leader
              << ", split_key=" << split_key
              << ", moved_key=" << primary_key << std::endl;
    if (config != nullptr) {
        std::cout << "  config=" << config->config_num
                  << ", ranges=" << config->range_ids.size()
                  << ", groups=[";
        for (size_t i = 0; i < config->replica_group_ids.size(); ++i) {
            if (i != 0) std::cout << ", ";
            std::cout << config->replica_group_ids[i];
        }
        std::cout << "]" << std::endl;
    }
    if (route != nullptr && !route->range_ids.empty()) {
        std::cout << "  row key=" << route->start_key
                  << " -> range=" << route->range_ids.front()
                  << ", replica_group="
                  << route->replica_group_ids.front()
                  << ", descriptor_version="
                  << route->descriptor_versions.front() << std::endl;
    }
    std::cout << "  copied snapshot rows=" << snapshot_copied
              << ", copied after catch-up=" << catchup_copied
              << ", source_left_before=" << source_left_before
              << ", source_left_after=" << leftRows(source).rows.size()
              << ", target_left_after=" << leftRows(target).rows.size()
              << ", target_has_catchup_row="
              << (target_row.rows.empty() ? "no" : "yes") << std::endl;
    std::cout << "  catalog rows: __ranges="
              << catalogRowCountOn(catalog_leader, "__ranges")
              << ", __global_keys="
              << catalogRowCountOn(catalog_leader, "__global_keys")
              << ", __range_transfers="
              << catalogRowCountOn(catalog_leader, "__range_transfers")
              << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--measure-restart") {
        std::cerr << "--measure-restart is not available in v133; "
                  << "this version focuses on range movement and "
                  << "ownership handoff."
                  << std::endl;
        return 2;
    }

    bool tests_only = argc > 1 && std::string(argv[1]) == "--tests-only";
    int imdb_arg = tests_only ? 2 : 1;
    std::cout << "BuzzDB v133: range movement with snapshot catch-up" << std::endl;
    const std::string imdb_file =
        argc > imdb_arg ? argv[imdb_arg] : defaultImdbInputFile();
    if (!tests_only) {
        printLocalBuzzDBTrace(imdb_file);
        printPartitionedSQLTrace(imdb_file);
        printV104BootstrapTrace(imdb_file);
    }

    TestRunner tests;

    struct TitleRowFixture {
        std::string line;
        std::vector<std::string> values;
    };

    struct TitleTransferFixture {
        std::vector<TitleRowFixture> initial_rows;
        TitleRowFixture catchup_row;
        std::string split_key;
    };

    auto titleLeftRangeId = []() {
        return defaultRangeIdForTable("title") + "-left";
    };

    auto titleRightRangeId = []() {
        return defaultRangeIdForTable("title") + "-right";
    };

    auto readTitleRows = [&]() {
        std::vector<std::string> lines =
            requireTupleLinesFromFile(imdb_file, "title", 16);
        std::vector<TitleRowFixture> titles;
        for (const auto& line : lines) {
            titles.push_back({line, tupleValuesFromLine(line, "title")});
        }
        return titles;
    };

    auto makeTitleTransferFixture =
        [&](const std::vector<TitleRowFixture>& titles) {
        if (titles.size() < 9) {
            throw std::runtime_error(
                "Need enough title rows for range transfer tests.");
        }
        TitleTransferFixture fixture;
        fixture.initial_rows.assign(titles.begin(), titles.begin() + 8);
        std::vector<TitleRowFixture> sorted_initial = fixture.initial_rows;
        std::sort(
            sorted_initial.begin(),
            sorted_initial.end(),
            [](const auto& lhs, const auto& rhs) {
                return tableRowKey("title", lhs.values.front()) <
                       tableRowKey("title", rhs.values.front());
            });
        if (sorted_initial.front().values.empty() ||
            sorted_initial.back().values.empty()) {
            throw std::runtime_error(
                "Need valid title primary keys for split tests.");
        }
        size_t split_index = sorted_initial.size() / 2;
        if (split_index == 0 || split_index >= sorted_initial.size()) {
            throw std::runtime_error("Invalid title split index.");
        }
        fixture.split_key =
            tableRowKey("title", sorted_initial[split_index].values.front());

        std::set<std::string> initial_keys;
        for (const auto& row : fixture.initial_rows) {
            initial_keys.insert(row.values.front());
        }
        for (size_t i = 8; i < titles.size(); ++i) {
            if (titles[i].values.empty() ||
                initial_keys.count(titles[i].values.front()) != 0) {
                continue;
            }
            std::string row_key =
                tableRowKey("title", titles[i].values.front());
            if (row_key < fixture.split_key) {
                fixture.catchup_row = titles[i];
                return fixture;
            }
        }
        throw std::runtime_error(
            "Need an extra IMDB title row in the left split range.");
    };

    auto titleTransferFixture = [&]() {
        return makeTitleTransferFixture(readTitleRows());
    };

    auto splitTitleCommand = [&](int version = 2,
                                 const std::string& left_group = "",
                                 const std::string& right_group = "") {
        return SplitTableCommand{
            "title",
            defaultRangeIdForTable("title"),
            titleLeftRangeId(),
            titleRightRangeId(),
            version,
            left_group,
            right_group};
    };

    auto prefixedReplicas = [](const std::string& prefix) {
        std::vector<Address> replicas;
        for (size_t i = 1; i <= 3; ++i) {
            replicas.emplace_back(prefix + std::to_string(i));
        }
        return replicas;
    };

    struct ConsensusGroupHarness {
        std::string group_id;
        std::vector<Address> replicas;
        Address leader;
        Address client;
        SearchState state;
        int client_id = 0;
        int next_request_id = 1;
    };

    auto makeConsensusGroup =
        [&](const std::string& group_id,
            const std::string& prefix,
            int client_id) {
        ConsensusGroupHarness group;
        group.group_id = group_id;
        group.replicas = prefixedReplicas(prefix);
        group.leader = group.replicas.front();
        group.client = Address("client-" + group_id);
        group.state = electedConsensusState(group.replicas, group.leader);
        group.client_id = client_id;
        return group;
    };

    auto clientReplyFor = [](const SearchState& state,
                             int client_id,
                             int request_id) -> std::optional<Result> {
        for (auto it = state.network().rbegin();
             it != state.network().rend(); ++it) {
            const auto* reply = std::get_if<ClientReply>(&it->message);
            if (reply != nullptr &&
                reply->client_id == client_id &&
                reply->request_id == request_id) {
                return reply->result;
            }
        }
        return std::nullopt;
    };

    auto commitCommand = [&](ConsensusGroupHarness& group,
                             const Command& command) {
        int request_id = group.next_request_id++;
        group.state = deliverConsensusCommandToQuorum(
            group.state,
            group.leader,
            group.client,
            group.client_id,
            request_id,
            command,
            {group.replicas[1], group.replicas[2]});
        auto reply =
            clientReplyFor(group.state, group.client_id, request_id);
        if (!reply.has_value()) {
            throw std::runtime_error(
                "missing client reply for " + describeCommand(command));
        }
        return *reply;
    };

    struct TransferScenario {
        TitleTransferFixture fixture;
        ConsensusGroupHarness catalog;
        ConsensusGroupHarness source_group;
        ConsensusGroupHarness target_group;
    };

    auto groupLeaderNode = [&](const ConsensusGroupHarness& group) {
        const auto* node = group.state.nodeAs<ConsensusReplica>(group.leader);
        if (node == nullptr) {
            throw std::runtime_error("missing consensus leader for " +
                                     group.group_id);
        }
        return node;
    };

    auto queryConfigOn = [&](const ConsensusGroupHarness& group,
                             const Address& replica,
                             int config_num) -> Result {
        const auto* node = group.state.nodeAs<ConsensusReplica>(replica);
        if (node == nullptr) {
            throw std::runtime_error("missing config replica");
        }
        return node->executeReadOnlyForTest(
            QueryRangeConfigCommand{config_num});
    };

    auto configOn = [&](const ConsensusGroupHarness& group,
                        int config_num = -1) {
        Result result = queryConfigOn(group, group.leader, config_num);
        const auto* config = std::get_if<RangeConfigResult>(&result);
        if (config == nullptr) {
            throw std::runtime_error("config query failed");
        }
        return *config;
    };

    auto groupForRange = [](const RangeConfigResult& config,
                            const std::string& range_id) {
        size_t limit = std::min(config.range_ids.size(),
                                config.replica_group_ids.size());
        for (size_t i = 0; i < limit; ++i) {
            if (config.range_ids[i] == range_id) {
                return config.replica_group_ids[i];
            }
        }
        return std::string{};
    };

    auto changedAssignments = [](const RangeConfigResult& lhs,
                                 const RangeConfigResult& rhs) {
        size_t changed = 0;
        for (size_t i = 0; i < lhs.range_ids.size(); ++i) {
            for (size_t j = 0; j < rhs.range_ids.size(); ++j) {
                if (lhs.range_ids[i] == rhs.range_ids[j] &&
                    lhs.replica_group_ids[i] != rhs.replica_group_ids[j]) {
                    ++changed;
                }
            }
        }
        return changed;
    };

    auto catalogTableOn = [&](const ConsensusGroupHarness& group,
                              const Address& replica,
                              const std::string& table) {
        const auto* node = group.state.nodeAs<ConsensusReplica>(replica);
        if (node == nullptr) {
            throw std::runtime_error("missing catalog replica");
        }
        Result result =
            node->executeReadOnlyForTest(ReadSystemCatalogCommand{table});
        const auto* rows = std::get_if<SelectAllResult>(&result);
        if (rows == nullptr) {
            throw std::runtime_error("missing catalog table " + table);
        }
        return *rows;
    };

    struct TransferView {
        std::string range_id;
        std::string source_group_id;
        std::string target_group_id;
        int transfer_epoch = 0;
        size_t snapshot_key_count = 0;
        size_t catchup_key_count = 0;
        int prepared_config_num = 0;
        std::string status;
    };

    auto latestTransferOn = [&](const ConsensusGroupHarness& group,
                                const std::string& range_id)
        -> std::optional<TransferView> {
        SelectAllResult rows =
            catalogTableOn(group, group.leader, "__range_transfers");
        std::optional<TransferView> latest;
        for (const auto& row : rows.rows) {
            auto row_range = selectAllValue(rows, row, "range_id");
            if (!row_range.has_value() || *row_range != range_id) {
                continue;
            }
            TransferView current{
                *row_range,
                *selectAllValue(rows, row, "source_group_id"),
                *selectAllValue(rows, row, "target_group_id"),
                std::stoi(*selectAllValue(rows, row, "transfer_epoch")),
                static_cast<size_t>(std::stoull(
                    *selectAllValue(rows, row, "snapshot_key_count"))),
                static_cast<size_t>(std::stoull(
                    *selectAllValue(rows, row, "catchup_key_count"))),
                std::stoi(*selectAllValue(rows, row,
                                          "prepared_config_num")),
                *selectAllValue(rows, row, "status")};
            if (!latest.has_value() ||
                current.transfer_epoch >= latest->transfer_epoch) {
                latest = current;
            }
        }
        return latest;
    };

    auto currentGlobalKeyCount = [&](const ConsensusGroupHarness& group,
                                     const std::string& range_id) {
        return currentGlobalKeyCountOn(groupLeaderNode(group), range_id);
    };

    auto routeForKey = [&](const TransferScenario& scenario,
                           const std::string& primary_key) {
        Result result = groupLeaderNode(scenario.catalog)->executeReadOnlyForTest(
            ExplainRouteCommand{"title", primary_key, false});
        const auto* route = std::get_if<RouteResult>(&result);
        if (route == nullptr ||
            route->range_ids.empty() ||
            route->replica_group_ids.empty()) {
            throw std::runtime_error("missing route for title id " + primary_key);
        }
        return *route;
    };

    auto routeGroupFor = [&](const TransferScenario& scenario,
                             const std::string& primary_key) {
        RouteResult route = routeForKey(scenario, primary_key);
        return route.replica_group_ids.front();
    };

    auto groupForReplicaGroup = [](TransferScenario& scenario,
                                   const std::string& group_id)
        -> ConsensusGroupHarness& {
        if (group_id == scenario.source_group.group_id) return scenario.source_group;
        if (group_id == scenario.target_group.group_id) return scenario.target_group;
        throw std::runtime_error("unknown physical range group " + group_id);
    };

    auto rangeBoundsFor = [&](const TransferScenario& scenario,
                              const std::string& range_id) {
        RangeConfigResult config = configOn(scenario.catalog);
        for (size_t i = 0; i < config.range_ids.size(); ++i) {
            if (config.range_ids[i] == range_id &&
                i < config.start_keys.size() &&
                i < config.end_keys.size()) {
                return std::pair<std::string, std::string>{
                    config.start_keys[i], config.end_keys[i]};
            }
        }
        throw std::runtime_error("missing range bounds for " + range_id);
    };

    auto transferProtocol = [&](TransferScenario& scenario,
                                const std::string& range_id,
                                int epoch) {
        auto bounds = rangeBoundsFor(scenario, range_id);
        return RangeTransferProtocol{
            [&](const Command& command) {
                return commitCommand(scenario.catalog, command);
            },
            [&](const Command& command) {
                return commitCommand(scenario.source_group, command);
            },
            [&](const Command& command) {
                return commitCommand(scenario.target_group, command);
            },
            "title",
            range_id,
            scenario.target_group.group_id,
            bounds.first,
            bounds.second,
            epoch};
    };

    auto controlPlaneRuntime = [&](TransferScenario& scenario) {
        ControlPlaneRuntime runtime{
            [&](const Command& command) {
                return commitCommand(scenario.catalog, command);
            }
        };
        runtime.registerReplicaGroup(
            scenario.source_group.group_id,
            [&](const Command& command) {
                return commitCommand(scenario.source_group, command);
            }
        );
        runtime.registerReplicaGroup(
            scenario.target_group.group_id,
            [&](const Command& command) {
                return commitCommand(scenario.target_group, command);
            }
        );
        return runtime;
    };

    auto recordCatalogKeyForRow = [&](TransferScenario& scenario,
                                      const TitleRowFixture& row) {
        const std::string& primary_key = row.values.front();
        RouteResult route = routeForKey(scenario, primary_key);
        tests.check(commitCommand(
                        scenario.catalog,
                        InsertRowCommand{
                            "__global_keys",
                            {"title",
                             primary_key,
                             tableRowKey("title", primary_key),
                             route.range_ids.front(),
                             route.replica_group_ids.front(),
                             std::to_string(route.descriptor_versions.front()),
                             "active"}}) == Result{InsertOkResult{}},
                    "catalog should record key ownership for title " +
                        primary_key);
    };

    auto selectAllTitles = [&](const ConsensusGroupHarness& group) {
        Result result = groupLeaderNode(group)->executeReadOnlyForTest(
            SelectAllCommand{"title"});
        const auto* rows = std::get_if<SelectAllResult>(&result);
        if (rows == nullptr) return SelectAllResult{};
        return *rows;
    };

    auto titleRowMatchesRange = [&](const TitleTransferFixture& fixture,
                                    const std::string& primary_key,
                                    const std::string& range_id) {
        bool left = tableRowKey("title", primary_key) < fixture.split_key;
        if (range_id == titleLeftRangeId()) return left;
        if (range_id == titleRightRangeId()) return !left;
        return false;
    };

    auto physicalRowsInRange = [&](const TransferScenario& scenario,
                                   const ConsensusGroupHarness& group,
                                   const std::string& range_id) {
        SelectAllResult all = selectAllTitles(group);
        SelectAllResult filtered{all.columns, {}};
        for (const auto& row : all.rows) {
            auto id = selectAllValue(all, row, "id");
            if (id.has_value() &&
                titleRowMatchesRange(scenario.fixture, *id, range_id)) {
                filtered.rows.push_back(row);
            }
        }
        return filtered;
    };

    auto physicalRowCountInRange = [&](const TransferScenario& scenario,
                                       const ConsensusGroupHarness& group,
                                       const std::string& range_id) {
        return physicalRowsInRange(scenario, group, range_id).rows.size();
    };

    auto selectTitleInGroup = [&](const ConsensusGroupHarness& group,
                                  const std::string& primary_key) {
        Result result = groupLeaderNode(group)->executeReadOnlyForTest(
            SelectWhereCommand{"title", "id", primary_key});
        const auto* rows = std::get_if<SelectAllResult>(&result);
        if (rows == nullptr) return SelectAllResult{};
        return *rows;
    };

    auto selectRoutedTitleById = [&](TransferScenario& scenario,
                                     const std::string& primary_key) {
        ConsensusGroupHarness& group = groupForReplicaGroup(
            scenario, routeGroupFor(scenario, primary_key));
        return selectTitleInGroup(group, primary_key);
    };

    auto insertRoutedTitle = [&](TransferScenario& scenario,
                                 const TitleRowFixture& row) {
        ConsensusGroupHarness& group = groupForReplicaGroup(
            scenario, routeGroupFor(scenario, row.values.front()));
        tests.check(commitCommand(group, parseSQL("INSERT " + row.line)) ==
                        Result{InsertOkResult{}},
                    "routed insert should write physical title row " +
                        row.values.front());
        recordCatalogKeyForRow(scenario, row);
    };

    auto updateRoutedTitleYear = [&](TransferScenario& scenario,
                                     const std::string& primary_key,
                                     const std::string& year) {
        ConsensusGroupHarness& group = groupForReplicaGroup(
            scenario, routeGroupFor(scenario, primary_key));
        return commitCommand(
            group,
            UpdateRowsCommand{"title", "production_year", year,
                              "id", primary_key});
    };

    auto prepareTransfer = [&](TransferScenario& scenario,
                               const std::string& range_id,
                               int epoch = 1) {
        auto protocol = transferProtocol(scenario, range_id, epoch);
        tests.check(protocol.prepare() == Result{StatementOkResult{}},
                    "range transfer protocol should prepare and copy snapshot");
        return physicalRowCountInRange(
            scenario, scenario.target_group, range_id);
    };

    auto catchUpTransfer = [&](TransferScenario& scenario,
                               const std::string& range_id,
                               int epoch = 1) {
        auto protocol = transferProtocol(scenario, range_id, epoch);
        tests.check(protocol.catchUp() == Result{StatementOkResult{}},
                    "range transfer protocol should catch up target copy");
        return physicalRowCountInRange(
            scenario, scenario.target_group, range_id);
    };

    auto commitTransfer = [&](TransferScenario& scenario,
                              const std::string& range_id,
                              int epoch = 1) {
        auto protocol = transferProtocol(scenario, range_id, epoch);
        return protocol.commit();
    };

    auto abortTransfer = [&](TransferScenario& scenario,
                             const std::string& range_id,
                             int epoch = 1) {
        auto protocol = transferProtocol(scenario, range_id, epoch);
        return protocol.abort();
    };

    auto buildTransferScenario = [&]() {
        TransferScenario scenario;
        scenario.fixture = titleTransferFixture();
        scenario.catalog =
            makeConsensusGroup("group-catalog", "catalog", 1);
        scenario.source_group =
            makeConsensusGroup("group-1", "source", 2);
        scenario.target_group =
            makeConsensusGroup("group-2", "target", 3);

        tests.check(commitCommand(scenario.catalog,
                                  bootstrapClusterACommand()) ==
                        Result{StatementOkResult{}},
                    "catalog group should bootstrap the cluster");
        tests.check(commitCommand(scenario.catalog,
                                  registerGroup1Command()) ==
                        Result{StatementOkResult{}},
                    "catalog group should register the source group");
        tests.check(commitCommand(
                        scenario.catalog,
                        RegisterReplicaGroupCommand{
                            scenario.target_group.group_id,
                            {"target1", "target2", "target3"}, 1}) ==
                        Result{StatementOkResult{}},
                    "catalog should register a target group for transfer");
        tests.check(commitCommand(scenario.catalog,
                                  createTitleTableCommand()) ==
                        Result{CreateTableOkResult{}},
                    "catalog should create only title metadata");
        tests.check(commitCommand(scenario.source_group,
                                  createTitleTableCommand()) ==
                        Result{CreateTableOkResult{}},
                    "source group should create physical title storage");
        tests.check(commitCommand(scenario.target_group,
                                  createTitleTableCommand()) ==
                        Result{CreateTableOkResult{}},
                    "target group should create physical title storage");

        for (const auto& row : scenario.fixture.initial_rows) {
            tests.check(commitCommand(scenario.source_group,
                                      parseSQL("INSERT " + row.line)) ==
                            Result{InsertOkResult{}},
                        "source group should load real IMDB title row " +
                            row.values.front());
            recordCatalogKeyForRow(scenario, row);
        }
        tests.check(
            commitCommand(
                scenario.catalog,
                splitTitleCommand(2)) ==
                Result{StatementOkResult{}},
            "catalog should split title metadata while source keeps rows");
        tests.check(physicalRowCountInRange(
                        scenario, scenario.source_group, titleLeftRangeId()) > 0 &&
                        physicalRowCountInRange(
                            scenario, scenario.target_group,
                            titleLeftRangeId()) == 0,
                    "setup should have source-owned physical rows and empty target");
        return scenario;
    };

    auto titleRowInRange = [&](const TitleTransferFixture& fixture,
                               const std::string& range_id) {
        for (const auto& row : fixture.initial_rows) {
            if (row.values.empty()) continue;
            if (titleRowMatchesRange(fixture, row.values.front(), range_id)) {
                return row;
            }
        }
        throw std::runtime_error("missing title row in range " + range_id);
    };

    tests.test("Move request records planned transfer without ownership switch", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        TitleRowFixture moved_row =
            titleRowInRange(scenario.fixture, titleLeftRangeId());
        size_t left_keys =
            currentGlobalKeyCount(scenario.catalog, titleLeftRangeId());
        size_t source_left = physicalRowCountInRange(
            scenario, scenario.source_group, titleLeftRangeId());

        tests.check(commitCommand(
                        scenario.catalog,
                        MoveRangeCommand{titleLeftRangeId(),
                                         scenario.target_group.group_id}) ==
                        Result{StatementOkResult{}},
                    "move should record a transfer request");
        auto requested =
            latestTransferOn(scenario.catalog, titleLeftRangeId());
        tests.check(requested.has_value() &&
                        requested->source_group_id ==
                            scenario.source_group.group_id &&
                        requested->target_group_id ==
                            scenario.target_group.group_id &&
                        requested->transfer_epoch == 1 &&
                        requested->snapshot_key_count == 0 &&
                        requested->catchup_key_count == 0 &&
                        requested->prepared_config_num == before.config_num &&
                        requested->status == "requested",
                    "move should create a request but not a snapshot");
        tests.check(configOn(scenario.catalog) == before &&
                        routeGroupFor(scenario,
                                      moved_row.values.front()) ==
                            scenario.source_group.group_id,
                    "requested move should not switch active routing");
        tests.check(left_keys == source_left &&
                        physicalRowCountInRange(
                            scenario, scenario.target_group,
                            titleLeftRangeId()) == 0,
                    "requested move should not copy or remove physical rows");
    });

    tests.test("Control plane runtime drains requested move through protocol phases", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        size_t moved_keys =
            currentGlobalKeyCount(scenario.catalog, titleLeftRangeId());
        tests.check(commitCommand(
                        scenario.catalog,
                        MoveRangeCommand{titleLeftRangeId(),
                                         scenario.target_group.group_id}) ==
                        Result{StatementOkResult{}},
                    "move should enqueue a transfer request for the runtime");

        ControlPlaneRuntime runtime = controlPlaneRuntime(scenario);
        tests.check(runtime.runUntilIdle() == 3,
                    "control plane runtime should prepare, catch up, and commit the request");
        RangeConfigResult after = configOn(scenario.catalog);
        tests.check(after.config_num == before.config_num + 1 &&
                        groupForRange(after, titleLeftRangeId()) ==
                            scenario.target_group.group_id &&
                        physicalRowCountInRange(
                            scenario, scenario.source_group,
                            titleLeftRangeId()) == 0 &&
                        physicalRowCountInRange(
                            scenario, scenario.target_group,
                            titleLeftRangeId()) == moved_keys,
                    "runtime should leave one physical owner after commit");
    });

    tests.test("Prepare transfer copies snapshot without ownership switch", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        size_t left_keys =
            currentGlobalKeyCount(scenario.catalog, titleLeftRangeId());
        tests.check(groupForRange(before, titleLeftRangeId()) ==
                        scenario.source_group.group_id &&
                        groupForRange(before, titleRightRangeId()) ==
                            scenario.source_group.group_id,
                    "setup should keep active ranges on the source group");

        size_t copied = prepareTransfer(scenario, titleLeftRangeId(), 1);
        auto transfer =
            latestTransferOn(scenario.catalog, titleLeftRangeId());
        tests.check(transfer.has_value() &&
                        transfer->source_group_id ==
                            scenario.source_group.group_id &&
                        transfer->target_group_id ==
                            scenario.target_group.group_id &&
                        transfer->snapshot_key_count == left_keys &&
                        transfer->catchup_key_count == 0 &&
                        transfer->status == "prepared",
                    "prepare should record a real snapshot manifest");
        tests.check(configOn(scenario.catalog) == before,
                    "prepare should not switch ownership");
        tests.check(copied == left_keys &&
                        physicalRowCountInRange(
                            scenario, scenario.target_group,
                            titleLeftRangeId()) == left_keys &&
                        physicalRowCountInRange(
                            scenario, scenario.source_group,
                            titleLeftRangeId()) == left_keys,
                    "prepare should copy snapshot rows but keep source active");
    });

    tests.test("Catch-up copies post-snapshot IMDB insert", [&] {
        auto scenario = buildTransferScenario();
        prepareTransfer(scenario, titleLeftRangeId(), 1);
        auto prepared =
            latestTransferOn(scenario.catalog, titleLeftRangeId());
        tests.check(prepared.has_value(),
                    "prepared transfer should be visible");

        insertRoutedTitle(scenario, scenario.fixture.catchup_row);
        tests.check(routeGroupFor(
                        scenario,
                        scenario.fixture.catchup_row.values.front()) ==
                        scenario.source_group.group_id,
                    "new row should still route to source before commit");
        tests.check(selectTitleInGroup(
                        scenario.target_group,
                        scenario.fixture.catchup_row.values.front()).rows.empty(),
                    "target should not see live suffix before catch-up copy");
        catchUpTransfer(scenario, titleLeftRangeId(), 1);
        auto caught_up =
            latestTransferOn(scenario.catalog, titleLeftRangeId());
        tests.check(caught_up.has_value() &&
                        caught_up->status == "caught_up" &&
                        caught_up->snapshot_key_count ==
                            prepared->snapshot_key_count &&
                        caught_up->catchup_key_count == 1,
                    "catch-up should record the suffix after snapshot");
        tests.check(selectTitleInGroup(
                        scenario.target_group,
                        scenario.fixture.catchup_row.values.front()).rows.size() == 1,
                    "catch-up should copy the live row to the target group");
    });

    tests.test("Commit requires catch-up and moves physical rows", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        prepareTransfer(scenario, titleLeftRangeId(), 1);
        tests.check(commitCommand(
                        scenario.catalog,
                        CommitRangeTransferCommand{
                            titleLeftRangeId(), 1}) ==
                        Result{ConfigRejectedResult{}},
                    "commit before catch-up should fail");
        tests.check(configOn(scenario.catalog) == before,
                    "failed commit should not mutate ownership");

        insertRoutedTitle(scenario, scenario.fixture.catchup_row);
        catchUpTransfer(scenario, titleLeftRangeId(), 1);
        size_t moved_keys =
            currentGlobalKeyCount(scenario.catalog, titleLeftRangeId());
        tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{StatementOkResult{}},
                    "commit should return OK after catch-up");
        RangeConfigResult after = configOn(scenario.catalog);
        tests.check(after.config_num == before.config_num + 1 &&
                        changedAssignments(before, after) == 1 &&
                        groupForRange(after, titleLeftRangeId()) ==
                            scenario.target_group.group_id,
                    "commit should switch only the moved range");
        tests.check(routeGroupFor(
                        scenario,
                        scenario.fixture.catchup_row.values.front()) ==
                        scenario.target_group.group_id,
                    "moved row should route to target after commit");
        tests.check(physicalRowCountInRange(
                        scenario, scenario.target_group,
                        titleLeftRangeId()) == moved_keys &&
                        physicalRowCountInRange(
                            scenario, scenario.source_group,
                            titleLeftRangeId()) == 0,
                    "commit should leave one physical owner for moved rows");
    });

    tests.test("Live routed write remains visible across range transfer", [&] {
        auto scenario = buildTransferScenario();
        const std::string moved_id =
            scenario.fixture.catchup_row.values.front();
        const std::string original_year =
            scenario.fixture.catchup_row.values.at(3);
        const std::string updated_year =
            replacementProductionYear(original_year);

        prepareTransfer(scenario, titleLeftRangeId(), 1);
        insertRoutedTitle(scenario, scenario.fixture.catchup_row);
        tests.check(routeGroupFor(scenario, moved_id) ==
                        scenario.source_group.group_id,
                    "prepared range should still route live writes to source");

        SelectAllResult before_commit =
            selectTitleInGroup(scenario.source_group, moved_id);
        tests.check(before_commit.rows.size() == 1 &&
                        selectAllValue(before_commit,
                                       before_commit.rows.front(),
                                       "production_year") == original_year,
                    "source should expose the live row before ownership switch");

        catchUpTransfer(scenario, titleLeftRangeId(), 1);
        auto caught_up =
            latestTransferOn(scenario.catalog, titleLeftRangeId());
        tests.check(caught_up.has_value() &&
                        caught_up->catchup_key_count == 1 &&
                        caught_up->status == "caught_up",
                    "catch-up manifest should account for one live suffix row");
        tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{StatementOkResult{}},
                    "commit should switch ownership after catch-up");
        tests.check(routeGroupFor(scenario, moved_id) ==
                        scenario.target_group.group_id,
                    "committed transfer should route the row to target");

        SelectAllResult after_commit = selectRoutedTitleById(scenario, moved_id);
        tests.check(after_commit.rows.size() == 1 &&
                        selectAllValue(after_commit,
                                       after_commit.rows.front(),
                                       "production_year") == original_year,
                    "target route should preserve the live row contents");
        tests.check(updateRoutedTitleYear(scenario, moved_id, updated_year) ==
                        Result{UpdateRowsResult{1}},
                    "post-transfer update should use the new owner route");
        SelectAllResult after_update = selectRoutedTitleById(scenario, moved_id);
        tests.check(after_update.rows.size() == 1 &&
                        selectAllValue(after_update,
                                       after_update.rows.front(),
                                       "production_year") == updated_year,
                    "post-transfer update should remain visible on target");
    });

    tests.test("Range transfer preserves before catch-up and after commit writes", [&] {
        auto scenario = buildTransferScenario();
        TitleRowFixture existing =
            titleRowInRange(scenario.fixture, titleLeftRangeId());
        const std::string existing_id = existing.values.front();
        const std::string first_year =
            replacementProductionYear(existing.values.at(3));
        const std::string second_year =
            replacementProductionYear(first_year);
        const std::string catchup_id =
            scenario.fixture.catchup_row.values.front();

        tests.check(routeGroupFor(scenario, existing_id) ==
                        scenario.source_group.group_id,
                    "setup should route existing row to source");
        tests.check(updateRoutedTitleYear(scenario, existing_id, first_year) ==
                        Result{UpdateRowsResult{1}},
                    "pre-transfer update should commit on source");
        prepareTransfer(scenario, titleLeftRangeId(), 1);
        insertRoutedTitle(scenario, scenario.fixture.catchup_row);
        tests.check(routeGroupFor(scenario, catchup_id) ==
                        scenario.source_group.group_id,
                    "catch-up row should not route to target before commit");
        catchUpTransfer(scenario, titleLeftRangeId(), 1);
        tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{StatementOkResult{}},
                    "commit should switch ownership after catch-up");

        tests.check(routeGroupFor(scenario, existing_id) ==
                        scenario.target_group.group_id &&
                        routeGroupFor(scenario, catchup_id) ==
                            scenario.target_group.group_id,
                    "both snapshot and catch-up rows should route to target");
        SelectAllResult existing_after_commit =
            selectRoutedTitleById(scenario, existing_id);
        SelectAllResult catchup_after_commit =
            selectRoutedTitleById(scenario, catchup_id);
        tests.check(existing_after_commit.rows.size() == 1 &&
                        selectAllValue(existing_after_commit,
                                       existing_after_commit.rows.front(),
                                       "production_year") == first_year &&
                        catchup_after_commit.rows.size() == 1,
                    "target route should expose snapshot and suffix rows");

        tests.check(updateRoutedTitleYear(scenario, existing_id, second_year) ==
                        Result{UpdateRowsResult{1}},
                    "post-transfer update should use target ownership");
        SelectAllResult after_second_update =
            selectRoutedTitleById(scenario, existing_id);
        tests.check(after_second_update.rows.size() == 1 &&
                        selectAllValue(after_second_update,
                                       after_second_update.rows.front(),
                                       "production_year") == second_year,
                    "post-transfer write should remain visible on target");
    });

    tests.test("Duplicate transfer messages do not change ownership twice", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        prepareTransfer(scenario, titleLeftRangeId(), 1);
        tests.check(commitCommand(
                        scenario.catalog,
                        PrepareRangeTransferCommand{
                            titleLeftRangeId(), scenario.target_group.group_id, 1}) ==
                        Result{ConfigRejectedResult{}},
                    "duplicate prepare at same epoch should be stale");
        tests.check(configOn(scenario.catalog) == before,
                    "duplicate prepare should not switch ownership");

        catchUpTransfer(scenario, titleLeftRangeId(), 1);
        size_t target_rows_after_catchup = physicalRowCountInRange(
            scenario, scenario.target_group, titleLeftRangeId());
        tests.check(commitCommand(
                        scenario.catalog,
                        CatchUpRangeTransferCommand{
                            titleLeftRangeId(), 1}) ==
                        Result{ConfigRejectedResult{}},
                    "duplicate catch-up should not append a second suffix");
        tests.check(physicalRowCountInRange(
                        scenario, scenario.target_group,
                        titleLeftRangeId()) == target_rows_after_catchup,
                    "duplicate catch-up should not duplicate target rows");
        tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{StatementOkResult{}},
                    "first commit should switch ownership");
        RangeConfigResult after = configOn(scenario.catalog);
        tests.check(after.config_num == before.config_num + 1 &&
                        changedAssignments(before, after) == 1 &&
                        groupForRange(after, titleLeftRangeId()) ==
                            scenario.target_group.group_id,
                    "commit should change ownership exactly once");
        tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{ConfigRejectedResult{}},
                    "duplicate commit should be rejected after ownership switch");
        tests.check(configOn(scenario.catalog) == after,
                    "duplicate commit should not create another config");
    });

    tests.test("Abort cleans target copy and rejects stale transfer epochs", [&] {
        auto scenario = buildTransferScenario();
        RangeConfigResult before = configOn(scenario.catalog);
        prepareTransfer(scenario, titleLeftRangeId(), 1);
        tests.check(physicalRowCountInRange(
                        scenario, scenario.target_group,
                        titleLeftRangeId()) > 0,
                    "prepared transfer should have copied rows to target");
        tests.check(abortTransfer(scenario, titleLeftRangeId(), 1) ==
                        Result{StatementOkResult{}},
                    "abort should return OK");
        tests.check(commitCommand(
                        scenario.catalog,
                        CommitRangeTransferCommand{
                            titleLeftRangeId(), 1}) ==
                        Result{ConfigRejectedResult{}},
                    "aborted transfer should not commit");
        tests.check(commitCommand(
                        scenario.catalog,
                        PrepareRangeTransferCommand{
                            titleLeftRangeId(), scenario.target_group.group_id, 1}) ==
                        Result{ConfigRejectedResult{}},
                    "same transfer epoch should be stale");
        tests.check(configOn(scenario.catalog) == before,
                    "abort and stale epoch should leave config unchanged");
        tests.check(physicalRowCountInRange(
                        scenario, scenario.source_group,
                        titleLeftRangeId()) > 0 &&
                        physicalRowCountInRange(
                            scenario, scenario.target_group,
                            titleLeftRangeId()) == 0,
                    "abort should leave source rows and clean target rows");
    });

    tests.test("Transfer protocol is deterministic and replicated", [&] {
        auto first = buildTransferScenario();
        auto second = buildTransferScenario();
        auto runTransfer = [&](TransferScenario& scenario) {
            prepareTransfer(scenario, titleLeftRangeId(), 1);
            insertRoutedTitle(scenario, scenario.fixture.catchup_row);
            catchUpTransfer(scenario, titleLeftRangeId(), 1);
            tests.check(commitTransfer(scenario, titleLeftRangeId(), 1) ==
                            Result{StatementOkResult{}},
                        "commit should succeed");
        };
        runTransfer(first);
        runTransfer(second);

        RangeConfigResult expected = configOn(first.catalog);
        tests.check(configOn(second.catalog) == expected,
                    "same transfer commands should produce same config");
        SelectAllResult expected_transfers =
            catalogTableOn(first.catalog, first.catalog.leader,
                           "__range_transfers");
        tests.check(catalogTableOn(second.catalog,
                                   second.catalog.leader,
                                   "__range_transfers") ==
                        expected_transfers,
                    "same transfer commands should produce same manifests");
        tests.check(physicalRowCountInRange(
                        first, first.source_group, titleLeftRangeId()) == 0 &&
                        physicalRowCountInRange(
                            second, second.source_group,
                            titleLeftRangeId()) == 0 &&
                        physicalRowCountInRange(
                            first, first.target_group,
                            titleLeftRangeId()) ==
                            physicalRowCountInRange(
                                second, second.target_group,
                                titleLeftRangeId()),
                    "deterministic transfer should move the same physical rows");
        for (const auto& replica : first.catalog.replicas) {
            Result result = queryConfigOn(first.catalog, replica, -1);
            const auto* replica_config =
                std::get_if<RangeConfigResult>(&result);
            tests.check(replica_config != nullptr &&
                            *replica_config == expected,
                        "catalog follower should expose moved config");
            tests.check(catalogTableOn(first.catalog, replica,
                                       "__range_transfers") ==
                            expected_transfers,
                        "catalog follower should expose transfer manifest");
        }
    });

    return tests.finish();
}
