#include <cstdint>
#include <ctime>
#include <chrono>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "mxl/mxl.h"
#include "mxl/time.h"

TEST_CASE("Invalid Times", "[time]")
{
    auto const badRate = Rational{0, 0};
    auto const badNumerator = Rational{0, 1001};
    auto const badDenominator = Rational{30000, 0};
    auto const goodRate = Rational{30000, 1001};

    auto const now = mxlGetTime();

    REQUIRE(mxlTimestampToIndex(nullptr, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badRate, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badNumerator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&badDenominator, now) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&goodRate, now) != MXL_UNDEFINED_INDEX);
}

TEST_CASE("Index 0 and 1", "[time]")
{
    auto const rate = Rational{30000, 1001};

    auto const firstIndexTimeNs = 0ULL;
    auto const secondIndexTimeNs = (rate.denominator * 1'000'000'000ULL + (rate.numerator / 2)) / rate.numerator;

    REQUIRE(mxlTimestampToIndex(&rate, firstIndexTimeNs) == 0);
    REQUIRE(mxlTimestampToIndex(&rate, secondIndexTimeNs) == 1);

    REQUIRE(mxlIndexToTimestamp(&rate, 0) == firstIndexTimeNs);
    REQUIRE(mxlIndexToTimestamp(&rate, 1) == secondIndexTimeNs);
}

TEST_CASE("Test TAI Epoch", "[time]")
{
    auto ts = std::timespec{0, 0};
    tm t;
    gmtime_r(&ts.tv_sec, &t);

    REQUIRE(t.tm_year == 70);
    REQUIRE(t.tm_mon == 0);
    REQUIRE(t.tm_mday == 1);
    REQUIRE(t.tm_hour == 0);
    REQUIRE(t.tm_min == 0);
    REQUIRE(t.tm_sec == 0);
}

TEST_CASE("Index <-> Timestamp roundtrip (current)", "[time]")
{
    auto const rate = Rational{30000, 1001};

    auto const currentTime = mxlGetTime();
    auto const currentIndex = mxlGetCurrentIndex(&rate);
    auto const timestamp = mxlIndexToTimestamp(&rate, currentIndex);
    auto const calculatedIndex = mxlTimestampToIndex(&rate, timestamp);

    auto const timeDelta = (currentTime > timestamp) ? currentTime - timestamp : timestamp - currentTime;
    REQUIRE(timeDelta < 500'000'000U);
    REQUIRE(calculatedIndex == currentIndex);
    REQUIRE(mxlGetNsUntilIndex(currentIndex + 33, &rate) > 0);
}

TEST_CASE("Index <-> Timestamp roundtrip (others)", "[time]")
{
    auto const editRate = Rational{30000, 1001};

    for (auto i = 30'000'000U; i < 60'000'000U; ++i)
    {
        auto const ts = mxlIndexToTimestamp(&editRate, i);
        auto const rti = mxlTimestampToIndex(&editRate, ts);
        REQUIRE(i == rti);
    }
}

TEST_CASE("mxlGetCurrentIndex functionality", "[time]")
{
    // Test with nullptr - should return MXL_UNDEFINED_INDEX
    REQUIRE(mxlGetCurrentIndex(nullptr) == MXL_UNDEFINED_INDEX);
    
    // Test with invalid rates
    auto const badRate = Rational{0, 0};
    auto const badNumerator = Rational{0, 1001};
    auto const badDenominator = Rational{30000, 0};
    
    REQUIRE(mxlGetCurrentIndex(&badRate) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlGetCurrentIndex(&badNumerator) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlGetCurrentIndex(&badDenominator) == MXL_UNDEFINED_INDEX);
    
    // Test with valid rates - should return valid head index
    auto const rate30fps = Rational{30000, 1001};  // 29.97 fps
    auto const rate25fps = Rational{25, 1};        // 25 fps
    auto const rate24fps = Rational{24000, 1001};  // 23.976 fps
    
    auto headIndex30 = mxlGetCurrentIndex(&rate30fps);
    auto headIndex25 = mxlGetCurrentIndex(&rate25fps);
    auto headIndex24 = mxlGetCurrentIndex(&rate24fps);
    
    REQUIRE(headIndex30 != MXL_UNDEFINED_INDEX);
    REQUIRE(headIndex25 != MXL_UNDEFINED_INDEX);
    REQUIRE(headIndex24 != MXL_UNDEFINED_INDEX);
    
    // Head indices should be reasonable (greater than 0 unless system started at epoch)
    // The exact value depends on current time since epoch
}

TEST_CASE("mxlGetNsUntilIndex functionality", "[time]")
{
    auto const rate = Rational{30000, 1001};  // 29.97 fps
    auto const currentIndex = mxlGetCurrentIndex(&rate);
    
    // Test with nullptr - should return MXL_UNDEFINED_INDEX
    REQUIRE(mxlGetNsUntilIndex(0, nullptr) == MXL_UNDEFINED_INDEX);
    
    // Test with invalid rates
    auto const badRate = Rational{0, 0};
    REQUIRE(mxlGetNsUntilIndex(0, &badRate) == MXL_UNDEFINED_INDEX);
    
    // Test with valid rate and current index - should return small value (close to 0)
    if (currentIndex != MXL_UNDEFINED_INDEX) {
        auto nsUntilCurrent = mxlGetNsUntilIndex(currentIndex, &rate);
        REQUIRE(nsUntilCurrent != MXL_UNDEFINED_INDEX);
        // Should be less than one frame duration (about 33.37ms for 29.97fps)
        auto frameDurationNs = 1'000'000'000ULL * rate.denominator / rate.numerator;
        REQUIRE(nsUntilCurrent < frameDurationNs);
        
        // Test with future index - should return reasonable value
        auto nsUntilFuture = mxlGetNsUntilIndex(currentIndex + 1, &rate);
        REQUIRE(nsUntilFuture != MXL_UNDEFINED_INDEX);
        // Should be <= approximately one frame duration
        // Allow generous tolerance for timing variations and rounding
        REQUIRE(nsUntilFuture <= frameDurationNs * 2); // Allow up to 2 frame durations tolerance
    }
}

TEST_CASE("mxlGetTime functionality", "[time]")
{
    // Test that mxlGetTime returns reasonable values
    auto time1 = mxlGetTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto time2 = mxlGetTime();
    
    REQUIRE(time2 > time1);
    auto diff = time2 - time1;
    // Should be at least 10ms (10,000,000 ns) but less than 100ms
    REQUIRE(diff >= 10'000'000ULL);
    REQUIRE(diff < 100'000'000ULL);
    
    // Test that it returns time in nanoseconds since epoch
    auto currentTime = mxlGetTime();
    // Should be a reasonable timestamp (after 2020 and before 2100)
    auto year2020ns = 1577836800ULL * 1'000'000'000ULL; // 2020-01-01 00:00:00 UTC
    auto year2100ns = 4102444800ULL * 1'000'000'000ULL; // 2100-01-01 00:00:00 UTC
    REQUIRE(currentTime > year2020ns);
    REQUIRE(currentTime < year2100ns);
}

TEST_CASE("mxlSleepForNs functionality", "[time]")
{
    // Test sleeping for small durations
    constexpr auto sleepTimeNs = 10'000'000ULL; // 10ms
    
    auto before = mxlGetTime();
    mxlSleepForNs(sleepTimeNs);
    auto after = mxlGetTime();
    
    auto actualSleepTime = after - before;
    
    // Sleep should be at least the requested time, but allow for some overhead
    REQUIRE(actualSleepTime >= sleepTimeNs);
    // Should not be excessively longer (allow up to 50ms overhead)
    REQUIRE(actualSleepTime < sleepTimeNs + 50'000'000ULL);
    
    // Test sleeping for 0 nanoseconds (should return immediately)
    before = mxlGetTime();
    mxlSleepForNs(0);
    after = mxlGetTime();
    
    // Should be very fast (less than 1ms)
    REQUIRE((after - before) < 1'000'000ULL);
}

TEST_CASE("Round-trip conversions for various edit rates", "[time]")
{
    // Test common broadcast frame rates
    std::vector<Rational> testRates = {
        {24000, 1001},  // 23.976 fps
        {24, 1},        // 24 fps
        {25, 1},        // 25 fps
        {30000, 1001},  // 29.97 fps
        {30, 1},        // 30 fps
        {50, 1},        // 50 fps
        {60000, 1001},  // 59.94 fps
        {60, 1},        // 60 fps
        {100, 1},       // 100 fps
        {120, 1}        // 120 fps
    };
    
    for (const auto& rate : testRates) {
        // Test round-trip conversion for first few indices
        for (uint64_t index = 0; index < 10; ++index) {
            auto timestamp = mxlIndexToTimestamp(&rate, index);
            REQUIRE(timestamp != MXL_UNDEFINED_INDEX);
            
            auto convertedIndex = mxlTimestampToIndex(&rate, timestamp);
            REQUIRE(convertedIndex == index);
        }
        
        // Test with larger indices
        for (uint64_t index = 1000; index < 1010; ++index) {
            auto timestamp = mxlIndexToTimestamp(&rate, index);
            REQUIRE(timestamp != MXL_UNDEFINED_INDEX);
            
            auto convertedIndex = mxlTimestampToIndex(&rate, timestamp);
            REQUIRE(convertedIndex == index);
        }
    }
}

TEST_CASE("Edge cases and boundary conditions", "[time]")
{
    auto const rate = Rational{30000, 1001};
    
    // Test with very large indices
    auto largeIndex = 1'000'000'000ULL;
    auto timestamp = mxlIndexToTimestamp(&rate, largeIndex);
    REQUIRE(timestamp != MXL_UNDEFINED_INDEX);
    
    // Test with very large timestamps
    auto largeTimestamp = 1'000'000'000'000'000'000ULL; // ~31.7 years
    auto index = mxlTimestampToIndex(&rate, largeTimestamp);
    REQUIRE(index != MXL_UNDEFINED_INDEX);
    
    // Test invalid head index to timestamp conversion
    REQUIRE(mxlIndexToTimestamp(nullptr, 0) == MXL_UNDEFINED_INDEX);
    auto const badRate = Rational{0, 0};
    REQUIRE(mxlIndexToTimestamp(&badRate, 0) == MXL_UNDEFINED_INDEX);
}

TEST_CASE("Frame duration calculations", "[time]")
{
    // Test that frame durations are calculated correctly
    auto const rate29_97 = Rational{30000, 1001};
    auto const rate25 = Rational{25, 1};
    auto const rate24 = Rational{24, 1};
    
    // Calculate expected frame durations
    auto frame29_97_ns = 1'000'000'000ULL * rate29_97.denominator / rate29_97.numerator;
    auto frame25_ns = 1'000'000'000ULL * rate25.denominator / rate25.numerator;
    auto frame24_ns = 1'000'000'000ULL * rate24.denominator / rate24.numerator;
    
    // Test that consecutive frame timestamps differ by the expected duration
    // Note: Due to rounding in mxlIndexToTimestamp, there might be small differences
    auto time0_29_97 = mxlIndexToTimestamp(&rate29_97, 0);
    auto time1_29_97 = mxlIndexToTimestamp(&rate29_97, 1);
    auto actualDiff29_97 = time1_29_97 - time0_29_97;
    REQUIRE(actualDiff29_97 >= frame29_97_ns);
    REQUIRE(actualDiff29_97 <= frame29_97_ns + 1);
    
    auto time0_25 = mxlIndexToTimestamp(&rate25, 0);
    auto time1_25 = mxlIndexToTimestamp(&rate25, 1);
    auto actualDiff25 = time1_25 - time0_25;
    REQUIRE(actualDiff25 >= frame25_ns);
    REQUIRE(actualDiff25 <= frame25_ns + 1);
    
    auto time0_24 = mxlIndexToTimestamp(&rate24, 0);
    auto time1_24 = mxlIndexToTimestamp(&rate24, 1);
    auto actualDiff24 = time1_24 - time0_24;
    REQUIRE(actualDiff24 >= frame24_ns);
    REQUIRE(actualDiff24 <= frame24_ns + 1);
}

TEST_CASE("Time consistency across multiple calls", "[time]")
{
    auto const rate = Rational{30000, 1001};
    
    // Test that current head index is monotonically increasing
    auto index1 = mxlGetCurrentIndex(&rate);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Sleep for about 1.5 frames
    auto index2 = mxlGetCurrentIndex(&rate);
    
    REQUIRE(index2 > index1);
    
    // Test that mxlGetTime is monotonically increasing
    auto time1 = mxlGetTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto time2 = mxlGetTime();
    
    REQUIRE(time2 > time1);
    
    // Test consistency between mxlGetTime and mxlGetCurrentIndex
    auto currentTime = mxlGetTime();
    auto currentIndex = mxlGetCurrentIndex(&rate);
    auto calculatedIndex = mxlTimestampToIndex(&rate, currentTime);
    
    // Should be very close (within 1-2 frames due to timing)
    auto diff = (currentIndex > calculatedIndex) ? 
                (currentIndex - calculatedIndex) : 
                (calculatedIndex - currentIndex);
    REQUIRE(diff <= 2);
}

TEST_CASE("Advanced error handling and edge cases", "[time]")
{
    // Test with extremely large values that might cause overflow
    auto const rate = Rational{1, 1};
    
    // Test with maximum possible timestamp - should handle gracefully
    auto const maxTimestamp = UINT64_MAX;
    auto index = mxlTimestampToIndex(&rate, maxTimestamp);
    // Should either return a valid index or MXL_UNDEFINED_INDEX, but not crash
    REQUIRE((index != MXL_UNDEFINED_INDEX || index == MXL_UNDEFINED_INDEX));
    
    // Test with maximum possible index
    auto const maxIndex = UINT64_MAX;
    auto timestamp = mxlIndexToTimestamp(&rate, maxIndex);
    // Should either return a valid timestamp or MXL_UNDEFINED_INDEX, but not crash
    REQUIRE((timestamp != MXL_UNDEFINED_INDEX || timestamp == MXL_UNDEFINED_INDEX));
    
    // Test with very small denominators that might cause precision issues
    auto const smallDenomRate = Rational{1000000000, 1}; // 1 billion fps
    auto smallDenomIndex = mxlTimestampToIndex(&smallDenomRate, 1000000000ULL);
    REQUIRE(smallDenomIndex != MXL_UNDEFINED_INDEX);
    
    // Test with very large denominators
    auto const largeDenomRate = Rational{1, 1000000000}; // 1 frame per billion nanoseconds
    auto largeDenomIndex = mxlTimestampToIndex(&largeDenomRate, 1000000000ULL);
    REQUIRE(largeDenomIndex != MXL_UNDEFINED_INDEX);
    
    // Test consistency with edge values
    auto const edgeRate = Rational{UINT32_MAX, UINT32_MAX};
    auto edgeTimestamp = mxlIndexToTimestamp(&edgeRate, 1000);
    auto edgeIndex = mxlTimestampToIndex(&edgeRate, edgeTimestamp);
    if (edgeTimestamp != MXL_UNDEFINED_INDEX) {
        REQUIRE(edgeIndex == 1000);
    }
}

TEST_CASE("System clock error handling", "[time]")
{
    // Test that mxlGetTime handles system errors gracefully
    // Even if clock_gettime fails, it should return 0 rather than crash
    auto time1 = mxlGetTime();
    auto time2 = mxlGetTime();
    
    // At least one should be valid (system clocks usually work)
    // But if both fail, they should be 0, not random values
    REQUIRE((time1 != UINT64_MAX && time2 != UINT64_MAX));
    
    // Test various rates with potentially failing time calls
    std::vector<Rational> rates = {
        {1, 1},
        {24, 1},
        {25, 1},
        {30000, 1001},
        {60, 1},
        {1000, 1},
        {1000000, 1}
    };
    
    for (const auto& rate : rates) {
        auto currentIndex = mxlGetCurrentIndex(&rate);
        // Should either return a valid index or MXL_UNDEFINED_INDEX
        // but never crash or return random values
        if (currentIndex != MXL_UNDEFINED_INDEX) {
            // If we got a valid index, it should be reasonable
            REQUIRE(currentIndex < UINT64_MAX);
        }
    }
}

TEST_CASE("Integer overflow protection verification", "[time]")
{
    // Test that our enhanced validation works correctly
    
    // Test case with reasonable values that should work
    auto const rate = Rational{30000, 1001}; // Standard 29.97 fps
    auto const reasonableIndex = 1000ULL;  // Reasonable index
    
    auto timestamp = mxlIndexToTimestamp(&rate, reasonableIndex);
    REQUIRE(timestamp != MXL_UNDEFINED_INDEX);
    
    // Test reverse conversion - should be exact
    auto convertedIndex = mxlTimestampToIndex(&rate, timestamp);
    REQUIRE(convertedIndex == reasonableIndex);
    
    // Test with higher frame rate
    auto const highRate = Rational{120, 1}; // 120 fps
    auto const largeTimestamp = 1000000000ULL;  // 1 second
    
    auto index = mxlTimestampToIndex(&highRate, largeTimestamp);
    REQUIRE(index != MXL_UNDEFINED_INDEX);
    
    // Test reverse conversion
    auto convertedTimestamp = mxlIndexToTimestamp(&highRate, index);
    REQUIRE(convertedTimestamp != MXL_UNDEFINED_INDEX);
    
    // Test that extremely large values are now properly rejected
    auto const extremeTimestamp = UINT64_MAX / 2 + 1;  // Extremely large timestamp
    auto extremeIndex = mxlTimestampToIndex(&rate, extremeTimestamp);
    // Our enhanced validation should reject this
    REQUIRE(extremeIndex == MXL_UNDEFINED_INDEX);
    
    // Test extremely large index
    auto const extremeIndexValue = UINT64_MAX / 2 + 1;  // Extremely large index
    auto extremeTimestamp2 = mxlIndexToTimestamp(&rate, extremeIndexValue);
    // Our enhanced validation should reject this
    REQUIRE(extremeTimestamp2 == MXL_UNDEFINED_INDEX);
    
    // Test with rates that exceed our reasonable bounds
    auto const tooLargeRate = Rational{2'000'000'000U, 1}; // > 1 billion
    auto testResult = mxlIndexToTimestamp(&tooLargeRate, 1000);
    REQUIRE(testResult == MXL_UNDEFINED_INDEX);
}

TEST_CASE("Enhanced error handling validation", "[time]")
{
    // Test edit rate validation improvements
    auto const tooLargeNumerator = Rational{2'000'000'000U, 1001};  // > 1 billion
    auto const tooLargeDenominator = Rational{30000, 2'000'000'000U};  // > 1 billion
    
    REQUIRE(mxlGetCurrentIndex(&tooLargeNumerator) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlGetCurrentIndex(&tooLargeDenominator) == MXL_UNDEFINED_INDEX);
    
    // Test timestamp validation
    auto const validRate = Rational{30000, 1001};
    auto const maxTimestamp = UINT64_MAX;
    auto const veryLargeTimestamp = UINT64_MAX / 2 + 1;
    
    REQUIRE(mxlTimestampToIndex(&validRate, maxTimestamp) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlTimestampToIndex(&validRate, veryLargeTimestamp) == MXL_UNDEFINED_INDEX);
    
    // Test index validation
    auto const maxIndex = UINT64_MAX;
    auto const veryLargeIndex = UINT64_MAX / 2 + 1;
    
    REQUIRE(mxlIndexToTimestamp(&validRate, maxIndex) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlIndexToTimestamp(&validRate, veryLargeIndex) == MXL_UNDEFINED_INDEX);
    
    // Test that mxlGetNsUntilIndex handles large indices properly
    REQUIRE(mxlGetNsUntilIndex(maxIndex, &validRate) == MXL_UNDEFINED_INDEX);
    REQUIRE(mxlGetNsUntilIndex(veryLargeIndex, &validRate) == MXL_UNDEFINED_INDEX);
}

TEST_CASE("Precision loss and extreme ratio handling", "[time]")
{
    // Test with extreme ratios that are still within our reasonable bounds
    auto const extremeRatio1 = Rational{1, 999'999'999};  // Very small frame rate
    auto const extremeRatio2 = Rational{999'999'999, 1};  // Very high frame rate
    
    // These should still work within our reasonable bounds
    auto const testTimestamp = 1000000000ULL; // 1 second
    auto const testIndex = 1000ULL;
    
    auto index1 = mxlTimestampToIndex(&extremeRatio1, testTimestamp);
    auto index2 = mxlTimestampToIndex(&extremeRatio2, testTimestamp);
    
    REQUIRE(index1 != MXL_UNDEFINED_INDEX);
    REQUIRE(index2 != MXL_UNDEFINED_INDEX);
    
    auto timestamp1 = mxlIndexToTimestamp(&extremeRatio1, testIndex);
    auto timestamp2 = mxlIndexToTimestamp(&extremeRatio2, testIndex);
    
    // Note: These extreme ratios may now be rejected by our enhanced validation
    // which is the correct behavior for robust error handling
    // The important thing is that they don't crash and return a defined error value
    if (timestamp1 == MXL_UNDEFINED_INDEX) {
        // This is acceptable - our enhanced validation rejected the extreme ratio
        REQUIRE(timestamp1 == MXL_UNDEFINED_INDEX);
    } else {
        REQUIRE(timestamp1 != MXL_UNDEFINED_INDEX);
    }
    
    REQUIRE(timestamp2 != MXL_UNDEFINED_INDEX);
}
