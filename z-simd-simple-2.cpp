#include <iostream>
#include <cstdlib>
#include <random>
#include <chrono>
#include <arm_neon.h>

const int NUM_READINGS = 1000000;  // Number of data points
const int MAX_LOCATION_ID = 1000;  // Example maximum location ID

struct SensorData {
    int* location_ids;
    uint16_t* temperatures;

    SensorData(size_t count) {
        location_ids = static_cast<int*>(aligned_alloc(16, sizeof(int) * count));
        temperatures = static_cast<uint16_t*>(aligned_alloc(16, sizeof(uint16_t) * count));
    }

    ~SensorData() {
        free(location_ids);
        free(temperatures);
    }
};

void generateData(SensorData& data, int count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> temp_dist(25.0, 5.0);
    std::uniform_int_distribution<int> location_dist(1, MAX_LOCATION_ID);

    for (int i = 0; i < count; ++i) {
        data.location_ids[i] = location_dist(gen);
        data.temperatures[i] = static_cast<uint16_t>(temp_dist(gen));
    }
}

float queryAverageTemperatureSIMD(const SensorData& data, int count, int locationID) {
    // Initialize vectors to hold sum and count of temperatures in the query range
    float32x4_t sum_vec = vdupq_n_f32(0.0);  // Sum vector initialized to zero
    float32x4_t count_vec = vdupq_n_f32(0.0);  // Count vector initialized to zero

    // Loop over the data in steps of 8 to use SIMD operations
    for (int i = 0; i <= count - 8; i += 8) {
        // Load eight location IDs from the sensor data into two 32-bit integer vectors
        int32x4_t loc_vec_1 = vld1q_s32(&data.location_ids[i]);
        int32x4_t loc_vec_2 = vld1q_s32(&data.location_ids[i + 4]);

        // Load eight temperatures from the sensor data into a 16-bit integer vector
        uint16x8_t temp_vec = vld1q_u16(&data.temperatures[i]);

        // Create masks to filter temperatures for the specific location
        uint32x4_t in_range_mask_1 = vceqq_s32(loc_vec_1, vdupq_n_s32(locationID));
        uint32x4_t in_range_mask_2 = vceqq_s32(loc_vec_2, vdupq_n_s32(locationID));

        // Apply the mask to the temperatures, keeping only those for the specific location
        uint16x4_t masked_temps_1 = vbsl_u16(vmovn_u32(in_range_mask_1), vget_low_u16(temp_vec), vdup_n_u16(0));
        uint16x4_t masked_temps_2 = vbsl_u16(vmovn_u32(in_range_mask_2), vget_high_u16(temp_vec), vdup_n_u16(0));

        // Combine the masked temperatures
        uint16x8_t masked_temps = vcombine_u16(masked_temps_1, masked_temps_2);

        // Sum up the masked temperatures
        float32x4_t temp_low = vcvtq_f32_u32(vmovl_u16(vget_low_u16(masked_temps)));
        float32x4_t temp_high = vcvtq_f32_u32(vmovl_u16(vget_high_u16(masked_temps)));
        sum_vec = vaddq_f32(sum_vec, temp_low);
        sum_vec = vaddq_f32(sum_vec, temp_high);

        // Count how many temperatures were added
        count_vec = vaddq_f32(count_vec, vcvtq_f32_u32(vmovl_u16(masked_temps_1)));
        count_vec = vaddq_f32(count_vec, vcvtq_f32_u32(vmovl_u16(masked_temps_2)));
    }

    // Perform a horizontal addition to sum up all elements in the sum and count vectors
    float total_sum = vaddvq_f32(sum_vec);  // Sum up all elements in sum_vec
    float total_count = vaddvq_f32(count_vec);  // Sum up all elements in count_vec

    // Handle the remaining elements that were not processed by the SIMD loop
    for (int i = count - (count % 8); i < count; ++i) {
        // Check if the remaining location IDs match the query location and process them
        if (data.location_ids[i] == locationID) {
            total_sum += data.temperatures[i];
            total_count += 1;
        }
    }

    // Calculate the average temperature if there are any valid counts, otherwise return zero
    return total_count > 0 ? total_sum / total_count : 0;
}

float queryAverageTemperatureNonSIMD(const SensorData& data, int count, int locationID) {
    float sum = 0.0;
    int valid_count = 0;
    for (int i = 0; i < count; ++i) {
        if (data.location_ids[i] == locationID) {
            sum += data.temperatures[i];
            valid_count++;
        }
    }
    return valid_count > 0 ? sum / valid_count : 0;
}

int main() {
    SensorData data(NUM_READINGS);
    generateData(data, NUM_READINGS);

    int locationID = MAX_LOCATION_ID / 2;  // Example location ID for querying

    std::cout << "First and last 5 data points:\n";
    for (int i = 0; i < 5; ++i) {
        std::cout << "Location ID: " << data.location_ids[i] << ", Temperature: " << data.temperatures[i] << "\n";
    }
    for (int i = NUM_READINGS - 5; i < NUM_READINGS; ++i) {
        std::cout << "Location ID: " << data.location_ids[i] << ", Temperature: " << data.temperatures[i] << "\n";
    }

    auto startSIMD = std::chrono::high_resolution_clock::now();
    float averageTemperatureSIMD = queryAverageTemperatureSIMD(data, NUM_READINGS, locationID);
    auto endSIMD = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedSIMD = endSIMD - startSIMD;

    auto startNonSIMD = std::chrono::high_resolution_clock::now();
    float averageTemperatureNonSIMD = queryAverageTemperatureNonSIMD(data, NUM_READINGS, locationID);
    auto endNonSIMD = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedNonSIMD = endNonSIMD - startNonSIMD;

    std::cout << "SIMD     Average Temperature: " << averageTemperatureSIMD << " C, Time: " << elapsedSIMD.count() << " seconds\n";
    std::cout << "Non-SIMD Average Temperature: " << averageTemperatureNonSIMD << " C, Time: " << elapsedNonSIMD.count() << " seconds\n";

    return 0;
}
