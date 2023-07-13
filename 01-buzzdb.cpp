#include <iostream>
#include <map>
#include <vector>

struct Row {
    int key;
    int value;
};

class ToyDatabase {
private:
    std::map<int, std::vector<int>> index; // a map is an ordered key-value container, acting as a B+ tree

public:
    std::vector<Row> table; // a vector of Row structs acting as a table

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