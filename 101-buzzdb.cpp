#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <iomanip>
#include <limits>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <cerrno>
#include <atomic>
#include <set>
#include <random>
#include <fcntl.h>
#include <unistd.h>

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
const std::string log_filename = "buzzdb.log";
const std::string master_record_filename = "buzzdb.master";
const std::string image_copy_filename = "buzzdb.image.dat";
const std::string image_copy_metadata_filename = "buzzdb.image.meta";

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

ImageCopyMetadata readImageCopyMetadata() {
    std::ifstream input(image_copy_metadata_filename);
    if (!input) {
        throw std::runtime_error("No database image copy metadata found.");
    }

    ImageCopyMetadata metadata;
    std::string key;
    input >> key >> metadata.master_lsn;
    if (key != "master_lsn") {
        throw std::runtime_error("Malformed image copy metadata.");
    }
    input >> key >> metadata.master_offset;
    if (key != "master_offset") {
        throw std::runtime_error("Malformed image copy metadata.");
    }
    input >> key >> metadata.image_copy_lsn;
    if (key != "image_copy_lsn") {
        throw std::runtime_error("Malformed image copy metadata.");
    }
    input >> key >> metadata.page_count;
    if (key != "page_count") {
        throw std::runtime_error("Malformed image copy metadata.");
    }
    return metadata;
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

    static void restoreImageCopyFile() {
        copyFileDurably(image_copy_filename, database_filename, "database image restore");
    }

    static void corruptDatabasePage(PageID requested_page_id = 13) {
        std::fstream file(database_filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) {
            throw std::runtime_error("Unable to open buzzdb.dat for media corruption.");
        }
        file.seekg(0, std::ios::end);
        size_t page_count = static_cast<size_t>(file.tellg()) / PAGE_SIZE;
        if (page_count == 0) {
            throw std::runtime_error("Cannot corrupt an empty buzzdb.dat.");
        }
        PageID page_id = requested_page_id < page_count
            ? requested_page_id
            : static_cast<PageID>(page_count - 1);
        std::vector<char> zeros(PAGE_SIZE, 0);
        file.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
        file.write(zeros.data(), PAGE_SIZE);
        file.flush();
        if (!file) {
            throw std::runtime_error("Unable to corrupt buzzdb.dat page.");
        }
        file.close();
        forceFileToStableStorage(database_filename, "corrupted database file");
        std::cout << "Media failure: corrupted buzzdb.dat page "
                  << page_id << std::endl;
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

void restoreImageCopyBeforeStartup() {
    auto metadata = readImageCopyMetadata();
    StorageManager::restoreImageCopyFile();
    writeDurableTextFile(
        master_record_filename,
        "checkpoint_lsn " + std::to_string(metadata.master_lsn) + "\n" +
        "checkpoint_offset " + std::to_string(metadata.master_offset) + "\n",
        "ARIES master record restored from image copy metadata"
    );
    std::cout << "Media recovery: restored fuzzy image copy with "
              << metadata.page_count << " page(s)" << std::endl;
    std::cout << "Media recovery: restored master checkpoint BEGIN LSN "
              << metadata.master_lsn
              << " offset " << metadata.master_offset
              << "; image copy was taken through LSN "
              << metadata.image_copy_lsn << std::endl;
}

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
            if(evictedPageId != INVALID_VALUE){
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

    size_t getStableStorageForces() const {
        return storage_manager.stable_storage_forces;
    }

    size_t getDeferredStableStorageRequests() const {
        return storage_manager.deferred_stable_storage_requests;
    }

    size_t getTotalDeferredStableStorageRequests() const {
        return storage_manager.total_deferred_stable_storage_requests;
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
    size_t data_pages_forced_at_commit = 0;
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
    bool forceLogUpTo(LSN lsn);
    void notePageFlushed(PageID page_id, LSN page_lsn, const std::string& tag);
    void maybeCrashAfterSteal(PageID page_id);
    int getCurrentTxnId() const { return current_txn_id; }
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

struct HistogramBucket {
    int lower = 0;
    int upper = 0;
    size_t count = 0;
};

struct PersistedColumnStats {
    TableId table_id;
    int column_id;
    size_t row_count;
    size_t page_count;
    size_t ndv;
    bool has_int_range;
    int min_int;
    int max_int;
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
        markDirty(page_id);
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
                markDirty(page_id);
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

bool insertTupleIntoTable(TableHeap& table, std::unique_ptr<Tuple> tuple) {
    auto insertIntoPage = [&](PageID page_id) {
        auto& page = table.getPage(page_id);
        if (page->addTuple(tuple->clone())) {
            table.markDirty(page_id);
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

    PageID page_id = table.allocatePage();
    auto& page = table.getPage(page_id);
    if (page->addTuple(std::move(tuple))) {
        table.markDirty(page_id);
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
                "buzzdb.log was written by an older WAL format; remove it before running v75."
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

    if (restart_undo_release_callback) {
        restart_undo_release_callback();
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
            LSN clr_lsn = appendClrRecord(
                txn_id,
                clr_prev_lsn,
                it->prev_lsn,
                it->table_id,
                it->page_id,
                it->slot_id,
                *it->before_tuple
            );
            txn_state->second.last_lsn = clr_lsn;
            active_transaction_table[txn_id] = {TxnStatus::ABORTING, clr_lsn};
            printThreadSafe(
                "  log: CLR txn " + std::to_string(txn_id) +
                " LSN " + std::to_string(clr_lsn) +
                " prevLSN " + std::to_string(clr_prev_lsn) +
                " undoNextLSN " + std::to_string(it->prev_lsn)
            );
            table.applyPhysiologicalUpdate(
                it->page_id, it->slot_id, it->before_tuple->clone(),
                true, "runtime abort undo", clr_lsn
            );
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
        " before-image record(s); locks can now be released"
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

void RecoveryManager::printPolicy() const {
    std::cout << "Recovery policy: Undo/Redo WAL + ARIES checkpoints/DPT/pageLSN + CLRs (Steal + No-Force)" << std::endl;
    std::cout << "  Steal: Yes; dirty uncommitted pages may reach disk." << std::endl;
    std::cout << "  Force: No; commit forces the log, not dirty data pages." << std::endl;
    std::cout << "  Stable storage: log and page flushes use fsync." << std::endl;
    std::cout << "  WAL: durable page flushes force the log through the page's LSN." << std::endl;
    std::cout << "  Checkpoint: fuzzy snapshots log ATT+DPT without forcing data pages." << std::endl;
    std::cout << "  Media recovery: restore fuzzy image copy, then replay WAL." << std::endl;
    std::cout << "  Analysis: master record points to the latest checkpoint begin LSN." << std::endl;
    std::cout << "  Redo: repeat history from the smallest recLSN, using pageLSN skips." << std::endl;
    std::cout << "  Undo: loser page updates write CLRs and apply physiological before-images." << std::endl;
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
    std::cout << "  Restart undo row X locks reacquired: "
              << restart_undo_locks_reacquired << std::endl;
    std::cout << "  Restart undo deadlocks resolved during early availability: "
              << restart_undo_deadlocks_resolved << std::endl;
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
    std::string label;
    enum State { RUNNING, COMMITTED, ABORTED } state = RUNNING;
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
            TxnContext{txn_id, txn_label, TxnContext::RUNNING}
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
    int txn_id;
    std::string txn_label;
    AccessType type;
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

void printTuple(const Tuple& tuple) {
    std::cout << "Tuple: [";
    for (const auto& field : tuple.fields) {
        field->print();
        std::cout << " ";
    }
    std::cout << "]";
}

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

std::string physicalJoinKindShortName(PhysicalJoinKind kind) {
    switch (kind) {
        case PhysicalJoinKind::NestedLoopJoin:
            return "NLJ";
        case PhysicalJoinKind::HashJoin:
            return "HJ";
        case PhysicalJoinKind::SortMergeJoin:
            return "SMJ";
    }
    return "HJ";
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
    Tuple emptyOutput;

public:
    explicit InsertOperator(TableHeap& tableHeap) : tableHeap(tableHeap) {}

    void setTupleToInsert(std::unique_ptr<Tuple> tuple) {
        tupleToInsert = std::move(tuple);
    }

    void open() override {}

    bool next() override {
        if (!tupleToInsert) {
            return false;
        }
        return insertTupleIntoTable(tableHeap, std::move(tupleToInsert));
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
    bool executed = false;
    size_t deletedCount = 0;
    Tuple emptyOutput;

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

std::string operatorTreeString(const QueryComponents& components) {
    std::string tree = "Scan(" + components.tableName + ")";
    for (const auto& join : components.joins) {
        tree = "HashJoin(" + tree + ", Scan(" + join.tableName + "))";
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

std::string tableLabel(const QueryComponents& components,
                       const std::string& table_name) {
    return table_name + "(" + actualTableName(components, table_name) + ")";
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

std::string joinLabel(const JoinClause& join) {
    return columnLabel(join.left) + " = " + columnLabel(join.right);
}

std::vector<std::string> writtenJoinOrder(const QueryComponents& components) {
    std::vector<std::string> table_names{components.tableName};
    for (const auto& join : components.joins) {
        table_names.push_back(join.tableName);
    }
    return table_names;
}

struct LogicalColumnExpr {
    ColumnRef column;
};

struct LogicalAggregateExpr {
    AggrFuncType function;
    LogicalColumnExpr input;
};

struct LogicalPredicate {
    enum class Kind { COLUMN_EQ_LITERAL, COLUMN_EQ_COLUMN, COLUMN_GT_LITERAL, COLUMN_LT_LITERAL };

    Kind kind;
    ColumnRef left;
    ColumnRef right;
    std::string literal;
};

struct LogicalPlanNode {
    enum class Kind { SCAN, JOIN, FILTER, PROJECT, AGGREGATE };

    Kind kind;
    std::string tableName;
    std::string actualTableName;
    std::vector<LogicalColumnExpr> projectExprs;
    std::vector<LogicalAggregateExpr> aggregateExprs;
    std::vector<LogicalPredicate> predicates;
    std::vector<std::unique_ptr<LogicalPlanNode>> inputs;
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

std::unique_ptr<LogicalPlanNode> buildLogicalPlan(const QueryComponents& components) {
    auto root = scanNode(components, components.tableName);
    for (const auto& join : components.joins) {
        auto join_node = std::make_unique<LogicalPlanNode>();
        join_node->kind = LogicalPlanNode::Kind::JOIN;
        join_node->predicates.push_back({
            LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
            join.left,
            join.right,
            ""
        });
        join_node->inputs.push_back(std::move(root));
        join_node->inputs.push_back(scanNode(components, join.tableName));
        root = std::move(join_node);
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

void printLogicalPlanNode(const LogicalPlanNode& node, size_t indent = 2) {
    auto expressions = logicalExpressionLabels(node);
    std::cout << std::string(indent, ' ') << logicalNodeName(node.kind);
    if (!expressions.empty()) {
        std::cout << "(" << joinStrings(expressions, ", ") << ")";
    }
    std::cout << std::endl;
    for (const auto& input : node.inputs) {
        printLogicalPlanNode(*input, indent + 2);
    }
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

    void rebuildExpressionIds(MemoGroup& group) {
        group.expressionIds.clear();
        for (const auto& expression : group.expressions) {
            group.expressionIds[expression.key()] = expression.id;
        }
    }

public:
    GroupId internGroup(const std::string& logical_property) {
        auto it = groupByProperty.find(logical_property);
        if (it != groupByProperty.end()) {
            return mergeGroups(it->second, it->second);
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

    GroupId mergeGroups(GroupId target_group_id, GroupId source_group_id) {
        groupMergeCount++;
        if (target_group_id == source_group_id) {
            return target_group_id;
        }

        auto& source = groupFor(source_group_id);
        auto source_expressions = source.expressions;
        for (auto expression : source_expressions) {
            addExpressionToGroup(target_group_id, std::move(expression));
        }

        for (auto& group : groups) {
            for (auto& expression : group.expressions) {
                for (auto& input : expression.inputs) {
                    if (input == source_group_id) {
                        input = target_group_id;
                    }
                }
            }
            rebuildExpressionIds(group);
        }

        auto& target = groupFor(target_group_id);
        for (const auto& winner : source.winners) {
            target.winners.emplace(winner.first, winner.second);
        }
        groupByProperty[source.logicalProperty] = target_group_id;
        if (rootGroupId == source_group_id) {
            rootGroupId = target_group_id;
        }
        return target_group_id;
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
    RuleKind kind;
    int promise;
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
    auto root_pattern = matchOpPattern("LogicalAggregate", {pickOnePattern()});
    auto scan_pattern = matchOpPattern("LogicalScan");
    auto filter_pattern = matchOpPattern("LogicalFilter", {pickOnePattern()});
    auto aggregate_pattern = matchOpPattern("LogicalAggregate", {pickOnePattern()});
    auto join_pattern = matchOpPattern(
        "LogicalEquiJoin",
        {pickOnePattern(), pickOnePattern()}
    );

    return {
        {
            "FILTER_PUSH_DOWN",
            RuleKind::Transformation,
            100,
            root_pattern,
            addRewrittenLogicalPlan
        },
        {
            "JOIN_PREDICATE_ATTACH",
            RuleKind::Transformation,
            90,
            root_pattern,
            addRewrittenLogicalPlan
        },
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

void printMemo(const Memo& memo) {
    std::cout << "\nMemo groups:" << std::endl;
    for (const auto& group : memo.allGroups()) {
        std::cout << "  G" << group.id << " " << group.logicalProperty
                  << std::endl;
        for (const auto& expression : group.expressions) {
            std::cout << "    E" << expression.id << " " << expression.op;
            if (!expression.inputs.empty()) {
                std::cout << "(";
                for (size_t i = 0; i < expression.inputs.size(); i++) {
                    if (i > 0) {
                        std::cout << ", ";
                    }
                    std::cout << "G" << expression.inputs[i];
                }
                std::cout << ")";
            }
            if (!expression.details.empty()) {
                std::cout << " [" << joinStrings(expression.details, ", ") << "]";
            }
            std::cout << std::endl;
        }
        for (const auto& winner : group.winners) {
            std::cout << "    winner[" << winner.first << "] = "
                      << winner.second.expression
                      << " cost=" << winner.second.cost << std::endl;
        }
    }
}

void printQueryGraph(const QueryComponents& components) {
    std::cout << "\nQuery graph:" << std::endl;
    std::cout << "  Nodes:" << std::endl;
    for (const auto& table_name : writtenJoinOrder(components)) {
        std::cout << "    " << tableLabel(components, table_name) << std::endl;
    }

    std::cout << "  Join edges:" << std::endl;
    for (const auto& join : components.joins) {
        std::cout << "    " << joinLabel(join) << std::endl;
    }
    for (const auto& equality : components.columnEqualities) {
        std::cout << "    "
                  << logicalPredicateLabel({
                         LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
                         equality.left,
                         equality.right,
                         ""
                     })
                  << std::endl;
    }

    std::cout << "  Selection predicates:" << std::endl;
    for (const auto& filter : components.filters) {
        std::cout << "    " << columnLabel(filter.column)
                  << " = " << filter.value << std::endl;
    }
}

void addNeededColumn(std::map<std::string, std::set<std::string>>& needed_columns,
                     const ColumnRef& column) {
    needed_columns[column.tableName].insert(
        attributeLabel(column.columnName, column.attributeIndex)
    );
}

void printNeededColumns(const QueryComponents& components) {
    std::map<std::string, std::set<std::string>> needed_columns;
    for (const auto& column : components.selectColumns) {
        addNeededColumn(needed_columns, column);
    }
    for (const auto& join : components.joins) {
        addNeededColumn(needed_columns, join.left);
        addNeededColumn(needed_columns, join.right);
    }
    for (const auto& filter : components.filters) {
        addNeededColumn(needed_columns, filter.column);
    }
    for (const auto& equality : components.columnEqualities) {
        addNeededColumn(needed_columns, equality.left);
        addNeededColumn(needed_columns, equality.right);
    }

    std::cout << "\nNeeded columns after projection pushdown:" << std::endl;
    for (const auto& table_name : writtenJoinOrder(components)) {
        auto columns_it = needed_columns.find(table_name);
        if (columns_it == needed_columns.end()) {
            continue;
        }
        std::vector<std::string> columns(
            columns_it->second.begin(),
            columns_it->second.end()
        );
        std::cout << "  " << table_name << ": "
                  << joinStrings(columns, ", ") << std::endl;
    }
}

void printLogicalExplanation(const QueryComponents& components) {
    auto logical_plan = buildLogicalPlan(components);
    auto rewritten_plan = buildRewrittenLogicalPlan(components);
    std::cout << "\nOriginal logical plan:" << std::endl;
    printLogicalPlanNode(*logical_plan);
    std::cout << "\nLogical rewrite rules represented in memo:" << std::endl;
    std::cout << "  Split conjunctive predicates" << std::endl;
    std::cout << "  Push single-table selections to scans" << std::endl;
    std::cout << "  Attach column equality predicates to joins when both sides are available" << std::endl;
    std::cout << "  Derive table-local needed columns" << std::endl;
    std::cout << "\nRepresentative rewritten alternative:" << std::endl;
    printLogicalPlanNode(*rewritten_plan);
    printQueryGraph(components);
    std::cout << "  Written join order: "
              << joinStrings(writtenJoinOrder(components), " -> ")
              << std::endl;
    printNeededColumns(components);
    std::cout << "\nDefault physical plan before costing:" << std::endl;
    std::cout << "  " << operatorTreeString(components) << " with pushed scan filters" << std::endl;
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

std::string intRangeString(const ColumnStats& stats) {
    if (!stats.hasIntRange) {
        return "";
    }
    return " min=" + std::to_string(stats.minInt) +
           " max=" + std::to_string(stats.maxInt);
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

double estimateRangeRowsUniform(const ColumnStats& stats,
                                int lower,
                                int upper,
                                double input_rows) {
    if (!stats.hasIntRange || upper < stats.minInt || lower > stats.maxInt) {
        return 1.0;
    }
    int overlap_lower = std::max(lower, stats.minInt);
    int overlap_upper = std::min(upper, stats.maxInt);
    double overlap = static_cast<double>(overlap_upper - overlap_lower + 1);
    double domain = static_cast<double>(stats.maxInt - stats.minInt + 1);
    return std::max(1.0, input_rows * overlap / domain);
}

double estimateRangeRowsFromBuckets(const std::vector<HistogramBucket>& buckets,
                                    int lower,
                                    int upper) {
    double estimate = 0.0;
    for (const auto& bucket : buckets) {
        if (upper < bucket.lower || lower > bucket.upper) {
            continue;
        }
        int overlap_lower = std::max(lower, bucket.lower);
        int overlap_upper = std::min(upper, bucket.upper);
        double overlap = static_cast<double>(overlap_upper - overlap_lower + 1);
        double width = static_cast<double>(bucket.upper - bucket.lower + 1);
        estimate += static_cast<double>(bucket.count) * overlap / width;
    }
    return std::max(1.0, estimate);
}

double qError(double estimate, double actual) {
    if (estimate <= 0.0 || actual <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return std::max(estimate / actual, actual / estimate);
}

std::string formatQError(double estimate, double actual) {
    auto error = qError(estimate, actual);
    if (!std::isfinite(error)) {
        return "inf";
    }
    return formatEstimate(error) + "x";
}

std::map<std::string, std::set<std::string>> neededColumnsByTable(
    const QueryComponents& components) {
    std::map<std::string, std::set<std::string>> needed_columns;
    for (const auto& column : components.selectColumns) {
        addNeededColumn(needed_columns, column);
    }
    for (const auto& join : components.joins) {
        addNeededColumn(needed_columns, join.left);
        addNeededColumn(needed_columns, join.right);
    }
    for (const auto& filter : components.filters) {
        addNeededColumn(needed_columns, filter.column);
    }
    for (const auto& equality : components.columnEqualities) {
        addNeededColumn(needed_columns, equality.left);
        addNeededColumn(needed_columns, equality.right);
    }
    return needed_columns;
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

size_t countFilterRows(const QueryComponents& components,
                       const FilterClause& filter,
                       Catalog& catalog,
                       BufferManager& buffer_manager) {
    auto& metadata = catalog.getTable(actualTableName(components, filter.column.tableName));
    auto filter_field = parseLiteralField(
        metadata.schema.columns[static_cast<size_t>(filter.column.attributeIndex)].type,
        filter.value
    );
    TableHeap table(metadata, buffer_manager);
    ScanOperator scan(table);
    auto predicate = SimplePredicate(
        SimplePredicate::Operand(static_cast<size_t>(filter.column.attributeIndex)),
        SimplePredicate::Operand(filter_field.clone()),
        SimplePredicate::ComparisonOperator::EQ
    );
    size_t count = 0;
    scan.open();
    while (scan.next()) {
        const auto& tuple = scan.getOutput();
        if (predicate.check(tuple)) {
            count++;
        }
    }
    scan.close();
    return count;
}

size_t countRangeRows(const QueryComponents& components,
                      const ColumnRef& column,
                      int lower,
                      int upper,
                      Catalog& catalog,
                      BufferManager& buffer_manager) {
    auto& metadata = catalog.getTable(actualTableName(components, column.tableName));
    TableHeap table(metadata, buffer_manager);
    ScanOperator scan(table);
    size_t count = 0;
    scan.open();
    while (scan.next()) {
        const auto& tuple = scan.getOutput();
        if (column.attributeIndex < 0 ||
            static_cast<size_t>(column.attributeIndex) >= tuple.fields.size()) {
            throw std::runtime_error("Range validation column is out of range.");
        }
        const Field& field = *tuple.fields[static_cast<size_t>(column.attributeIndex)];
        if (field.getType() == INT &&
            field.asInt() >= lower &&
            field.asInt() <= upper) {
            count++;
        }
    }
    scan.close();
    return count;
}

std::string topValueString(const ColumnStats& stats, size_t limit = 3) {
    std::vector<std::pair<std::string, size_t>> values(
        stats.mcvCounts.begin(),
        stats.mcvCounts.end()
    );
    std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });

    std::ostringstream out;
    for (size_t i = 0; i < values.size() && i < limit; i++) {
        if (i > 0) {
            out << ", ";
        }
        out << values[i].first << "=" << values[i].second;
    }
    return out.str();
}

size_t maxValueCount(const ColumnStats& stats) {
    size_t max_count = 0;
    for (const auto& value : stats.mcvCounts) {
        max_count = std::max(max_count, value.second);
    }
    return max_count;
}

std::string bucketSummary(const std::vector<HistogramBucket>& buckets) {
    std::ostringstream out;
    for (size_t i = 0; i < buckets.size(); i++) {
        if (i > 0) {
            out << " ";
        }
        out << "[" << buckets[i].lower << "-" << buckets[i].upper
            << ":" << buckets[i].count << "]";
    }
    return out.str();
}

void printAnalyzeStats(const QueryComponents& components,
                       const StatisticsCatalog& stats) {
    auto needed_columns = neededColumnsByTable(components);
    std::map<std::string, std::set<std::string>> filtered_columns;
    for (const auto& filter : components.filters) {
        filtered_columns[filter.column.tableName].insert(filter.column.columnName);
    }
    std::cout << "\nANALYZE stats for query tables:" << std::endl;
    std::cout << "  # ndv = number of distinct values" << std::endl;
    std::cout << "  # mcv = top " << MCV_VALUE_LIMIT
              << " most common values" << std::endl;
    for (const auto& table_name : writtenJoinOrder(components)) {
        const auto& table_stats = tableStatsFor(stats, components, table_name);
        std::cout << "  " << tableLabel(components, table_name)
                  << ": rows=" << table_stats.rowCount
                  << " pages=" << table_stats.pageCount << std::endl;
        for (const auto& column_name : needed_columns[table_name]) {
            auto column_it = table_stats.columns.find(column_name);
            if (column_it == table_stats.columns.end()) {
                continue;
            }
            std::cout << "    " << column_name
                      << " ndv=" << column_it->second.ndv
                      << intRangeString(column_it->second)
                      << std::endl;
            if (column_it->second.type == STRING &&
                column_it->second.ndv > 1 &&
                maxValueCount(column_it->second) > 1 &&
                filtered_columns[table_name].find(column_name) !=
                    filtered_columns[table_name].end()) {
                std::cout << "      mcv: "
                          << topValueString(column_it->second)
                          << std::endl;
            }
        }
    }
}

struct RangeValidation {
    ColumnRef column;
    int lower = 0;
    int upper = 0;
};

std::vector<RangeValidation> chooseRangeValidations(
    const QueryComponents& components,
    const StatisticsCatalog& stats,
    Catalog& catalog) {
    std::vector<RangeValidation> validations;
    std::vector<std::pair<std::string, std::string>> preferred_columns = {
        {"t", "id"},
        {"mc", "company_id"},
        {"mi", "info_type_id"}
    };

    for (const auto& preferred : preferred_columns) {
        const auto& table_stats = tableStatsFor(stats, components, preferred.first);
        auto column_it = table_stats.columns.find(preferred.second);
        if (column_it == table_stats.columns.end() ||
            !column_it->second.hasIntRange ||
            column_it->second.minInt == column_it->second.maxInt) {
            continue;
        }

        ColumnRef column{preferred.first, preferred.second, -1};
        resolveColumnRef(column, catalog, components);
        int span = column_it->second.maxInt - column_it->second.minInt;
        int lower = column_it->second.minInt + span / 4;
        int upper = column_it->second.minInt + span / 2;
        validations.push_back({column, lower, upper});
    }
    return validations;
}

void printHistogramRangeValidation(const QueryComponents& components,
                                   const StatisticsCatalog& stats,
                                   Catalog& catalog,
                                   BufferManager& buffer_manager) {
    auto validations = chooseRangeValidations(components, stats, catalog);
    if (validations.empty()) {
        return;
    }

    std::cout << "\nHistogram range validation:" << std::endl;
    for (const auto& validation : validations) {
        const auto& column_stats = columnStatsFor(stats, components, validation.column);
        auto width_estimate = estimateRangeRowsFromBuckets(
            column_stats.equiWidthBuckets,
            validation.lower,
            validation.upper
        );
        auto depth_estimate = estimateRangeRowsFromBuckets(
            column_stats.equiDepthBuckets,
            validation.lower,
            validation.upper
        );
        auto actual = countRangeRows(
            components,
            validation.column,
            validation.lower,
            validation.upper,
            catalog,
            buffer_manager
        );

        std::cout << "  " << columnLabel(validation.column)
                  << " BETWEEN " << validation.lower
                  << " AND " << validation.upper
                  << ": equi-width=" << formatEstimate(width_estimate)
                  << " q-error="
                  << formatQError(width_estimate, static_cast<double>(actual))
                  << " equi-depth=" << formatEstimate(depth_estimate)
                  << " q-error="
                  << formatQError(depth_estimate, static_cast<double>(actual))
                  << " actual=" << actual
                  << std::endl;
        std::cout << "    equi-width buckets: "
                  << bucketSummary(column_stats.equiWidthBuckets)
                  << std::endl;
        std::cout << "    equi-depth buckets: "
                  << bucketSummary(column_stats.equiDepthBuckets)
                  << std::endl;
    }
}

double printCardinalityEstimates(const QueryComponents& components,
                                 const StatisticsCatalog& stats) {
    std::cout << "\nEstimated cardinalities with top-k MCV filters:" << std::endl;
    std::map<std::string, double> mcv_table_rows;
    for (const auto& table_name : writtenJoinOrder(components)) {
        auto base_rows = static_cast<double>(
            tableStatsFor(stats, components, table_name).rowCount
        );
        auto mcv_rows = estimateRowsAfterTableFilters(
            stats,
            components,
            table_name
        );
        mcv_table_rows[table_name] = mcv_rows;
        std::cout << "  " << table_name << " scan/filter: "
                  << "rows=" << formatEstimate(mcv_rows)
                  << " from " << formatEstimate(base_rows)
                  << std::endl;
    }

    double mcv_rows = mcv_table_rows[components.tableName];
    std::set<std::string> seen_tables{components.tableName};
    for (const auto& join : components.joins) {
        mcv_rows = estimateJoinRows(
            mcv_rows,
            mcv_table_rows[join.tableName],
            columnStatsFor(stats, components, join.left),
            columnStatsFor(stats, components, join.right)
        );
        seen_tables.insert(join.tableName);
        std::cout << "  join " << joinLabel(join) << ": "
                  << "rows=" << formatEstimate(mcv_rows)
                  << std::endl;
    }
    for (const auto& equality : components.columnEqualities) {
        std::cout << "  predicate "
                  << logicalPredicateLabel({
                         LogicalPredicate::Kind::COLUMN_EQ_COLUMN,
                         equality.left,
                         equality.right,
                         ""
                     })
                  << ": graph edge, not an independent selectivity factor"
                  << std::endl;
    }
    std::cout << "  final join body estimate: "
              << formatEstimate(mcv_rows)
              << std::endl;
    return mcv_rows;
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
    std::string planDescription;
    size_t dpStatesKept = 0;
    size_t dpCandidatesConsidered = 0;
    size_t dpCrossProductsPruned = 0;
    std::vector<PlanSnapshot> annealingCheckpoints;

    PlannedQuery() = default;

    PlannedQuery(QueryComponents components,
                 PhysicalJoinPlan physicalPlan,
                 std::shared_ptr<JoinPlanNode> planRoot = nullptr,
                 std::string planDescription = "")
        : components(std::move(components)),
          physicalPlan(std::move(physicalPlan)),
          planRoot(std::move(planRoot)),
          planDescription(std::move(planDescription)) {}
};

PlanSnapshot makePlanSnapshot(size_t transformations,
                              const PlannedQuery& plan) {
    PlanSnapshot snapshot;
    snapshot.transformations = transformations;
    snapshot.estimatedCost = plan.physicalPlan.totalCost;
    snapshot.components = plan.components;
    snapshot.joinKinds = plan.physicalPlan.joinKinds;
    snapshot.planRoot = plan.planRoot;
    snapshot.order = plan.planDescription.empty()
        ? joinStrings(writtenJoinOrder(plan.components), " -> ")
        : plan.planDescription;
    return snapshot;
}

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
    std::vector<JoinClause> edges = components.joins;
    for (const auto& equality : components.columnEqualities) {
        edges.push_back({"", "", equality.left, equality.right});
    }
    return edges;
}

std::optional<JoinClause> orientJoinEdge(const JoinClause& edge,
                                         const std::set<std::string>& joined_tables,
                                         const QueryComponents& components) {
    bool left_joined = joined_tables.find(edge.left.tableName) != joined_tables.end();
    bool right_joined = joined_tables.find(edge.right.tableName) != joined_tables.end();
    if (left_joined == right_joined) {
        return std::nullopt;
    }

    JoinClause oriented = edge;
    oriented.tableName = left_joined ? edge.right.tableName : edge.left.tableName;
    oriented.actualTableName = actualTableName(components, oriented.tableName);
    return oriented;
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

std::string memoPhysicalJoinName(PhysicalJoinKind kind) {
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

std::string memoPhysicalPlanExpression(const std::shared_ptr<JoinPlanNode>& node) {
    if (!node) {
        return "";
    }
    if (node->isLeaf) {
        return "Scan(" + node->tableName + ")";
    }
    auto left = memoPhysicalPlanExpression(node->left);
    auto right = memoPhysicalPlanExpression(node->right);
    if (node->joinKind == PhysicalJoinKind::SortMergeJoin) {
        left = "SORT(" + left + ")";
        right = "SORT(" + right + ")";
    }
    return memoPhysicalJoinName(node->joinKind) + "(" + left + ", " + right + ")";
}

std::string joinPlanTableGroup(const std::shared_ptr<JoinPlanNode>& node) {
    if (!node) {
        return "{}";
    }
    if (node->isLeaf) {
        return node->tableName;
    }
    return "{" + joinStrings(node->tables, ",") + "}";
}

void appendJoinPlanTree(const std::shared_ptr<JoinPlanNode>& node,
                        const std::string& base_indent,
                        size_t depth,
                        std::ostringstream& out) {
    if (!node) {
        return;
    }
    out << base_indent << std::string(depth * 2, ' ');
    if (node->isLeaf) {
        out << node->tableName << "\n";
        return;
    }

    out << physicalJoinKindShortName(node->joinKind) << " "
        << joinPlanTableGroup(node->left) << " ⋈ "
        << joinPlanTableGroup(node->right) << "\n";
    if (node->left && !node->left->isLeaf) {
        appendJoinPlanTree(node->left, base_indent, depth + 1, out);
    }
    if (node->right && !node->right->isLeaf) {
        appendJoinPlanTree(node->right, base_indent, depth + 1, out);
    }
}

std::string prettyJoinPlanTree(const std::shared_ptr<JoinPlanNode>& node,
                               const std::string& indent = "") {
    std::ostringstream out;
    appendJoinPlanTree(node, indent, 0, out);
    return out.str();
}

size_t bitCount(uint64_t mask) {
    size_t count = 0;
    while (mask != 0) {
        count += mask & 1ULL;
        mask >>= 1ULL;
    }
    return count;
}

std::set<std::string> tablesForMask(const std::vector<std::string>& table_names,
                                    uint64_t mask) {
    std::set<std::string> tables;
    for (size_t i = 0; i < table_names.size(); i++) {
        if ((mask & (1ULL << i)) != 0) {
            tables.insert(table_names[i]);
        }
    }
    return tables;
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
    PhysicalJoinPlan physicalPlan;
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
        auto logical_plan = buildLogicalPlan(components);
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

std::optional<MemoPlanChoice> chooseMemoGroupPlan(
    const Memo& memo,
    const QueryComponents& components,
    const StatisticsCatalog& stats,
    const OptimizerAlgebra& algebra,
    GroupId group_id,
    const PhysicalPropertySet& required_property,
    const std::vector<JoinClause>& edges,
    std::map<std::pair<GroupId, std::string>, MemoPlanChoice>& winners,
    std::set<std::pair<GroupId, std::string>>& active_groups) {
    auto required_property_name = required_property.describe();
    auto cache_key = std::make_pair(group_id, required_property_name);
    auto cached = winners.find(cache_key);
    if (cached != winners.end()) {
        return cached->second;
    }
    if (active_groups.find(cache_key) != active_groups.end()) {
        return std::nullopt;
    }
    active_groups.insert(cache_key);

    const auto& group = memo.allGroups()[group_id - 1];
    std::optional<MemoPlanChoice> best;
    auto unordered_property_value = unorderedProperty();

    for (const auto& expression : group.expressions) {
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
            candidate = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                required_property,
                edges,
                winners,
                active_groups
            );
        } else if (auto join_kind = memoImplementationJoinKind(expression.op);
                   join_kind && expression.inputs.size() == 2) {
            auto left = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[0],
                unordered_property_value,
                edges,
                winners,
                active_groups
            );
            auto right = chooseMemoGroupPlan(
                memo,
                components,
                stats,
                algebra,
                expression.inputs[1],
                unordered_property_value,
                edges,
                winners,
                active_groups
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

                        left = chooseMemoGroupPlan(
                            memo,
                            components,
                            stats,
                            algebra,
                            expression.inputs[0],
                            left_required,
                            edges,
                            winners,
                            active_groups
                        );
                        right = chooseMemoGroupPlan(
                            memo,
                            components,
                            stats,
                            algebra,
                            expression.inputs[1],
                            right_required,
                            edges,
                            winners,
                            active_groups
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
            candidate = addSortEnforcer(*candidate, required_property);
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
    const PlanTraitSet& required_traits) {
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
        active_groups
    );
    if (!final_choice) {
        throw std::runtime_error("Memo winner search found no executable expression.");
    }
    PlannedQuery planned_query{
        components,
        final_choice->physicalPlan,
        final_choice->planRoot,
        joinPlanTreeString(final_choice->planRoot)
    };
    MemoWinnerSearch winner;
    winner.finalGroupId = memo.finalGroupId();
    winner.requiredTrait = required_traits.describe();
    winner.deliveredTrait = planned_query.physicalPlan.deliveredProperty;
    winner.expression = planned_query.planRoot
        ? memoPhysicalPlanExpression(planned_query.planRoot)
        : "Scan(" + planned_query.components.tableName + ")";
    if (!planned_query.physicalPlan.finalSortColumns.empty()) {
        winner.expression = "Sort(" + winner.expression + ")";
    }
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

class SearchStrategy {
public:
    virtual ~SearchStrategy() = default;

    virtual std::string name() const = 0;
    virtual MemoWinnerSearch chooseWinner(
        Memo& memo,
        const QueryComponents& components,
        const StatisticsCatalog& stats,
        const OptimizerAlgebra& algebra,
        const PlanTraitSet& required_traits) const = 0;
};

class MemoWinnerSearchStrategy : public SearchStrategy {
public:
    std::string name() const override {
        return "memo winner search";
    }

    MemoWinnerSearch chooseWinner(
        Memo& memo,
        const QueryComponents& components,
        const StatisticsCatalog& stats,
        const OptimizerAlgebra& algebra,
        const PlanTraitSet& required_traits) const override {
        return chooseMemoWinner(
            memo,
            components,
            stats,
            algebra,
            required_traits
        );
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
    std::string searchStrategyName;
    std::vector<std::string> logicalOperators;
    std::vector<std::string> physicalAlgorithms;
    std::vector<std::string> searchSpaceRules;
};

class Optimizer {
    const StatisticsCatalog& stats;
    PlanTraitSet requiredTraits;
    BuzzDBOptimizerAlgebra algebra;
    MemoRewriteSearchSpace searchSpace;
    MemoWinnerSearchStrategy searchStrategy;

public:
    Optimizer(const StatisticsCatalog& stats,
              PlanTraitSet requiredTraits = {})
        : stats(stats),
          requiredTraits(requiredTraits) {}

    OptimizerResult optimize(const QueryComponents& components) const {
        auto memo = algebra.buildInitialMemo(components);
        MemoTransformationStats memo_stats;
        searchSpace.expand(memo, components, memo_stats);
        auto memo_winner = searchStrategy.chooseWinner(
            memo,
            components,
            stats,
            algebra,
            requiredTraits
        );

        return {
            components,
            memo,
            memo_stats,
            memo_winner.plannedQuery,
            requiredTraits,
            memo_winner,
            algebra.name(),
            searchSpace.name(),
            searchStrategy.name(),
            algebra.logicalOperators(),
            algebra.physicalAlgorithms(),
            searchSpace.rules()
        };
    }
};

void printOptimizerSummary(const OptimizerResult& result) {
    std::cout << "\nOptimizer boundary:" << std::endl;
    std::cout << "  framework: optd-style memo with physical properties and enforcers" << std::endl;
    std::cout << "  algebra: " << result.algebraName << std::endl;
    std::cout << "    logical operators: "
              << joinStrings(result.logicalOperators, ", ") << std::endl;
    std::cout << "    physical algorithms: "
              << joinStrings(result.physicalAlgorithms, ", ") << std::endl;
    std::cout << "  search space: " << result.searchSpaceName << std::endl;
    std::cout << "    rules: "
              << joinStrings(result.searchSpaceRules, ", ") << std::endl;
    std::cout << "  search strategy: "
              << result.searchStrategyName << std::endl;
    std::cout << "  required traits: "
              << result.requiredTraits.describe() << std::endl;
    std::cout << "  property model: MergeJoin requests ordered join-key inputs; Sort enforces missing order"
              << std::endl;
    std::cout << "\nMemo winner search:" << std::endl;
    std::cout << "  required trait: "
              << result.memoWinner.requiredTrait << std::endl;
    std::cout << "  final group: G"
              << result.memoWinner.finalGroupId << std::endl;
    std::cout << "  final group winner: "
              << result.memoWinner.expression << std::endl;
    std::cout << "  winner cost: "
              << formatEstimate(result.memoWinner.cost) << std::endl;
    std::cout << "  delivered trait: "
              << result.memoWinner.deliveredTrait << std::endl;
    if (result.plannedQuery.physicalPlan.sortEnforcers > 0) {
        std::cout << "  enforcer: SORT_ENFORCER adds "
                  << formatEstimate(result.plannedQuery.physicalPlan.enforcerCost)
                  << " cost";
        if (!result.plannedQuery.physicalPlan.finalSortColumns.empty()) {
            std::cout << " for "
                      << orderPropertyForColumn(
                             result.plannedQuery.physicalPlan.finalSortColumns.front()
                         );
        }
        std::cout << std::endl;
    }
    std::cout << "  executable plan: from memo winner" << std::endl;
    std::cout << "  memo rule fires: "
              << result.memoStats.firedRules.size() << std::endl;
    std::cout << "  memo before rules: "
              << result.memoStats.initialGroups << " group(s), "
              << result.memoStats.initialExpressions << " expression(s)"
              << std::endl;
    std::cout << "  memo after rules: "
              << result.memoStats.finalGroups << " group(s), "
              << result.memoStats.finalExpressions << " expression(s)"
              << std::endl;
    std::cout << "  equivalent group merges: "
              << result.memoStats.mergedGroups << std::endl;
    std::cout << "  expression hash deduplications: "
              << result.memoStats.deduplicatedExpressions << std::endl;
    std::map<std::string, size_t> rule_counts;
    for (const auto& fire : result.memoStats.firedRules) {
        rule_counts[fire.ruleName]++;
    }
    for (const auto& rule_count : rule_counts) {
        std::cout << "    " << rule_count.first << ": "
                  << rule_count.second << " expression(s)" << std::endl;
    }
    printMemo(result.memo);
}

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

void printPhysicalJoinCosts(const QueryComponents& components,
                            const PhysicalJoinPlan& plan,
                            const std::string& plan_label,
                            const std::string& plan_description = "",
                            const std::shared_ptr<JoinPlanNode>& plan_root = nullptr) {
    if (plan.steps.empty()) {
        return;
    }

    std::cout << "\nPhysical join costing for " << plan_label << ":" << std::endl;
    std::cout << "  # cost is relative operator work for this tuple-at-a-time BuzzDB executor"
              << std::endl;
    std::cout << "  # NestedLoopJoin materializes the right side once, then compares left_rows * right_rows"
              << std::endl;
    std::cout << "  # HashJoin scans/builds the right side once, then probes with the left stream"
              << std::endl;
    std::cout << "  # SortMergeJoin uses in-memory Sort operators before merge"
              << std::endl;
    if (plan_description.empty()) {
        std::cout << "  chosen join order: "
                  << joinStrings(writtenJoinOrder(components), " -> ")
                  << std::endl;
    } else if (plan_root) {
        std::cout << "  chosen join tree:" << std::endl;
        std::cout << prettyJoinPlanTree(plan_root, "    ");
    } else {
        std::cout << "  chosen join tree: "
                  << plan_description
                  << std::endl;
    }
    for (const auto& step : plan.steps) {
        std::cout << "  " << joinLabel(step.join) << std::endl;
        std::cout << "    inputs: left rows/pages="
                  << formatEstimate(step.leftRows) << "/"
                  << formatEstimate(step.leftPages)
                  << " left total="
                  << formatEstimate(step.leftTotalCost)
                  << " right rows/pages="
                  << formatEstimate(step.rightRows) << "/"
                  << formatEstimate(step.rightPages)
                  << " right access="
                  << formatEstimate(step.rightAccessCost)
                  << std::endl;
        std::cout << "    output rows/pages="
                  << formatEstimate(step.outputRows) << "/"
                  << formatEstimate(step.outputPages)
                  << std::endl;
        std::cout << "    NestedLoopJoin cost="
                  << formatEstimate(step.nestedLoopCost)
                  << " HashJoin cost="
                  << formatEstimate(step.hashJoinCost)
                  << " SortMergeJoin cost="
                  << formatEstimate(step.sortMergeCost);
        if (step.orderedOutputRequired) {
            std::cout << " (Sort inputs; no final Sort)";
        }
        std::cout << std::endl;
        std::cout << "    chosen: "
                  << physicalJoinKindName(step.chosen)
                  << std::endl;
    }
    std::cout << "  estimated plan cost: "
              << formatEstimate(plan.totalCost)
              << " final rows/pages="
              << formatEstimate(plan.finalRows) << "/"
              << formatEstimate(plan.finalPages)
              << std::endl;
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
        auto predicate = makeScanFilterPredicate(components, catalog, table_name);
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
    struct GroupCommitEntry {
        TxnPtr txn;
        LSN commit_lsn;
        bool durable = false;
        std::mutex latch;
        std::condition_variable cv;
    };

    HashIndex hash_index;
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
    std::mutex group_commit_latch;
    std::condition_variable group_commit_cv;
    std::vector<std::shared_ptr<GroupCommitEntry>> group_commit_queue;
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
        concurrency_control_policy->commit(tx->id);
        logConcurrencyControl(txnLabel(tx) + " COMMIT; release strict 2PL locks");
        return true;
    }

    void abort(const TxnPtr& tx) {
        if (recovery_manager.hasTxn(tx->id)) {
            recovery_manager.abortTxn(tx->id);
            recovery_manager.finishTxn(tx->id);
        }
        txn_manager.abort(*tx);
        concurrency_control_policy->abort(tx->id);
        logConcurrencyControl(txnLabel(tx) + " ABORT; release strict 2PL locks");
    }

    void queueGroupCommit(const TxnPtr& tx) {
        LSN commit_lsn = recovery_manager.queueCommit(tx->id);
        auto entry = std::make_shared<GroupCommitEntry>();
        entry->txn = tx;
        entry->commit_lsn = commit_lsn;
        {
            std::lock_guard<std::mutex> guard(group_commit_latch);
            group_commit_queue.push_back(entry);
        }
        group_commit_cv.notify_all();
        logConcurrencyControl(txnLabel(tx) + " COMMIT queued at LSN " +
              std::to_string(commit_lsn) +
              "; X locks stay held until the group force");
        std::unique_lock<std::mutex> wait_guard(entry->latch);
        entry->cv.wait(wait_guard, [&]() { return entry->durable; });
        logConcurrencyControl(txnLabel(tx) + " COMMIT returns after durable group force");
    }

    void waitForQueuedCommits(size_t expected_count) {
        std::unique_lock<std::mutex> guard(group_commit_latch);
        group_commit_cv.wait(guard, [&]() {
            return group_commit_queue.size() >= expected_count;
        });
    }

    void flushGroupCommit() {
        std::vector<std::shared_ptr<GroupCommitEntry>> batch;
        {
            std::lock_guard<std::mutex> guard(group_commit_latch);
            batch.swap(group_commit_queue);
        }
        if (batch.empty()) {
            return;
        }

        LSN max_lsn = 0;
        std::string labels;
        for (const auto& entry : batch) {
            max_lsn = std::max(max_lsn, entry->commit_lsn);
            if (!labels.empty()) {
                labels += ", ";
            }
            labels += txnLabel(entry->txn);
        }

        printThreadSafe("\nGROUP COMMIT");
        printThreadSafe("  queued txns: " + labels);
        printThreadSafe(
            "  force WAL through max commit LSN " + std::to_string(max_lsn)
        );
        auto force_delta = recovery_manager.forceCommitGroupUpTo(max_lsn);
        printThreadSafe(
            "  log fsyncs this batch: " + std::to_string(force_delta)
        );
        printThreadSafe(
            "  durable through LSN " +
            std::to_string(recovery_manager.getFlushedLSN())
        );

        for (const auto& entry : batch) {
            txn_manager.commit(*entry->txn);
            concurrency_control_policy->commit(entry->txn->id);
            logConcurrencyControl(txnLabel(entry->txn) +
                  " locks released after durable COMMIT");
            recovery_manager.finishTxn(entry->txn->id);
            {
                std::lock_guard<std::mutex> guard(entry->latch);
                entry->durable = true;
            }
            entry->cv.notify_one();
        }
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
            DeleteOperator deleteOp(
                table,
                static_cast<size_t>(where_column),
                where_value
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
            if (recovery_manager.isActive()) {
                throw std::runtime_error("INSERT inside an undo/redo WAL update transaction is not supported in v83.");
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

    QueryTable executePlanSnapshot(const PlanSnapshot& snapshot,
                                   const TxnPtr& txn = nullptr,
                                   bool print_tuples = false) {
        if (!acquireQueryLocks(txn, snapshot.components)) {
            return {};
        }

        std::lock_guard<std::mutex> execution_guard(execution_latch);
        auto& metadata = catalog.getTable(snapshot.components.baseTableName);
        auto queryColumns = deriveQueryColumns(snapshot.components, metadata);
        (void)queryColumns;
        if (print_tuples) {
            std::cout << "QUERY "
                      << (snapshot.planRoot
                          ? joinPlanTreeString(snapshot.planRoot)
                          : physicalPlanTreeString(
                                snapshot.components,
                                snapshot.joinKinds
                            ))
                      << std::endl;
        }
        return ::executeQuery(
            snapshot.components,
            catalog,
            buffer_manager,
            txn,
            print_tuples,
            snapshot.joinKinds,
            {},
            snapshot.planRoot
        );
    }

    void explainQuery(const std::string& query) {
        auto components = parseQuery(query);
        resolveQueryColumns(components, catalog);
        printLogicalExplanation(components);
    }

    void printStatsAndEstimates(const std::string& query,
                                const TxnPtr& txn = nullptr,
                                PlanTraitSet required_traits = {}) {
        auto components = parseQuery(query);
        resolveQueryColumns(components, catalog);
        auto stats = loadQueryTableStats(components, catalog);
        printAnalyzeStats(components, stats);
        auto final_estimate = printCardinalityEstimates(components, stats);
        Optimizer optimizer(stats, required_traits);
        auto optimizer_result = optimizer.optimize(components);
        printOptimizerSummary(optimizer_result);
        auto planned_query = optimizer_result.plannedQuery;
        planned_query_cache[
            query + "#memo"
        ] = planned_query;
        printPhysicalJoinCosts(
            planned_query.components,
            planned_query.physicalPlan,
            "memo winner search",
            planned_query.planDescription,
            planned_query.planRoot
        );
        if (planned_query.dpStatesKept > 0) {
            std::cout << "  DP states kept: "
                      << planned_query.dpStatesKept << std::endl;
            std::cout << "  DP candidates considered: "
                      << planned_query.dpCandidatesConsidered << std::endl;
            std::cout << "  Connected subgraph/complement pairs: "
                      << planned_query.dpCrossProductsPruned << std::endl;
        }
        std::cout << "\nActual validation:" << std::endl;
        for (const auto& filter : components.filters) {
            auto base_rows = static_cast<double>(
                tableStatsFor(stats, components, filter.column.tableName).rowCount
            );
            auto mcv_estimate = estimateEqualityFilterRowsWithMcv(
                base_rows,
                columnStatsFor(stats, components, filter.column),
                filter.value
            );
            auto actual = countFilterRows(
                components,
                filter,
                catalog,
                buffer_manager
            );
            std::cout << "  " << columnLabel(filter.column)
                      << " = " << filter.value
                      << ": estimate=" << formatEstimate(mcv_estimate)
                      << " actual=" << actual
                      << " q-error="
                      << formatQError(mcv_estimate, static_cast<double>(actual))
                      << std::endl;
        }

        auto query_rows = executeQuery(query, txn, false);
        if (hasAggregateProjection(components) ||
            components.sumOperation ||
            components.groupBy) {
            std::cout << "  estimated join rows before aggregation: "
                      << formatEstimate(final_estimate) << std::endl;
            std::cout << "  query output rows: "
                      << query_rows.size() << std::endl;
        } else {
            std::cout << "  final join body: estimate="
                      << formatEstimate(final_estimate)
                      << " q-error="
                      << formatQError(
                             final_estimate,
                             static_cast<double>(query_rows.size())
                         )
                      << " actual=" << query_rows.size()
                      << std::endl;
        }
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

std::string jobJoinQuery() {
    return
        "PROJECT MIN{cn.name}, MIN{miidx.info}, MIN{t.title} "
        "FROM title AS t "
        "JOIN kind_type AS kt ON {t.kind_id} = {kt.id} "
        "JOIN movie_companies AS mc ON {t.id} = {mc.movie_id} "
        "JOIN company_name AS cn ON {mc.company_id} = {cn.id} "
        "JOIN company_type AS ct ON {mc.company_type_id} = {ct.id} "
        "JOIN movie_info AS mi ON {t.id} = {mi.movie_id} "
        "JOIN info_type AS it2 ON {mi.info_type_id} = {it2.id} "
        "JOIN movie_info_idx AS miidx ON {t.id} = {miidx.movie_id} "
        "JOIN info_type AS it ON {miidx.info_type_id} = {it.id} "
        "WHERE {cn.country_code} = [us] "
        "AND {ct.kind} = production_companies "
        "AND {it.info} = rating "
        "AND {it2.info} = release_dates "
        "AND {kt.kind} = movie "
        "AND {mi.movie_id} = {miidx.movie_id} "
        "AND {mi.movie_id} = {mc.movie_id} "
        "AND {miidx.movie_id} = {mc.movie_id}";
}

void printSampleRows(const QueryTable& rows, size_t limit) {
    for (size_t row_index = 0; row_index < rows.size() && row_index < limit; row_index++) {
        std::cout << "  ";
        for (const auto& field : rows[row_index].fields) {
            std::cout << fieldToString(*field) << " ";
        }
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[]) {
    BuzzDB db;
    std::string imdb_filename = argc > 1 ? argv[1] : "imdb_large.txt";
    bool seed_database = db.isNewDatabase();
    auto elapsedMs = [](auto start, auto end) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start
        ).count();
    };

    createJobTables(db);

    auto load_txn = db.begin("LOAD");
    auto load_start = std::chrono::steady_clock::now();
    if (seed_database) {
        db.loadDataFile(imdb_filename, load_txn);
    }
    auto load_end = std::chrono::steady_clock::now();
    db.commit(load_txn);
    db.execute("ANALYZE");

    auto query_txn = db.begin("Q1");
    std::cout << "\nQuery optimization with memo rules" << std::endl;
    std::cout << "  Workload: JOB 9-table join query" << std::endl;
    db.explainQuery(jobJoinQuery());
    auto query_components = parseQuery(jobJoinQuery());
    resolveQueryColumns(query_components, db.catalog);
    ColumnRef required_order{"t", "id", -1};
    resolveColumnRef(required_order, db.catalog, query_components);
    auto required_traits = orderedByProperty(required_order);
    db.printStatsAndEstimates(jobJoinQuery(), query_txn, required_traits);

    auto mcv_stats = loadQueryTableStats(query_components, db.catalog);
    auto optimizer_result = Optimizer(mcv_stats, required_traits).optimize(query_components);
    auto plan = optimizer_result.plannedQuery;
    db.clearBufferPool();
    auto query_start = std::chrono::steady_clock::now();
    auto rows = db.executePlanSnapshot(
        makePlanSnapshot(0, plan),
        query_txn,
        false
    );
    auto query_end = std::chrono::steady_clock::now();

    std::cout << "\nMemo winner execution with MCV stats:" << std::endl;
    std::cout << "  Buffer pool is cleared before timed execution."
              << std::endl;
    std::cout << "  elapsed: " << elapsedMs(query_start, query_end)
              << " ms" << std::endl;
    std::cout << "  estimated cost: "
              << formatEstimate(plan.physicalPlan.totalCost) << std::endl;
    std::cout << "  estimated join rows: "
              << formatEstimate(plan.physicalPlan.finalRows) << std::endl;
    std::cout << "  output rows: " << rows.size() << std::endl;
    if (plan.planRoot) {
        std::cout << "  plan:" << std::endl;
        std::cout << prettyJoinPlanTree(plan.planRoot, "    ");
    }
    if (plan.dpStatesKept > 0) {
        std::cout << "  DP states kept: " << plan.dpStatesKept
                  << ", candidates considered: "
                  << plan.dpCandidatesConsidered
                  << ", connected pairs: "
                  << plan.dpCrossProductsPruned << std::endl;
    }
    std::cout << "  Sample rows:" << std::endl;
    printSampleRows(rows, 5);
    db.commit(query_txn);
    std::cout << "\nTiming:" << std::endl;
    if (seed_database) {
        std::cout << "  Load: " << elapsedMs(load_start, load_end)
                  << " ms" << std::endl;
    } else {
        std::cout << "  Load: skipped; database already exists" << std::endl;
    }

    return 0;
}
