#include <iostream>
#include <vector>
#include <fstream>
#include <random>
#include <filesystem>
#include <chrono>
#include <numeric>
#include <unordered_map>
#include <iomanip>
#include <cmath>

const int NUM_ROWS = 500000;
const int NUM_COLS = 8;
const int PAGE_SIZE = 4096; // PAGE_SIZE in bytes
const int INTS_PER_PAGE = PAGE_SIZE / sizeof(int);
const int ROWS_PER_PAGE = PAGE_SIZE / (NUM_COLS * sizeof(int)); // Number of rows that fit in a page
const int FILTER_COLUMN = 2;
const int AGGREGATE_COLUMN = 3;
const int FILTER_THRESHOLD = 80;

// Custom skewed distribution for product IDs
int zipf(double alpha, int n) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);

    static std::vector<double> cdf;
    if (cdf.empty()) {
        double sum = 0.0;
        for (int i = 1; i <= n; i++) {
            sum += 1.0 / std::pow(i, alpha);
        }
        cdf.resize(n + 1);
        cdf[0] = 0;
        for (int i = 1; i <= n; i++) {
            cdf[i] = cdf[i - 1] + (1.0 / std::pow(i, alpha)) / sum;
        }
    }

    double r = dis(gen);
    for (int i = 1; i <= n; i++) {
        if (r < cdf[i]) {
            return i;
        }
    }
    return n;
}


// Function to generate realistic data
void generateData(std::vector<std::vector<int>>& data) {
    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < NUM_ROWS; ++i) {
        std::vector<int> row(NUM_COLS);
        for (int j = 0; j < NUM_COLS; ++j) {
            row[j] = zipf(1.8, 100); // Zipf distribution
        }
        data.push_back(row);
    }
}

// Function to store data in row-wise and columnar storage
void storeData(const std::vector<std::vector<int>>& data) {
    std::ofstream rowFile("row_storage.dat", std::ios::binary);
    std::vector<std::ofstream> colFiles(NUM_COLS);
    for (int i = 0; i < NUM_COLS; ++i) {
        colFiles[i].open("column_storage_" + std::to_string(i) + ".dat", std::ios::binary);
    }

    int rowBytesWritten = 0;
    std::vector<int> colBytesWritten(NUM_COLS, 0);

    auto startRowTime = std::chrono::high_resolution_clock::now();

    for (const auto& row : data) {
        // Write row-wise data
        rowFile.write(reinterpret_cast<const char*>(row.data()), row.size() * sizeof(int));
        rowBytesWritten += row.size() * sizeof(int);
        if (rowBytesWritten >= PAGE_SIZE) {
            rowBytesWritten = 0;
        }
    }

    // Add padding for the last page if necessary for row-wise storage
    int remainingRowBytes = rowBytesWritten % PAGE_SIZE;
    if (remainingRowBytes > 0) {
        int padding = PAGE_SIZE - remainingRowBytes;
        rowFile.write(std::vector<char>(padding, 0).data(), padding);
    }

    rowFile.close();

    auto endRowTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> rowWriteTime = endRowTime - startRowTime;

    auto startColTime = std::chrono::high_resolution_clock::now();

    for (const auto& row : data) {
        for (int j = 0; j < NUM_COLS; ++j) {
            colFiles[j].write(reinterpret_cast<const char*>(&row[j]), sizeof(int));
            colBytesWritten[j] += sizeof(int);
            if (colBytesWritten[j] >= PAGE_SIZE) {
                colBytesWritten[j] = 0;
            }
        }
    }

    // Add padding for the last pages if necessary for columnar storage
    for (int j = 0; j < NUM_COLS; ++j) {
        int remainingColBytes = colBytesWritten[j] % PAGE_SIZE;
        if (remainingColBytes > 0) {
            int padding = PAGE_SIZE - remainingColBytes;
            colFiles[j].write(std::vector<char>(padding, 0).data(), padding);
        }
    }

    for (int i = 0; i < NUM_COLS; ++i) {
        colFiles[i].close();
    }

    auto endColTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> colWriteTime = endColTime - startColTime;

    std::cout << "Row-wise storage write time: " << rowWriteTime.count() << " seconds" << std::endl;
    std::cout << "Columnar storage write time: " << colWriteTime.count() << " seconds" << std::endl;
}

// Function to read a page of data from row-wise storage
std::vector<std::vector<int>> readRowPage(std::ifstream& file, int startRow, int& pagesRead) {
    std::vector<std::vector<int>> page(ROWS_PER_PAGE, std::vector<int>(NUM_COLS));
    int pageIndex = startRow / ROWS_PER_PAGE;
    file.seekg(pageIndex * PAGE_SIZE, std::ios::beg);
    for (int i = 0; i < ROWS_PER_PAGE && file; ++i) {
        file.read(reinterpret_cast<char*>(page[i].data()), NUM_COLS * sizeof(int));
    }

    pagesRead++;
    return page;
}

// Function to read a page of data from columnar storage
std::vector<int> readColumnPage(std::ifstream& file, int startRow, int&pagesRead) {
    std::vector<int> page(INTS_PER_PAGE, 0);
    int pageIndex = startRow / INTS_PER_PAGE;
    file.seekg(pageIndex * PAGE_SIZE, std::ios::beg);

    file.read(reinterpret_cast<char*>(page.data()), sizeof(int) * INTS_PER_PAGE);

    pagesRead++;
    return page;
}

