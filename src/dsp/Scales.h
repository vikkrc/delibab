#pragma once

#include <cmath>

namespace delibab
{
// Bitmask of allowed pitch classes per scale, bit 0 = root. Order matches
// scaleNames() in Parameters.cpp.
inline int scaleMask (int scaleIndex) noexcept
{
    static constexpr int masks[] {
        0b111111111111, // Chromatic
        0b101010110101, // Major          0 2 4 5 7 9 11
        0b010110101101, // Minor          0 2 3 5 7 8 10
        0b011010101101, // Dorian         0 2 3 5 7 9 10
        0b010110101011, // Phrygian       0 1 3 5 7 8 10
        0b101011010101, // Lydian         0 2 4 6 7 9 11
        0b011010110101, // Mixolydian     0 2 4 5 7 9 10
        0b100110101101, // Harmonic minor 0 2 3 5 7 8 11
        0b001010010101, // Pent. major    0 2 4 7 9
        0b010010101001, // Pent. minor    0 3 5 7 10
        0b010101010101, // Whole tone     0 2 4 6 8 10
        0b000010000001, // Fifths         0 7
        0b000000000001, // Octaves        0
    };
    constexpr int count = (int) (sizeof (masks) / sizeof (masks[0]));
    return masks[scaleIndex < 0 ? 0 : (scaleIndex >= count ? count - 1 : scaleIndex)];
}

// Snap a (fractional) MIDI pitch to the nearest note of the scale.
inline float quantizeToScale (float midiPitch, int key, int scaleIndex) noexcept
{
    const int mask = scaleMask (scaleIndex);
    if (mask == 0b111111111111)
        return std::round (midiPitch);

    const int centre = (int) std::lround (midiPitch);
    for (int distance = 0; distance <= 12; ++distance)
    {
        // Prefer the closer candidate; on a tie prefer the one below.
        const int candidates[2] { midiPitch - (float) (centre - distance) <= (float) (centre + distance) - midiPitch
                                      ? centre - distance : centre + distance,
                                  midiPitch - (float) (centre - distance) <= (float) (centre + distance) - midiPitch
                                      ? centre + distance : centre - distance };
        for (int c : candidates)
        {
            const int pc = ((c - key) % 12 + 12) % 12;
            if ((mask >> pc) & 1)
                return (float) c;
        }
    }
    return std::round (midiPitch);
}

} // namespace delibab
