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

    int data_i;
    float data_f;
    std::unique_ptr<char[]> data_s;
    size_t data_s_length;

public:
    Field(int i) : type(INT) { data_i = i; }
    Field(float f) : type(FLOAT) { data_f = f; }
    Field(const std::string& s) : type(STRING) {
        data_s_length = s.size() + 1;
        data_s = std::make_unique<char[]>(data_s_length);
        strcpy(data_s.get(), s.c_str());
    }

    // Copy assignment operator
    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;

        switch(type){
            case INT: 
                data_i = other.data_i; break;
            case FLOAT: 
                data_f = other.data_f; break;
            case STRING:
                data_s_length = other.data_s_length;
                data_s = std::make_unique<char[]>(data_s_length);
                strcpy(data_s.get(), other.data_s.get());
                break;
        }

        return *this;
    }

    // Copy constructor
    Field(Field&& other){
        type = other.type;

        switch(type){
            case INT: 
                data_i = other.data_i; break;
            case FLOAT: 
                data_f = other.data_f; break;
            case STRING:
                data_s_length = other.data_s_length;
                data_s = std::make_unique<char[]>(data_s_length);
                strcpy(data_s.get(), other.data_s.get());
                break;
        }
    }

    FieldType getType() const { return type; }
    int asInt() const { return data_i; }
    float asFloat() const { return data_f; }
    std::string asString() const { return std::string(data_s.get()); }

    void print() const{
        switch(getType()){
            case INT: std::cout << data_i; break;
            case FLOAT: std::cout << data_f; break;
            case STRING: std::cout << std::string(data_s.get()); break;
        }
    }
};

class Tuple {
    std::vector<std::unique_ptr<Field>> fields;

public:

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

class BuzzDB {
private:
    // a map is an ordered key-value container
    std::map<int, std::vector<int>> index;

public:
    size_t number_of_tuples = 4;

    // a vector of Tuple unique pointers acting as a table
    std::vector<std::unique_ptr<Tuple>> table;

    // insert function
    void insert(int key, int value) {
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

        table.push_back(std::move(newTuple));
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

    // Get the end time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate and print the elapsed time
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;

    return 0;
}