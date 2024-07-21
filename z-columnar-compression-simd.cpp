#include <iostream>
#include <vector>
#include <fstream>
#include <random>
#include <chrono>
#include <cmath>
#include <cstring>  // For memset
#include <arm_neon.h>

const int NUM_ROWS = 1000000;
const int NUM_COLS = 4;  // Includes Timestamp, Temperature, Humidity, Wind Speed
const int PAGE_SIZE = 4096;  // PAGE_SIZE in bytes
//const int INTS_PER_PAGE = PAGE_SIZE / sizeof(int);
//const int ROWS_PER_PAGE = PAGE_SIZE / (NUM_COLS * sizeof(int));
const int BITS_PER_BYTE = 8; 
const int START_TIMESTAMP = 1609459200; // Starting timestamp (e.g., 2021-01-01 00:00:00)
const double QUERY_START_OFFSET = 0.6;
const double QUERY_END_OFFSET = 0.8;
const int TEMPERATURE_BIT_WIDTH = 4;

void generateData(std::vector<std::vector<int>>& data) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> temp_change(-0.5, 5); // Small change around the last temperature
    std::uniform_int_distribution<> humidity_dis(0, 100);
    std::uniform_int_distribution<> wind_speed_dis(0, 100);
    std::uniform_int_distribution<> time_dis(1, 5);

    int timestamp = START_TIMESTAMP;
    int last_temp = 35; // Starting temperature

    for (int i = 0; i < NUM_ROWS; ++i) {
        std::vector<int> row(NUM_COLS);
        row[0] = timestamp;
        last_temp = std::min(40, std::max(35, static_cast<int>(last_temp + temp_change(gen)))); // Keep within bounds
        row[1] = last_temp;
        row[2] = humidity_dis(gen);
        row[3] = wind_speed_dis(gen);
        data.push_back(row);
        timestamp += time_dis(gen);
    }
}

// Store data in both row-wise and columnar formats
void storeData(const std::vector<std::vector<int>>& data) {
    std::ofstream rowFile("row_storage.dat", std::ios::binary);
    std::vector<std::ofstream> colFiles(NUM_COLS);
    for (int i = 0; i < NUM_COLS; ++i) {
        colFiles[i].open("column_storage_" + std::to_string(i) + ".dat", std::ios::binary);
    }

    for (const auto& row : data) {
        rowFile.write(reinterpret_cast<const char*>(row.data()), NUM_COLS * sizeof(int));
        for (int j = 0; j < NUM_COLS; ++j) {
            colFiles[j].write(reinterpret_cast<const char*>(&row[j]), sizeof(int));
        }
    }

    // Manage padding for the last page if not full
    long currentPos = rowFile.tellp();
    int remainingBytes = PAGE_SIZE - (currentPos % PAGE_SIZE);
    if (remainingBytes > 0 && remainingBytes < PAGE_SIZE) {
        std::vector<char> padding(remainingBytes, 0);
        rowFile.write(padding.data(), padding.size());
    }

    rowFile.close();
    for (auto& file : colFiles) {
        currentPos = file.tellp();
        remainingBytes = PAGE_SIZE - (currentPos % PAGE_SIZE);
        if (remainingBytes > 0 && remainingBytes < PAGE_SIZE) {
            std::vector<char> padding(remainingBytes, 0);
            file.write(padding.data(), padding.size());
        }
        file.close();
    }
}

// Query functions for both row-wise and columnar storage
double queryAverageTemperatureRowStorage(int startTimestamp, int endTimestamp) {
    std::ifstream rowFile("row_storage.dat", std::ios::binary);
    std::vector<int> row(NUM_COLS);
    int sumTemperatures = 0;
    int count = 0;

    while (rowFile.read(reinterpret_cast<char*>(row.data()), NUM_COLS * sizeof(int))) {
        if (row[0] >= startTimestamp && row[0] <= endTimestamp) {
            sumTemperatures += row[1];
            count++;
        }
    }
    rowFile.close();
    return count > 0 ? static_cast<double>(sumTemperatures) / count : 0.0;
}


