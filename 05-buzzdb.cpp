#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>
#include <chrono>

#include <iostream>
#include <map>
#include <string>

#include <iostream>
#include <vector>
#include <string>
#include <variant>

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;

    union {
        int i;
        float f;
        char* s;
    } data;

public:
    Field(int i) : type(INT) { data.i = i; }
    Field(float f) : type(FLOAT) { data.f = f; }
    Field(const std::string& s) : type(STRING) {
        data.s = new char[s.size() + 1];
        std::copy(s.begin(), s.end(), data.s);
        data.s[s.size()] = '\0';
    }

    // Destructor
    ~Field() {
        if (type == STRING) { 
            if (data.s != nullptr){
                std::cout << "Field Destructor:: Free field: " << data.s << "\n";
                delete[] data.s;
            }
        }
    }

    FieldType getType() const { return type; }
    int asInt() const { return data.i; }
    float asFloat() const { return data.f; }
    std::string asString() const { return std::string(data.s); }

    void print() const{
        switch(getType()){
            case INT: std::cout << data.i; break;
            case FLOAT: std::cout << data.f; break;
            case STRING: std::cout << data.s; break;
        }
    }
};

class Tuple {
    std::vector<Field> fields;

public:
    void addField(const Field& field) {
        fields.push_back(field);
    }

    // Destructor
    ~Tuple() {
        std::cout << "Tuple Destructor:: \n";
        for (auto& field : fields) {
            if (field.type == STRING){
                field.~Field();
            }
        }
    }

    void print() const {
        for (const auto& field : fields) {
            field.print();
        }
        std::cout << "\n";
    }
};

class BuzzDB {
private:
    // a map is an ordered key-value container
    std::map<int, std::vector<int>> index;

public:
    // a vector of Tuple structs acting as a table
    std::vector<Tuple> table; 

    // insert function
    void insert(int key, int value) {
        std::cout << "inserting: " << key << "\n";
        Tuple newTuple;
        Field key_field = Field(key);
        Field value_field = Field(value);
        float float_val = 132.04;
        Field float_field = Field(float_val);
        Field string_field = Field("buzzdb");

        newTuple.addField(key_field);
        newTuple.addField(value_field);
        newTuple.addField(float_field);
        newTuple.addField(string_field);

        table.push_back(newTuple);
        index[key].push_back(value);
        std::cout << "Done \n";
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