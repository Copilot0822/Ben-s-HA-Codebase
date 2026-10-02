#include "pressure_math.h"
#include <cassert>
#include <cstdio>

static void word(uint8_t *destination, int32_t value)
{
    const uint16_t bits = static_cast<uint16_t>(value);
    destination[0] = bits >> 8;
    destination[1] = bits & 0xFF;
    destination[2] = pressure::crc8(destination, 2);
}

int main()
{
    // Required mapping, both signs, limits, and rounding half away from zero.
    const float inputs[] = {-125, -5.27f, -1.04f, 0, 1.46f, 2.37f, 8.92f, 125, -0.05f, 0.05f, -140, 140};
    const int16_t expected[] = {-1250, -53, -10, 0, 15, 24, 89, 1250, -1, 1, -1250, 1250};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        assert(pressure::to_matter(inputs[i]) == expected[i]);
    }
    const uint8_t crc_example[] = {0xBE, 0xEF};
    assert(pressure::crc8(crc_example, 2) == 0x92); // Datasheet independent CRC check vector.
    for (int32_t raw : {-30000, -1265, -1, 0, 1, 1265, 30000}) {
        uint8_t frame[9];
        word(frame, raw);
        word(frame + 3, -4000);
        word(frame + 6, 240);
        pressure::Reading result;
        assert(pressure::decode(frame, result));
        assert(std::fabs(result.pressure_pa - raw / 240.0f) < 0.0001f);
        assert(result.temperature_c == -20.0f);
        for (size_t crc_index : {2u, 5u, 8u}) {
            frame[crc_index] ^= 1;
            assert(!pressure::decode(frame, result));
            frame[crc_index] ^= 1;
        }
        word(frame + 6, 0);
        assert(!pressure::decode(frame, result));
        word(frame + 6, -1);
        assert(!pressure::decode(frame, result));
    }
    pressure::MovingAverage<5> average;
    for (int i = 0; i < 5; ++i) assert(average.add(-2.0f) == -2.0f);
    for (int i = 0; i < 5; ++i) average.add(3.0f);
    assert(average.add(3.0f) == 3.0f);
    average.reset();
    assert(average.add(-5.0f) == -5.0f); // A recovered sensor does not blend stale history.
    std::puts("PASS: signed decoding, CRC corruption, scale validation, required mapping, filter recovery");
}
