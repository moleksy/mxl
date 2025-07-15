#include "mxl/time.h"
#include "internal/Thread.hpp"
#include "internal/Timing.hpp"
#include <limits>
#include <cstdint>

namespace {
    // Maximum reasonable edit rate values to prevent overflow
    constexpr uint32_t MAX_REASONABLE_NUMERATOR = 1'000'000'000U;   // 1 billion
    constexpr uint32_t MAX_REASONABLE_DENOMINATOR = 1'000'000'000U; // 1 billion
    constexpr uint32_t MIN_REASONABLE_RATE_COMPONENT = 1U;
    
    // Maximum reasonable timestamp to prevent overflow (about 584 years in nanoseconds)
    constexpr uint64_t MAX_REASONABLE_TIMESTAMP = 18'446'744'073'709'551'615ULL / 2;
    
    // Helper function to validate edit rate parameters
    bool isValidEditRate(Rational const* editRate) noexcept {
        if (editRate == nullptr) {
            return false;
        }
        
        // Check for zero values
        if (editRate->denominator == 0 || editRate->numerator == 0) {
            return false;
        }
        
        // Check for reasonable ranges to prevent overflow
        if (editRate->numerator > MAX_REASONABLE_NUMERATOR ||
            editRate->denominator > MAX_REASONABLE_DENOMINATOR) {
            return false;
        }
        
        // Check for minimum reasonable values
        if (editRate->numerator < MIN_REASONABLE_RATE_COMPONENT ||
            editRate->denominator < MIN_REASONABLE_RATE_COMPONENT) {
            return false;
        }
        
        return true;
    }
    
    // Helper function to validate timestamp values
    bool isValidTimestamp(uint64_t timestamp) noexcept {
        // Check for maximum timestamp value (reserved for MXL_UNDEFINED_INDEX)
        if (timestamp == MXL_UNDEFINED_INDEX) {
            return false;
        }
        
        // Check for extremely large values that might cause overflow
        if (timestamp > MAX_REASONABLE_TIMESTAMP) {
            return false;
        }
        
        return true;
    }
    
    // Helper function to validate index values
    bool isValidIndex(uint64_t index) noexcept {
        // Check for maximum index value (reserved for MXL_UNDEFINED_INDEX)
        if (index == MXL_UNDEFINED_INDEX) {
            return false;
        }
        
        // Check for extremely large values that might cause overflow
        if (index > MAX_REASONABLE_TIMESTAMP) {
            return false;
        }
        
        return true;
    }
    
    // Helper function to safely cast 128-bit result to uint64_t with overflow detection
    uint64_t safecast128ToUint64(__int128_t value) noexcept {
        if (value < 0) {
            return MXL_UNDEFINED_INDEX;
        }
        
        if (value > std::numeric_limits<uint64_t>::max()) {
            return MXL_UNDEFINED_INDEX;
        }
        
        // Additional sanity check for reasonable values
        if (static_cast<uint64_t>(value) > MAX_REASONABLE_TIMESTAMP) {
            return MXL_UNDEFINED_INDEX;
        }
        
        return static_cast<uint64_t>(value);
    }
    
    // Helper function to validate the result of time operations
    bool isValidTimeResult(uint64_t result) noexcept {
        return result != MXL_UNDEFINED_INDEX && result <= MAX_REASONABLE_TIMESTAMP;
    }
}

extern "C"
MXL_EXPORT
uint64_t mxlGetTime()
{
    auto const timepoint = currentTime(mxl::lib::Clock::TAI);
    
    // Validate that we got a valid timepoint
    if (!timepoint) {
        return 0ULL; // Return 0 for invalid time rather than garbage
    }
    
    // Ensure the value is positive and reasonable
    if (timepoint.value < 0) {
        return 0ULL;
    }
    
    uint64_t result = static_cast<uint64_t>(timepoint.value);
    
    // Additional sanity check for reasonable timestamp values
    if (result > MAX_REASONABLE_TIMESTAMP) {
        return 0ULL; // Return 0 for unreasonable timestamps
    }
    
    return result;
}

extern "C"
MXL_EXPORT
uint64_t mxlGetCurrentIndex(Rational const* editRate)
{
    if (!isValidEditRate(editRate)) {
        return MXL_UNDEFINED_INDEX;
    }

    auto const now = mxlGetTime();
    if (now == 0ULL) {
        return MXL_UNDEFINED_INDEX;
    }
    
    return mxlTimestampToIndex(editRate, now);
}

