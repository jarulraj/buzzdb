#include <iostream>
#include <thread>
#include <vector>

volatile int sharedCounter = 0; // Marked volatile to discourage compiler optimizations

void incrementCounter() {
    for (int i = 0; i < 100000; ++i) {
        // Each thread increments the sharedCounter by its threadID + 1
        // This makes the operation less predictable and harder to optimize out
        sharedCounter += 1;
        sharedCounter -= 1;
    }
}

int main() {
    const int numThreads = 2;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i) {
        threads.push_back(std::thread(incrementCounter));
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // The expected result would be the sum of arithmetic series multiplied by the loop count
    // However, due to data races, the actual result may differ
    int expectedCounter = 0;
    for (int i = 1; i <= numThreads; ++i) {
        expectedCounter += 100000;
    }

    std::cout << "Expected counter: " << expectedCounter << std::endl;
    std::cout << "Actual counter: " << sharedCounter << std::endl;

    return 0;
}
