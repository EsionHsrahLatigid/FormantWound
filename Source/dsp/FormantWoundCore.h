#pragma once

#include <array>
#include <cstdint>

namespace formantwound::dsp
{
struct FormantWoundParameters
{
    float resolution = 0.55f;
    float excitation = 0.45f;
    float warp = 0.52f;
    bool freeze = false;
    float reseed = 0.0f;
    float damage = 0.35f;
    float feedback = 0.18f;
    float mix = 1.0f;
    float outputDb = 0.0f;
};

struct WoundSnapshot
{
    static constexpr int columns = 32;
    static constexpr int rows = 10;

    std::array<float, columns * rows> cells {};
    float inputRms = 0.0f;
    float wetRms = 0.0f;
    float residualRms = 0.0f;
    float envelopeMotion = 0.0f;
    int activeOrder = 0;
    bool rescue = false;
    bool frozen = false;
};

class FormantWoundCore final
{
public:
    void prepare(double newSampleRate, int channelSeed) noexcept;
    void reset() noexcept;
    float processSample(float input, const FormantWoundParameters& parameters) noexcept;
    void copySnapshot(WoundSnapshot& destination) const noexcept;

private:
    static constexpr int maxOrder = 16;
    static constexpr int historySize = 1024;
    static constexpr int analysisSize = 512;
    static constexpr int analysisHop = 64;
    static constexpr float pi = 3.14159265358979323846f;

    void analyzeFrame(const FormantWoundParameters& parameters) noexcept;
    void updateDisplay() noexcept;
    [[nodiscard]] int orderFromResolution(float resolution) const noexcept;
    [[nodiscard]] float nextNoise() noexcept;
    [[nodiscard]] float sanitize(float value) const noexcept;

    double sampleRate = 48000.0;
    std::array<float, historySize> inputHistory {};
    std::array<float, maxOrder> residualHistory {};
    std::array<float, maxOrder> synthesisHistory {};
    std::array<float, maxOrder> currentCoefficients {};
    std::array<float, maxOrder> targetCoefficients {};
    std::array<float, maxOrder> warpedCoefficients {};
    std::array<float, maxOrder + 1> autocorrelation {};
    std::array<float, maxOrder + 1> lpcWork {};
    std::array<float, WoundSnapshot::columns> envelopeHistory {};
    WoundSnapshot snapshot;
    std::uint32_t randomState = 0x4d3c2b1au;
    int writeIndex = 0;
    int samplesUntilAnalysis = 0;
    int impulseCountdown = 80;
    int activeOrder = 8;
    float inputFollower = 0.0f;
    float wetFollower = 0.0f;
    float residualFollower = 0.0f;
    float dcX = 0.0f;
    float dcY = 0.0f;
    float feedbackState = 0.0f;
    float previousReseed = -1.0f;
    float envelopeMotion = 0.0f;
    bool rescueActive = false;
};
} // namespace formantwound::dsp