// Function to perform the query on row storage
std::pair<double, long long> queryRowStorage(int& pagesRead) {
    std::ifstream rowFile("row_storage.dat", std::ios::binary);
    long long sum = 0;
    int countFilteredRows = 0;

    for (int startRow = 0; startRow < NUM_ROWS; startRow += ROWS_PER_PAGE) {
        auto page = readRowPage(rowFile, startRow, pagesRead);
        for (const auto& row : page) {
            //std::cout << row[FILTER_COLUMN] << "\n";
            if (row[FILTER_COLUMN] > FILTER_THRESHOLD) {
                sum += row[AGGREGATE_COLUMN];
                countFilteredRows++;
            }
        }
    }

    rowFile.close();
    return {countFilteredRows, sum};
}

// Function to perform the query on columnar storage with late materialization
long long queryColumnarStorage(int& filterPagesRead, int& aggregatePagesRead) {
    std::ifstream filterFile("column_storage_" + std::to_string(FILTER_COLUMN) + ".dat", std::ios::binary);
    std::ifstream aggregateFile("column_storage_" + std::to_string(AGGREGATE_COLUMN) + ".dat", std::ios::binary);
    long long sum = 0;
    std::unordered_map<int, std::vector<int>> pageOffsetMap;

    // Read the filter column and collect row offsets for qualifying rows
    for (int startRow = 0; startRow < NUM_ROWS; startRow += INTS_PER_PAGE) {
        auto filterPage = readColumnPage(filterFile, startRow, filterPagesRead);
        for (size_t i = 0; i < filterPage.size(); ++i) {
            //std::cout << filterPage[i] << "\n";
            if (filterPage[i] > FILTER_THRESHOLD) {
                pageOffsetMap[startRow / INTS_PER_PAGE].push_back(i);
            }
        }
    }

    // Read the aggregate column using the collected row offsets
    for (const auto& [pageIndex, offsets] : pageOffsetMap) {
        auto aggregatePage = readColumnPage(aggregateFile, pageIndex * INTS_PER_PAGE, aggregatePagesRead);
        for (const auto& rowIndex : offsets) {
            sum += aggregatePage[rowIndex];
        }
    }

    filterFile.close();
    aggregateFile.close();

    return sum;
}

// Function to print a small subset of the generated table
void printGeneratedData() {
    std::ifstream rowFile("row_storage.dat", std::ios::binary);
    if (!rowFile) {
        std::cerr << "Failed to open row storage file for reading." << std::endl;
        return;
    }

    std::cout << "Sample of generated data (first 10 rows):" << std::endl;
    for (int i = 0; i < 10; ++i) {
        std::vector<int> row(NUM_COLS);
        rowFile.read(reinterpret_cast<char*>(row.data()), NUM_COLS * sizeof(int));
        for (int j = 0; j < NUM_COLS; ++j) {
            std::cout << std::setw(4) << row[j] << " ";
        }
        std::cout << std::endl;
    }
    rowFile.close();
}

int main() {
    std::vector<std::vector<int>> data;
    generateData(data);
    storeData(data);
    std::cout << "Data generated and stored." << std::endl;

    std::cout << "PAGE SIZE: " << PAGE_SIZE << "\n";
    std::cout << "ROWS PER PAGE: " << ROWS_PER_PAGE << "\n";
    std::cout << "INTEGERS PER PAGE: " << INTS_PER_PAGE << "\n";

    // Print a small subset of the generated table
    printGeneratedData();

    // Measure and display the performance of row storage
    int rowPagesRead = 0;
    auto start = std::chrono::high_resolution_clock::now();
    auto [countFilteredRows, rowStorageResult] = queryRowStorage(rowPagesRead);

    double selectivity = static_cast<double>(countFilteredRows) / NUM_ROWS;
    std::cout << "Filter Selectivity: " << selectivity << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> rowStorageTime = end - start;
    std::cout << "Row Storage Query Result: " << rowStorageResult << std::endl;
    std::cout << "Row Storage Query Time: " << rowStorageTime.count() << " seconds" << std::endl;

    // Measure and display the performance of columnar storage with late materialization
    int filterPagesRead = 0;
    int aggregatePagesRead = 0;
    start = std::chrono::high_resolution_clock::now();
    auto columnarStorageResult = queryColumnarStorage(filterPagesRead, aggregatePagesRead);
    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> columnarStorageTime = end - start;
    std::cout << "Columnar Storage Query Result: " << columnarStorageResult << std::endl;
    std::cout << "Columnar Storage Query Time: " << columnarStorageTime.count() << " seconds" << std::endl;

    std::cout <<"\n\nPages Read: \n";

    std::cout << "Total Row Storage Pages Read: " << rowPagesRead << std::endl;
    std::cout << "Filter Pages Read: " << filterPagesRead << std::endl;
    std::cout << "Aggregate Pages Read: " << aggregatePagesRead << std::endl;
    std::cout << "Total Columnar Storage Pages Read: " << (filterPagesRead + aggregatePagesRead) << std::endl;

    return 0;
}
