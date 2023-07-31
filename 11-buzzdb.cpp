#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>

#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread> // For std::this_thread::sleep_for

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
static constexpr size_t MAX_SLOTS = 200;   // Fixed number of slots
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
    size_t used_size = 0;
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;

    SlottedPage(){
        used_size = metadata_size;
        std::cout << "Used size: " << used_size << " bytes \n";

        // In case of an empty page,
        // Initialize slot array inside page
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

        // If there is still not enough space, reject the operation
        if (used_size + tuple_size > PAGE_SIZE) {
            std::cout << "Page is full. Cannot add more tuples. ";
            std::cout << "Page contains: " << used_size << " bytes. \n";
            return false;
        }

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
            std::cout << "Page does not contain an empty slot with sufficient space to store the tuple.";
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
                offset = used_size;
            }

            slot_array[slot_itr].offset = offset;
        }
        else{
            offset = slot_array[slot_itr].offset;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE){
            slot_array[slot_itr].length = tuple_size;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + offset, 
                    serializedTuple.c_str(), 
                    tuple_size);

        // Update page's used size
        used_size += tuple_size;

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;

                // Update page's used size
                used_size -= slot_array[slot_itr].length;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Write this page to a file.
    void write(const std::string& filename) const {
        std::ofstream out(filename);
        out.write(page_data.get(), PAGE_SIZE);
        out.close();
    }

    // Read this page from a file.
    static std::unique_ptr<SlottedPage> deserialize(const std::string& filename) {
        std::ifstream in(filename);
        auto page = std::make_unique<SlottedPage>();

        // Read the content of the file into the new unique_ptr
        if (in.read(page->page_data.get(), PAGE_SIZE)) {
            std::cout << "Page read successfully from file." << std::endl;
        } else {
            std::cerr << "Error: Unable to read data from the file." << std::endl;
        }

        in.close();
        return page;
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


class BuzzDB {
private:
    // a map is an ordered key-value container
    std::map<int, std::vector<int>> index;

public:
    size_t max_number_of_tuples = 5000;
    size_t tuple_insertion_attempt_counter = 0;

    // a vector of Tuple unique pointers acting as a table
    std::vector<std::unique_ptr<Tuple>> table;

    SlottedPage page;

    // insert function
    void insert(int key, int value) {
        tuple_insertion_attempt_counter += 1;

        if(tuple_insertion_attempt_counter >= max_number_of_tuples){
            return;
        }

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

        //newTuple->print();

        page.addTuple(std::move(newTuple));

        // Skip deleting tuples only once every hundred tuples
        if (tuple_insertion_attempt_counter % 100 != 0){
            page.deleteTuple(0);
        }

        //table.push_back(std::move(newTuple));
        index[key].push_back(value);
    }

    // perform a SELECT ... GROUP BY ... SUM query
    void selectGroupBySum() {
        for (auto const& pair : index) { // for each unique key
            int sum = 0;
            for (auto const& value : pair.second) {
                sum += value; // sum all values for the key
            }
            std::cout << "key: " << pair.first << ", sum: " << sum << '\n';
        }
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
    
    db.selectGroupBySum();

    std::string filename = "page.dat";

    db.page.print();

    // Serialize to disk
    db.page.write(filename);

    // Deserialize from disk
    auto loadedPage = SlottedPage::deserialize(filename);

    loadedPage->print();

    std::cout << "Deleting slots 0 and 7 \n";
    loadedPage->deleteTuple(0);
    loadedPage->deleteTuple(7);

    loadedPage->write(filename);

    // Deserialize again from disk -- page is updated this time
    auto loadedPage2 = SlottedPage::deserialize(filename);

    loadedPage2->print();

    // Get the end time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

    return 0;
}