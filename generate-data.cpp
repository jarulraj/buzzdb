#include <fstream>
#include <random>
#include <iostream>

int main() {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::default_random_engine generator(std::random_device{}());
    std::uniform_int_distribution<int> amountDistribution(101, 999);
    std::discrete_distribution<int> customerDistribution({40, 20, 10, 10, 5, 5, 5, 3, 2});

    int number_of_sales = 10 * 1000;

    std::ofstream outputFile("output.txt");
    if (outputFile.is_open()) {
        for (int i = 0; i < number_of_sales; ++i) {
            // +1 to get IDs from 1 to 10
            int customerId = customerDistribution(gen);
            int amount = amountDistribution(gen);
            outputFile << customerId << " " << amount << "\n";
        }
        outputFile.close();
    } else {
        std::cerr << "Unable to open file";
    }

    outputFile.close();

    return 0;
}
