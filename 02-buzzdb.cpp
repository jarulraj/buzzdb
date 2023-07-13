#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <iostream>

// A "class" in C++ is a user-defined data type. 
// It is a blueprint for creating objects of a particular type, 
// providing initial values for state (member variables or fields), 
// and implementations of behavior (member functions or methods)
class Row {
public:
    int key;
    int value;
};

class BuzzDB {
private:
    // a map is an ordered key-value container
    std::map<int, std::vector<int>> index;

public:
    // a vector of Row structs acting as a table
    std::vector<Row> table; 

    // insert function
    void insert(int key, int value) {
        Row newRow = {key, value};
        table.push_back(newRow);
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
    BuzzDB db;

    int number_of_sales = 1000;

    std::ifstream inputFile("output.txt");
    if (!inputFile) {
        std::cerr << "Unable to open file" << std::endl;
        return 1;
    }

    for (int i = 0; i < number_of_sales; ++i) {
        int field1, field2;
        if (!(inputFile >> field1 >> field2)) {
            std::cerr << "Error reading line " << (i+1) << std::endl;
            return 1;
        }

        db.insert(field1, field2);
    }
    
    db.selectGroupBySum();

    return 0;
}