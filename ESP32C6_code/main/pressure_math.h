#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pressure {
// Sensirion SDP8xx CRC-8: polynomial 0x31, initialization 0xFF, no reflection/XOR.
inline uint8_t crc8(const uint8_t *bytes, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint8_t>((crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1);
        }
    }
    return crc;
}

inline int32_t signed_word(const uint8_t *bytes)
{
    const uint16_t word = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    // Explicit two's-complement conversion avoids an unsigned negative-pressure bug.
    return (word & 0x8000) ? static_cast<int32_t>(word) - 65536 : word;
}

struct Reading {
    float pressure_pa = 0;
    float temperature_c = 0;
    int32_t scale_factor = 0;
};

inline bool decode(const uint8_t (&frame)[9], Reading &reading)
{
    for (size_t offset = 0; offset < 9; offset += 3) {
        if (crc8(frame + offset, 2) != frame[offset + 2]) {
            return false;
        }
    }
    const int32_t scale = signed_word(frame + 6);
    if (scale <= 0) {
        return false;
    }
    reading.pressure_pa = static_cast<float>(signed_word(frame)) / static_cast<float>(scale);
    reading.temperature_c = static_cast<float>(signed_word(frame + 3)) / 200.0f;
    reading.scale_factor = scale;
    return true;
}

// INTENTIONAL fake-unit hack: Matter normally describes this integer in 0.1 kPa.
// We send round(real differential Pa * 10), so HA's kPa-labelled NUMBER is Pa.
// Do not divide by 1000 or convert Pa to kPa here. Negative values stay signed.
// Limit the published value to the sensor's calibrated +/-125 Pa range.
inline int16_t to_matter(float pressure_pa)
{
    const float clamped = std::fmax(-125.0f, std::fmin(125.0f, pressure_pa));
    return static_cast<int16_t>(std::lround(clamped * 10.0f));
}

template <size_t N> class MovingAverage {
public:
    void reset() { count_ = next_ = 0; sum_ = 0; }
    float add(float value)
    {
        if (count_ == N) {
            sum_ -= values_[next_];
        } else {
            ++count_;
        }
        values_[next_] = value;
        sum_ += value;
        next_ = (next_ + 1) % N;
        return sum_ / static_cast<float>(count_);
    }
private:
    std::array<float, N> values_{};
    size_t count_ = 0;
    size_t next_ = 0;
    float sum_ = 0;
};
} // namespace pressure
