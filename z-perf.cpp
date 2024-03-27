#include <chrono>
#include <iostream>

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    
    long sum = 0;
    for(int i = 0; i < 100000000; ++i) {
        sum += i;
    }

    std::cout << "Sum: " << sum << "\n";

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "Time taken: " << diff.count() << " seconds\n";
}
