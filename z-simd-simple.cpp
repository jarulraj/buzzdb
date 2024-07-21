#include <iostream>
#include <cstdlib>
#include <random>
#include <chrono>
#include <arm_neon.h>

const int NUM_READINGS = 1000000;  // Number of data points
const int START_TIMESTAMP = 1609459200;  // Example starting timestamp (2021-01-01)
const double QUERY_START_OFFSET = 0.6;
const double QUERY_END_OFFSET = 0.7;

// SensorData structure now uses SoA format
struct SensorData {
    int* timestamps;
    float* temperatures;

    SensorData(size_t count) {
        timestamps = static_cast<int*>(aligned_alloc(16, sizeof(int) * count));
        temperatures = static_cast<float*>(aligned_alloc(16, sizeof(float) * count));
    }

    ~SensorData() {
        free(timestamps);
        free(temperatures);
    }
};

void generateData(SensorData& data, int count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> temp_dist(25.0, 5.0);
    std::uniform_int_distribution<int> time_dist(1, 5);

    int timestamp = START_TIMESTAMP;
    for (int i = 0; i < count; ++i) {
        data.timestamps[i] = timestamp;
        data.temperatures[i] = temp_dist(gen);
        timestamp += time_dist(gen);
    }
}

float queryAverageTemperatureSIMD(const SensorData& data, int count, int startTimestamp, int endTimestamp) {
    // Initialize vectors to hold sum and count of temperatures in the query range
    float32x4_t sum_vec = vdupq_n_f32(0.0);  // Sum vector initialized to zero
    float32x4_t count_vec = vdupq_n_f32(0.0);  // Count vector initialized to zero

    // Loop over the data in steps of 4 to use SIMD operations
    for (int i = 0; i <= count - 4; i += 4) {
        // Load four timestamps from the sensor data into a 32-bit integer vector
        int32x4_t ts_vec = vld1q_s32(&data.timestamps[i]);
        // Load four temperatures from the sensor data into a 32-bit float vector
        float32x4_t temp_vec = vld1q_f32(&data.temperatures[i]);

        // Create masks to filter temperatures within the query time range
        uint32x4_t in_range_mask = vandq_u32(vcgeq_s32(ts_vec, vdupq_n_s32(startTimestamp)),
                                             vcleq_s32(ts_vec, vdupq_n_s32(endTimestamp)));

        // Apply the mask to the temperatures, keeping only those within the time range
        float32x4_t masked_temps = vmulq_f32(temp_vec, vcvtq_f32_u32(in_range_mask));
        // Sum up the masked temperatures
        sum_vec = vaddq_f32(sum_vec, masked_temps);
        // Count how many temperatures were added
        count_vec = vaddq_f32(count_vec, vcvtq_f32_u32(in_range_mask));
    }

    // Perform a horizontal addition to sum up all elements in the sum and count vectors
    float total_sum = vaddvq_f32(sum_vec);  // Sum up all elements in sum_vec
    float total_count = vaddvq_f32(count_vec);  // Sum up all elements in count_vec

    // Handle the remaining elements that were not processed by the SIMD loop
    for (int i = count - (count % 4); i < count; ++i) {
        // Check if the remaining timestamps are within the query range and process them
        if (data.timestamps[i] >= startTimestamp && data.timestamps[i] <= endTimestamp) {
            total_sum += data.temperatures[i];
            total_count += 1;
        }
    }

    // Calculate the average temperature if there are any valid counts, otherwise return zero
    return total_count > 0 ? total_sum / total_count : 0;
}


float queryAverageTemperatureNonSIMD(const SensorData& data, int count, int startTimestamp, int endTimestamp) {
    float sum = 0.0;
    int valid_count = 0;
    for (int i = 0; i < count; ++i) {
        if (data.timestamps[i] >= startTimestamp && data.timestamps[i] <= endTimestamp) {
            sum += data.temperatures[i];
            valid_count++;
        }
    }
    return valid_count > 0 ? sum / valid_count : 0;
}

int main() {
    SensorData data(NUM_READINGS);
    generateData(data, NUM_READINGS);

    int startTimestamp = START_TIMESTAMP + NUM_READINGS * QUERY_START_OFFSET;
    int endTimestamp = START_TIMESTAMP + NUM_READINGS * QUERY_END_OFFSET;

    std::cout << "First and last 5 data points:\n";
    for (int i = 0; i < 5; ++i) {
        std::cout << "Timestamp: " << data.timestamps[i] << ", Temperature: " << data.temperatures[i] << "\n";
    }
    for (int i = NUM_READINGS - 5; i < NUM_READINGS; ++i) {
        std::cout << "Timestamp: " << data.timestamps[i] << ", Temperature: " << data.temperatures[i] << "\n";
    }

    auto startSIMD = std::chrono::high_resolution_clock::now();
    float averageTemperatureSIMD = queryAverageTemperatureSIMD(data, NUM_READINGS, startTimestamp, endTimestamp);
    auto endSIMD = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedSIMD = endSIMD - startSIMD;

    auto startNonSIMD = std::chrono::high_resolution_clock::now();
    float averageTemperatureNonSIMD = queryAverageTemperatureNonSIMD(data, NUM_READINGS, startTimestamp, endTimestamp);
    auto endNonSIMD = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedNonSIMD = endNonSIMD - startNonSIMD;

    std::cout << "SIMD     Average Temperature: " << averageTemperatureSIMD << " C, Time: " << elapsedSIMD.count() << " seconds\n";
    std::cout << "Non-SIMD Average Temperature: " << averageTemperatureNonSIMD << " C, Time: " << elapsedNonSIMD.count() << " seconds\n";

    return 0;
}
