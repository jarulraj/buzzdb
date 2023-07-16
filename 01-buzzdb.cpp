#include <iostream>
#include <map>
#include <vector>

// A "class" in C++ is a user-defined data type. 
// It is a blueprint for creating objects of a particular type, 
// providing initial values for state (member variables or fields), 
// and implementations of behavior (member functions or methods)
class Tuple {
public:
    int key;
    int value;
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
        Tuple newTuple = {key, value};
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
    BuzzDB db;

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