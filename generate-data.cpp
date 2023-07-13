#include <fstream>
#include <random>
#include <iostream>

int main() {
    std::random_device rd;
    std::mt19937 gen(rd());
    // For customer IDs between 1 and 9
    std::uniform_int_distribution<> dis1(1, 9); 
    std::poisson_distribution<> dis2(100);

    int number_of_sales = 1000;

    std::ofstream outputFile("output.txt");
    if (outputFile.is_open()) {
        for (int i = 0; i < number_of_sales; ++i) {
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