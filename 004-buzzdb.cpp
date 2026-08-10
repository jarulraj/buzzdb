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

enum FieldType { INT, FLOAT };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;

    union {
        int i;
        float f;
    } data;

public:
    Field(int i) : type(INT) { data.i = i; }
    Field(float f) : type(FLOAT) { data.f = f; }

    FieldType getType() const { return type; }
    int asInt() const { return data.i; }
    float asFloat() const { return data.f; }

    void print() const{
        switch(getType()){
            case INT: std::cout << data.i; break;
            case FLOAT: std::cout << data.f; break;
        }
    }
};

class Tuple {
    std::vector<Field> fields;

public:
    void addField(const Field& field) {
        fields.push_back(field);
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
        Tuple newTuple;
        Field key_field = Field(key);
        Field value_field = Field(value);
        float float_val = 132.04;
        Field float_field = Field(float_val);

        newTuple.addField(key_field);
        newTuple.addField(value_field);
        newTuple.addField(float_field);

        table.push_back(newTuple);
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