extern "C"
MXL_EXPORT
uint64_t mxlTimestampToIndex(Rational const* editRate, uint64_t timestamp)
{
    if (!isValidEditRate(editRate)) {
        return MXL_UNDEFINED_INDEX;
    }

    // Validate timestamp
    if (!isValidTimestamp(timestamp)) {
        return MXL_UNDEFINED_INDEX;
    }
    
    // Perform calculation with overflow protection
    __int128_t const numerator = static_cast<__int128_t>(timestamp) * editRate->numerator;
    __int128_t const rounding = 500'000'000LL * editRate->denominator;
    __int128_t const denominator = 1'000'000'000LL * editRate->denominator;
    
    // Check for intermediate overflow
    if (numerator < 0 || rounding < 0 || denominator <= 0) {
        return MXL_UNDEFINED_INDEX;
    }
    
    // Check for potential division overflow
    if (numerator > std::numeric_limits<__int128_t>::max() - rounding) {
        return MXL_UNDEFINED_INDEX;
    }
    
    __int128_t const result = (numerator + rounding) / denominator;
    
    return safecast128ToUint64(result);
}

extern "C"
MXL_EXPORT
uint64_t mxlIndexToTimestamp(Rational const* editRate, uint64_t index)
{
    if (!isValidEditRate(editRate)) {
        return MXL_UNDEFINED_INDEX;
    }

    // Validate index
    if (!isValidIndex(index)) {
        return MXL_UNDEFINED_INDEX;
    }
    
    // Perform calculation with overflow protection
    __int128_t const numerator = static_cast<__int128_t>(index) * editRate->denominator * 1'000'000'000LL;
    __int128_t const rounding = static_cast<__int128_t>(editRate->numerator) / 2;
    __int128_t const denominator = static_cast<__int128_t>(editRate->numerator);
    
    // Check for intermediate overflow and valid denominator
    if (numerator < 0 || rounding < 0 || denominator <= 0) {
        return MXL_UNDEFINED_INDEX;
    }
    
    // Check for potential addition overflow
    if (numerator > std::numeric_limits<__int128_t>::max() - rounding) {
        return MXL_UNDEFINED_INDEX;
    }
    
    __int128_t const result = (numerator + rounding) / denominator;
    
    return safecast128ToUint64(result);
}

extern "C"
MXL_EXPORT
uint64_t mxlGetNsUntilIndex(uint64_t index, Rational const* editRate)
{
    if (!isValidEditRate(editRate)) {
        return MXL_UNDEFINED_INDEX;
    }

    // Validate index
    if (!isValidIndex(index)) {
        return MXL_UNDEFINED_INDEX;
    }
    
    auto const targetNs = mxlIndexToTimestamp(editRate, index);
    if (targetNs == MXL_UNDEFINED_INDEX) {
        return MXL_UNDEFINED_INDEX;
    }
    
    auto const nowNs = mxlGetTime();
    if (nowNs == 0ULL) {
        return MXL_UNDEFINED_INDEX;
    }
    
    // Handle the case where target time is in the past
    if (targetNs < nowNs) {
        return 0ULL;
    }
    
    // Check for subtraction overflow (should not happen with uint64_t, but be safe)
    uint64_t const diff = targetNs - nowNs;
    if (diff > MAX_REASONABLE_TIMESTAMP) {
        return MXL_UNDEFINED_INDEX;
    }
    
    return diff;
}

extern "C"
MXL_EXPORT
void mxlSleepForNs(uint64_t ns)
{
    // Validate input - extremely large values might cause issues
    if (ns > std::numeric_limits<int64_t>::max()) {
        // Cap at maximum reasonable sleep duration (about 292 years)
        ns = std::numeric_limits<int64_t>::max();
    }
    
    // Handle the case where ns is 0 (immediate return)
    if (ns == 0) {
        return;
    }
    
    try {
        mxl::lib::this_thread::sleep(mxl::lib::Duration(static_cast<int64_t>(ns)), mxl::lib::Clock::TAI);
    } catch (...) {
        // If sleep fails, we silently continue - this is a best-effort function
        // Alternative: could implement fallback sleep mechanism
    }
}
