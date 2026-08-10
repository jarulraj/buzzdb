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

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

const int PAGE_SIZE = 4096;

// Page class
class Page {
public:
    size_t used_size = 0;
    std::vector<std::unique_ptr<Tuple>> tuples;

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {
        // Calculate the size of tuple
        size_t tuple_size = 0;
        for (const auto& field : tuple->fields) {
            tuple_size += field->data_length;
        }

        if (used_size + tuple_size > PAGE_SIZE) {
            // If not enough space, run garbage collection and compaction first
            //garbageCollect();
        }

        // If there is still not enough space, reject the operation
        if (used_size + tuple_size > PAGE_SIZE) {
            std::cout << "Page is full. Cannot add more tuples. ";
            std::cout << "Page contains: " << tuples.size() << " tuples. \n";
            return false;
        }

        tuples.push_back(std::move(tuple));
        used_size += tuple_size;
        return true;
    }

    // Write this page to a file.
    void write(const std::string& filename) const {
        std::ofstream out(filename);
        // First write the number of tuples.
        size_t numTuples = tuples.size();
        out.write(reinterpret_cast<const char*>(&numTuples), sizeof(numTuples));

        // Then write each tuple.
        for (const auto& tuple : tuples) {
            // Write the number of fields in the tuple.
            size_t numFields = tuple->fields.size();
            out.write(reinterpret_cast<const char*>(&numFields), sizeof(numFields));

            // Then write each field.
            for (const auto& field : tuple->fields) {
                // Write the type of the field.
                out.write(reinterpret_cast<const char*>(&field->type), sizeof(field->type));
                // Write the length of the field.
                out.write(reinterpret_cast<const char*>(&field->data_length), sizeof(field->data_length));
                // Then write the field data.
                out.write(field->data.get(), field->data_length);
            }

        }

        out.close();
    }

    // Read this page from a file.
    void read(const std::string& filename) {
        std::ifstream in(filename);

        // First read the number of tuples.
        size_t numTuples;
        in.read(reinterpret_cast<char*>(&numTuples), sizeof(numTuples));

        std::cout << "Num Tuples: " << numTuples << "\n";

        // Then read each tuple.
        for (size_t i = 0; i < numTuples; ++i) {
            auto tuple = std::make_unique<Tuple>();

            // Read the number of fields in the tuple.
            size_t numFields;
            in.read(reinterpret_cast<char*>(&numFields), sizeof(numFields));

            // Then read each field.
            for (size_t j = 0; j < numFields; ++j) {
                // Read the type of the field.
                FieldType type;
                in.read(reinterpret_cast<char*>(&type), sizeof(type));
                // Read the length of the field.
                size_t data_length;
                in.read(reinterpret_cast<char*>(&data_length), sizeof(data_length));
                // Then read the field data.
                std::unique_ptr<char[]> data(new char[data_length]);
                in.read(data.get(), data_length);

                // Add the field to the tuple.
                switch(type){
                    case INT:
                    {
                        int val = *reinterpret_cast<int*>(data.get());
                        auto field = std::make_unique<Field>(val);
                        tuple->addField(std::move(field));
                        break;
                    }
                    case FLOAT:
                    {
                        float val = *reinterpret_cast<float*>(data.get());
                        auto field = std::make_unique<Field>(val);
                        tuple->addField(std::move(field));
                        break;
                    }
                    case STRING: 
                    {
                        char* val = reinterpret_cast<char*>(data.get());
                        auto field = std::make_unique<Field>(std::string(val, data_length));
                        tuple->addField(std::move(field));
                        break;
                    }
                }

            }

            std::cout << "Tuple " << (i+1) << " :: "; 
            tuple->print();

            // Add the tuple to the page.
            //addTuple(std::move(tuple));
        }

        in.close();
    }

};


class BuzzDB {
private:
    // a map is an ordered key-value container
    std::map<int, std::vector<int>> index;

public:
    size_t max_number_of_tuples = 500;
    size_t currently_added_tuples = 0;

    // a vector of Tuple unique pointers acting as a table
    std::vector<std::unique_ptr<Tuple>> table;

    Page page;

    // insert function
    void insert(int key, int value) {
        if (currently_added_tuples == max_number_of_tuples)
        return;

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
        currently_added_tuples += 1;

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

    // Serialize to disk
    db.page.write(filename);

    // Deserialize from disk
    Page page2;
    page2.read(filename);

    // Get the end time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

    return 0;
}