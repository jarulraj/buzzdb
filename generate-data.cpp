#include <fstream>
#include <random>
#include <iostream>

int main() {
    std::random_device rd;
    std::mt19937 gen(rd());
    // For customer IDs between 1 and 10
    std::uniform_int_distribution<> dis1(1, 10); 
    // For amounts between 1 and 100    
    std::uniform_int_distribution<> dis2(1, 100);

    std::ofstream outputFile("output.txt");
    if (outputFile.is_open()) {
        for (int i = 0; i < 1000; ++i) {
            int customerId = dis1(gen);
            int amount = dis2(gen);
            outputFile << customerId << " " << amount << "\n";
        }
        outputFile.close();
    } else {
        std::cerr << "Unable to open file";
    }

    return 0;
}