double queryAverageTemperatureColumnarStorage(int startTimestamp, int endTimestamp) {
    std::ifstream timestampFile("column_storage_0.dat", std::ios::binary);
    std::ifstream temperatureFile("column_storage_1.dat", std::ios::binary);

    if (!timestampFile.is_open() || !temperatureFile.is_open()) {
        std::cerr << "Error opening files." << std::endl;
        return 0.0;
    }

    int timestamp, temperature;
    int sumTemperatures = 0;
    int count = 0;
    bool withinRange = false;
    long long startOffset = -1, endOffset = -1, currentOffset = 0;

    // Find start and end offsets for the given timestamp range
    while (timestampFile.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp))) {
        if (timestamp >= startTimestamp && startOffset == -1) {
            startOffset = currentOffset;
            withinRange = true;
        }
        if (timestamp > endTimestamp && withinRange) {
            endOffset = currentOffset - 1;
            break;
        }
        currentOffset++;
    }

    //std::cout << "COLUMN: " << startOffset << " " << endOffset << "\n";

    // If the range never started, return zero
    if (startOffset == -1) return 0.0;

    // If the end offset was never set but we found a start, set end offset to the last read position
    if (endOffset == -1) endOffset = currentOffset - 1;

    // Move to the start position in the temperature file and read relevant temperatures
    temperatureFile.seekg(startOffset * sizeof(int), std::ios::beg);
    for (long long i = startOffset; i <= endOffset; ++i) {
        if (temperatureFile.read(reinterpret_cast<char*>(&temperature), sizeof(temperature))) {
            //std::cout << i << " : " << temperature << "\n";
            sumTemperatures += temperature;
            count++;
        }
    }

    timestampFile.close();
    temperatureFile.close();

    return count > 0 ? static_cast<double>(sumTemperatures) / count : 0.0;
}

