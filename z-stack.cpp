#include <iostream>
#include <vector>

// Attempts to return a reference to a vector allocated on the stack
const std::vector<int>& createVectorOnStack() {
    std::vector<int> vec = {1, 2, 3, 4}; // Allocated on the stack
    return vec;
    // Warning: Returning reference to local/temporary object here
}

int main() {
    const auto& myVec = createVectorOnStack();
    // Undefined behavior: myVec refers to a destroyed vector

    // Attempting to use myVec will lead to undefined behavior
    for (int element : myVec) { // This is dangerous and incorrect!
        std::cout << element << " ";
    }
    std::cout << "\n";

    // The program may crash, or worse, silently produce incorrect results
}
