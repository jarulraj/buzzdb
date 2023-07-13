#include <iostream>
#include <map>
#include <vector>

struct Row {
    int key;
    int value;
};

class ToyDatabase {
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
    ToyDatabase db;

    db.insert(1, 100);
    db.insert(1, 200);
    db.insert(2, 50);
    db.insert(3, 200);
    db.insert(3, 200);
    db.insert(3, 100);
    db.insert(4, 500);
    
    db.selectGroupBySum();

    return 0;
}