// Bit-packing utilities
std::vector<uint8_t> bitPack(const std::vector<int>& values, int bitWidth) {
    int numBits = values.size() * bitWidth;
    int numBytes = (numBits + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
    std::vector<uint8_t> packedData(numBytes, 0);

    int bitPos = 0;
    for (int value : values) {
        for (int b = 0; b < bitWidth; ++b) {
            if (value & (1 << b)) {
                packedData[bitPos / BITS_PER_BYTE] |= (1 << (bitPos % BITS_PER_BYTE));
            }
            ++bitPos;
        }
    }
    return packedData;
}

int globalMinTemp;

// Function to store data in compressed columnar storage
void storeCompressedColumnar(const std::vector<std::vector<int>>& data) {
    std::vector<int> timestamps, temperatures;
    for (const auto& row : data) {
        timestamps.push_back(row[0]);
        temperatures.push_back(row[1]);
    }

    // Delta encoding for timestamps
    std::vector<int> deltaTimestamps(timestamps.size());
    // Store the first timestamp directly as there is no previous timestamp to form a delta with
    deltaTimestamps[0] = timestamps[0];

    // Delta encode the remaining timestamps
    std::adjacent_difference(timestamps.begin(), timestamps.end(), deltaTimestamps.begin());
    // The first value now contains the first timestamp, which is not a delta. We overwrite it later.

    // Correctly computing deltas: start from the second element
    std::transform(timestamps.begin() + 1, timestamps.end(), timestamps.begin(), deltaTimestamps.begin() + 1,
               [](int current, int previous) {
                   return current - previous;
               });

    // Since the first element isn't a delta, reassign it to be the first actual timestamp
    deltaTimestamps[0] = timestamps[0];

    // For debugging: Output the first few delta values to verify they're correct
    std::cout << "Delta-encoded Timestamps: \n";
    for(int i = 0; i < 5; i++) {
        std::cout << deltaTimestamps[i] << "\n";
    }

    // Determine bit width needed for temperature values
    int minTemp = *std::min_element(temperatures.begin(), temperatures.end());
    globalMinTemp = minTemp;

    std::cout << "Minimum Temperature: " << minTemp << "\n";

    // Adjust temperatures for zero-based indexing
    std::vector<int> adjustedTemperatures(temperatures.size());
    std::transform(temperatures.begin(), temperatures.end(), adjustedTemperatures.begin(),
                [minTemp](int temp) { return temp - minTemp; });

    // Bit-pack adjusted temperature values
    auto packedTemperatures = bitPack(adjustedTemperatures, TEMPERATURE_BIT_WIDTH);

    // Optionally output some bit-packed data for verification
    std::cout << "Sample adjusted temperatures: ";
    for (size_t i = 0; i < std::min(size_t(10), adjustedTemperatures.size()); i++) {
        std::cout << adjustedTemperatures[i] << " ";
    }
    std::cout << "\n";

    std::cout << "Sample bit-packed adjusted temperatures: ";
    for (size_t i = 0; i < std::min(size_t(10), packedTemperatures.size()); i++) {
        std::cout << std::bitset<8>(packedTemperatures[i]) << " ";
    }
    std::cout << "\n";

    // Write to files
    std::ofstream deltaFile("delta_encoded_timestamps.dat", std::ios::binary);
    std::ofstream tempFile("packed_temperatures.dat", std::ios::binary);

    for (int delta : deltaTimestamps) {
        deltaFile.write(reinterpret_cast<const char*>(&delta), sizeof(delta));
    }

    tempFile.write(reinterpret_cast<const char*>(packedTemperatures.data()), packedTemperatures.size());

    deltaFile.close();
    tempFile.close();
}

double queryCompressedColumnar(int startTimestamp, int endTimestamp) {
    std::ifstream deltaFile("delta_encoded_timestamps.dat", std::ios::binary);
    std::ifstream tempFile("packed_temperatures.dat", std::ios::binary);

    int currentTimestamp = 0;
    int delta;
    bool withinRange = false;
    long long startOffset = -1, endOffset = -1, currentOffset = 0;

    while (deltaFile.read(reinterpret_cast<char*>(&delta), sizeof(delta))) {
        currentTimestamp += delta;
        if (currentTimestamp >= startTimestamp && startOffset == -1) {
            startOffset = currentOffset;
            withinRange = true;
        }
        if (currentTimestamp > endTimestamp && withinRange) {
            endOffset = currentOffset - 1;
            break;
        }
        currentOffset++;
    }

    //std::cout << "COMPRESSED COLUMN: " << startOffset << " " << endOffset << "\n";

    // Calculate bit positions and read the required section
    size_t startBitPos = startOffset * TEMPERATURE_BIT_WIDTH;
    size_t endBitPos = (endOffset + 1) * TEMPERATURE_BIT_WIDTH; // +1 to include the end offset
    size_t bufferSize = (endBitPos - startBitPos + BITS_PER_BYTE - 1) / BITS_PER_BYTE;

    std::vector<uint8_t> buffer(bufferSize);
    tempFile.seekg(startBitPos / BITS_PER_BYTE);
    tempFile.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
    tempFile.close();

    int sumTemperatures = 0;
    size_t count = 0;
    for (long long i = startOffset; i <= endOffset; ++i) {
        size_t bitIndex = (i - startOffset) * TEMPERATURE_BIT_WIDTH;
        int temperature = 0;
        for (int b = 0; b < TEMPERATURE_BIT_WIDTH; ++b) {
            if (buffer[(bitIndex + b) / BITS_PER_BYTE] & (1 << ((bitIndex + b) % BITS_PER_BYTE))) {
                temperature |= (1 << b);
            }
        }
        temperature += globalMinTemp;  // Adjust back based on encoding offset
        //std::cout << i << " : " << temperature << "\n";
        sumTemperatures += temperature;
        count++;
    }

    deltaFile.close();
    tempFile.close();

    return count > 0 ? static_cast<double>(sumTemperatures) / count : 0.0;
}

// Function to print the contents of a NEON vector for debugging
void print_neon_vector(uint16x8_t vec, const char* label) {
    uint16_t vals[8];
    vst1q_u16(vals, vec);
    std::cout << label << ": ";
    for (int i = 0; i < 8; ++i) {
        std::cout << vals[i] << " ";
    }
    std::cout << "\n";
}

double queryCompressedColumnarWithSIMD(int startTimestamp, int endTimestamp) {
    std::ifstream deltaFile("delta_encoded_timestamps.dat", std::ios::binary);
    std::ifstream tempFile("packed_temperatures.dat", std::ios::binary);

    int currentTimestamp = 0;
    int delta;
    bool withinRange = false;
    long long startOffset = -1, endOffset = -1, currentOffset = 0;

    while (deltaFile.read(reinterpret_cast<char*>(&delta), sizeof(delta))) {
        currentTimestamp += delta;
        if (currentTimestamp >= startTimestamp && startOffset == -1) {
            startOffset = currentOffset;
            withinRange = true;
        }
        if (currentTimestamp > endTimestamp && withinRange) {
            endOffset = currentOffset - 1;
            break;
        }
        currentOffset++;
    }

    if (startOffset == -1) return 0.0;
    if (endOffset == -1) endOffset = currentOffset - 1;

    size_t startBitPos = startOffset * TEMPERATURE_BIT_WIDTH;
    size_t endBitPos = (endOffset + 1) * TEMPERATURE_BIT_WIDTH;
    size_t bufferSize = (endBitPos - startBitPos + 7) / 8;

    std::vector<uint8_t> buffer(bufferSize);
    tempFile.seekg(startBitPos / 8, std::ios::beg);
    tempFile.read(reinterpret_cast<char*>(buffer.data()), bufferSize);

    // Initialize NEON vectors to hold the sums
    int32x4_t sum_vec = vdupq_n_s32(0);
    int32x4_t partial_sum_vec = vdupq_n_s32(0);
    int temperatures[4];  // Array to hold unpacked temperatures
    size_t count = 0;
    const int temps_per_loop = 4;  // Number of temperatures processed per loop iteration

    for (long long i = startOffset; i <= endOffset; i += temps_per_loop) {
        int32x4_t temp_vec = vdupq_n_s32(0);

        // Unpack temperatures from bit-packed buffer
        for (int j = 0; j < temps_per_loop && (i + j) <= endOffset; ++j) {
            size_t bitIndex = (i + j - startOffset) * TEMPERATURE_BIT_WIDTH;
            int temperature = 0;
            for (int b = 0; b < TEMPERATURE_BIT_WIDTH; ++b) {
                if (buffer[(bitIndex + b) / 8] & (1 << ((bitIndex + b) % 8))) {
                    temperature |= (1 << b);
                }
            }
            temperature += globalMinTemp;  // Adjust temperature based on encoding offset
            temperatures[j] = temperature;  // Store temperature in array
        }

        // Load unpacked temperatures into a NEON vector
        temp_vec = vld1q_s32(reinterpret_cast<int*>(temperatures));

        // Add the temperatures to the partial sum vector
        partial_sum_vec = vaddq_s32(partial_sum_vec, temp_vec);

        // Periodically accumulate partial sums to avoid overflow
        if ((i - startOffset) % (temps_per_loop * 1024) == 0) {
            sum_vec = vaddq_s32(sum_vec, partial_sum_vec);
            partial_sum_vec = vdupq_n_s32(0);  // Reset partial sum vector
        }

        count += temps_per_loop;  // Increment the count by the number of processed temperatures
    }

    // Add any remaining partial sums to the total sum
    sum_vec = vaddq_s32(sum_vec, partial_sum_vec);

    // Perform a horizontal add to sum the elements of the NEON vector
    int total_sum = vaddvq_s32(sum_vec);
    size_t total_count = count;

    // Print final sum for debugging
    //print_neon_vector(sum_vec, "Final sum_vec");

    deltaFile.close();
    tempFile.close();

    // Return the average temperature
    return total_count > 0 ? static_cast<double>(total_sum) / total_count : 0.0;
}


int main() {
    std::vector<std::vector<int>> data;
    generateData(data);
    storeData(data);
    storeCompressedColumnar(data);

    // Example start of query window
    int startTimestamp = START_TIMESTAMP + NUM_ROWS * QUERY_START_OFFSET;  
    // Example end of query window
    int endTimestamp = START_TIMESTAMP + NUM_ROWS * QUERY_END_OFFSET;  

    auto startRow = std::chrono::high_resolution_clock::now();
    double avgTempRow = queryAverageTemperatureRowStorage(startTimestamp, endTimestamp);
    auto endRow = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedRow = endRow - startRow;

    std::cout << "Average Temperature (Row Storage): " << avgTempRow << "°C\n";
    std::cout << "Query Time (Row Storage): " << elapsedRow.count() << " seconds\n";

    auto startCol = std::chrono::high_resolution_clock::now();
    double avgTempCol = queryAverageTemperatureColumnarStorage(startTimestamp, endTimestamp);
    auto endCol = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedCol = endCol - startCol;

    std::cout << "Average Temperature (Columnar Storage): " << avgTempCol << "°C\n";
    std::cout << "Query Time (Columnar Storage): " << elapsedCol.count() << " seconds\n";

    auto startCompressed = std::chrono::high_resolution_clock::now();
    double averageTempCompressed = queryCompressedColumnar(startTimestamp, endTimestamp);
    auto endCompressed = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedCompressed = endCompressed - startCompressed;

    std::cout << "Average temperature in compressed columnar storage: " << averageTempCompressed << "°C\n";
    std::cout << "Query time on compressed data: " << elapsedCompressed.count() << " seconds\n";

    auto startCompressedSIMD = std::chrono::high_resolution_clock::now();
    double averageTempCompressedSIMD = queryCompressedColumnarWithSIMD(startTimestamp, endTimestamp);
    auto endCompressedSIMD = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedCompressedSIMD = endCompressedSIMD - startCompressedSIMD;

    std::cout << "Average temperature in compressed columnar storage with SIMD: " << averageTempCompressedSIMD << "°C\n";
    std::cout << "Query time on compressed data with SIMD: " << elapsedCompressedSIMD.count() << " seconds\n";

    return 0;
}
