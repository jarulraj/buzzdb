#include <iostream>
#include <thread>
#include <vector>

volatile int bankBalance = 1000; // Initial bank balance

void performTransactions() {
    for (int i = 0; i < 100000; ++i) {
        bankBalance += 10; // Deposit
        // Sleep for 2 microseconds to encourage context switches
        //std::this_thread::sleep_for(std::chrono::microseconds(10));
        bankBalance -= 10; // Withdrawal
    }
}

int main() {
    std::vector<std::thread> threads;

    for (int i = 0; i < 8; ++i) {
        // Initiate transactions without synchronization
        threads.emplace_back(performTransactions); 
    }
    for (auto& thread : threads) {
        thread.join(); // Wait for all threads to finish
    }
    std::cout << "Expected balance: 1000\nActual balance: " << bankBalance << std::endl;
    return 0;
}

