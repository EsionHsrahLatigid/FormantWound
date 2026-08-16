#include "FormantWoundCore.h"

#include <algorithm>
#include <cmath>

namespace formantwound::dsp
{
namespace
{
float clamp01(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

float softClip(float value) noexcept
{
    if (! std::isfinite(value))
        return 0.0f;
    return std::tanh(value);
}

float dbToGain(float db) noexcept
{
    if (! std::isfinite(db))
        db = 0.0f;
    return std::pow(10.0f, std::clamp(db, -36.0f, 18.0f) / 20.0f);
}
} // namespace

void FormantWoundCore::prepare(double newSampleRate, int channelSeed) noexcept
{
    sampleRate = std::isfinite(newSampleRate) && newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    analysisFrameSamples = samplesForSeconds(512.0 / 48000.0, 128, historySize);
    analysisHopSamples = samplesForSeconds(64.0 / 48000.0, 8, 256);
    displayHopSamples = samplesForSeconds(1.0 / 30.0, 32, 8192);
    coefficientSmoothing = onePoleCoefficient(0.00835);
    followerSmoothing = onePoleCoefficient(0.0139);
    dcCoefficient = static_cast<float>(std::exp(-2.0 * static_cast<double>(pi) * 38.0 / sampleRate));
    randomState = 0x6d2b79f5u ^ static_cast<std::uint32_t>(channelSeed * 0x45d9f3bu + 0x9e3779b9u);
    reset();
}

void FormantWoundCore::reset() noexcept
{
    inputHistory.fill(0.0f);
    residualHistory.fill(0.0f);
    synthesisHistory.fill(0.0f);
    currentCoefficients.fill(0.0f);
    targetCoefficients.fill(0.0f);
    warpedCoefficients.fill(0.0f);
    autocorrelation.fill(0.0f);
    lpcWork.fill(0.0f);
    envelopeHistory.fill(0.0f);
    displaySnapshot = {};
    writeIndex = 0;
    samplesUntilAnalysis = 0;
    samplesUntilDisplay = 0;
    impulseCountdown = samplesForSeconds(80.0 / 48000.0, 1, historySize);
    activeOrder = 8;
    inputFollower = 0.0f;
    wetFollower = 0.0f;
    residualFollower = 0.0f;
    dcX = 0.0f;
    dcY = 0.0f;
    feedbackState = 0.0f;
    previousReseed = -1.0f;
    envelopeMotion = 0.0f;
    rescueActive = false;
    publishSnapshot(displaySnapshot);
}

float FormantWoundCore::processSample(float input, const FormantWoundParameters& parameters) noexcept
{
    input = sanitize(input);
    const auto reseed = clamp01(parameters.reseed);
    if (std::abs(reseed - previousReseed) > 0.02f)
    {
        randomState ^= static_cast<std::uint32_t>(1u + static_cast<unsigned>(reseed * 65535.0f)) * 0x27d4eb2du;
        impulseCountdown = samplesForSeconds((8.0 + static_cast<double>(nextNoise()) * 120.0) / 48000.0, 1, historySize);
        previousReseed = reseed;
    }

    inputHistory[static_cast<std::size_t>(writeIndex)] = input;
    writeIndex = (writeIndex + 1) & (historySize - 1);
    if (--samplesUntilAnalysis <= 0)
    {
        samplesUntilAnalysis = analysisHopSamples;
        if (! parameters.freeze)
            analyzeFrame(parameters);
    }

    const auto order = orderFromResolution(parameters.resolution);
    activeOrder = order;
    const auto excitation = clamp01(parameters.excitation);
    const auto damage = clamp01(parameters.damage);
    const auto feedback = std::clamp(parameters.feedback, 0.0f, 0.92f);
    const auto mix = clamp01(parameters.mix);
    const auto warp = std::clamp(std::isfinite(parameters.warp) ? parameters.warp : 0.5f, 0.0f, 1.0f);
    const auto warpBipolar = (warp - 0.5f) * 2.0f;

    for (int i = 0; i < maxOrder; ++i)
        currentCoefficients[static_cast<std::size_t>(i)] +=
            (targetCoefficients[static_cast<std::size_t>(i)] - currentCoefficients[static_cast<std::size_t>(i)]) * coefficientSmoothing;

    float residual = input;
    for (int i = 0; i < order; ++i)
        residual -= currentCoefficients[static_cast<std::size_t>(i)] * residualHistory[static_cast<std::size_t>(i)];
    residual = sanitize(residual);

    const auto noise = (nextNoise() * 2.0f - 1.0f);
    const auto activity = inputFollower > 1.0e-8f ? std::clamp(inputFollower * 5.0f + 0.015f, 0.0f, 1.0f) : 0.0f;
    if (--impulseCountdown <= 0)
    {
        const auto base = 20 + static_cast<int>((1.0f - damage) * 130.0f);
        impulseCountdown = samplesForSeconds((static_cast<double>(base) + static_cast<double>(nextNoise()) * 37.0) / 48000.0,
                                             1,
                                             historySize);
    }
    const auto impulse = impulseCountdown == 1 ? (nextNoise() > 0.5f ? 1.0f : -1.0f) : 0.0f;
    const auto corruptedExciter = residual * (1.0f - excitation)
        + (0.55f * noise + 0.75f * impulse) * activity * excitation;

    float absSum = 0.0f;
    for (int i = 0; i < order; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto spectralFold = 1.0f + warpBipolar * 0.48f * std::sin(static_cast<float>(i + 1) * 0.77f);
        const auto chip = 1.0f - damage * (0.10f + 0.025f * static_cast<float>((i * 5) % 7));
        const auto bitStep = 1.0f / static_cast<float>(2 + static_cast<int>(damage * 18.0f));
        auto coefficient = currentCoefficients[index] * spectralFold * chip;
        coefficient = std::round(coefficient / bitStep) * bitStep;
        warpedCoefficients[index] = sanitize(coefficient);
        absSum += std::abs(warpedCoefficients[index]);
    }
    const auto stabilityLimit = 0.78f + 0.14f * (1.0f - feedback);
    if (absSum > stabilityLimit && absSum > 0.0f)
    {
        const auto scale = stabilityLimit / absSum;
        for (int i = 0; i < order; ++i)
            warpedCoefficients[static_cast<std::size_t>(i)] *= scale;
    }

    float wet = corruptedExciter + feedbackState * feedback * (0.28f + 0.35f * damage);
    for (int i = 0; i < order; ++i)
        wet += warpedCoefficients[static_cast<std::size_t>(i)] * synthesisHistory[static_cast<std::size_t>(i)];
    wet = softClip(wet * (1.0f + damage * 2.0f));

    if (inputFollower > 0.01f && std::abs(wet) < 0.0004f && excitation > 0.25f)
    {
        wet += 0.025f * activity * noise;
        rescueActive = true;
    }
    else
    {
        rescueActive = false;
    }

    const auto dcOut = wet - dcX + dcCoefficient * dcY;
    dcX = wet;
    dcY = sanitize(dcOut);
    wet = dcY;

    for (int i = maxOrder - 1; i > 0; --i)
    {
        residualHistory[static_cast<std::size_t>(i)] = residualHistory[static_cast<std::size_t>(i - 1)];
        synthesisHistory[static_cast<std::size_t>(i)] = synthesisHistory[static_cast<std::size_t>(i - 1)];
    }
    residualHistory[0] = residual;
    synthesisHistory[0] = wet;
    feedbackState = wet;

    inputFollower += (input * input - inputFollower) * followerSmoothing;
    residualFollower += (residual * residual - residualFollower) * followerSmoothing;
    wetFollower += (wet * wet - wetFollower) * followerSmoothing;

    displaySnapshot.frozen = parameters.freeze;
    if (--samplesUntilDisplay <= 0)
    {
        samplesUntilDisplay = displayHopSamples;
        updateDisplay();
    }

    if (mix <= 0.0f)
        return sanitize(input);

    const auto wetOut = wet * dbToGain(parameters.outputDb) * 0.35f;
    auto output = input * (1.0f - mix) + wetOut * mix;
    output = std::clamp(softClip(output), -0.98f, 0.98f);
    return sanitize(output);
}

void FormantWoundCore::analyzeFrame(const FormantWoundParameters& parameters) noexcept
{
    const auto order = orderFromResolution(parameters.resolution);
    autocorrelation.fill(0.0f);

    for (int n = 0; n < analysisFrameSamples; ++n)
    {
        const auto historyIndex = (writeIndex - 1 - n + historySize) & (historySize - 1);
        const auto window = 0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(analysisFrameSamples - 1));
        const auto x = inputHistory[static_cast<std::size_t>(historyIndex)] * window;
        for (int lag = 0; lag <= order; ++lag)
        {
            const auto lagIndex = (historyIndex - lag + historySize) & (historySize - 1);
            autocorrelation[static_cast<std::size_t>(lag)] += x * inputHistory[static_cast<std::size_t>(lagIndex)] * window;
        }
    }

    const auto energy = std::max(autocorrelation[0], 1.0e-8f);
    lpcWork.fill(0.0f);
    float predictionError = energy;
    float motion = 0.0f;

    for (int i = 1; i <= order; ++i)
    {
        float acc = autocorrelation[static_cast<std::size_t>(i)];
        for (int j = 1; j < i; ++j)
            acc -= lpcWork[static_cast<std::size_t>(j)] * autocorrelation[static_cast<std::size_t>(i - j)];

        auto reflection = acc / std::max(predictionError, 1.0e-8f);
        reflection = std::clamp(reflection, -0.84f, 0.84f);
        lpcWork[static_cast<std::size_t>(i)] = reflection;
        for (int j = 1; j <= i / 2; ++j)
        {
            const auto left = lpcWork[static_cast<std::size_t>(j)];
            const auto right = lpcWork[static_cast<std::size_t>(i - j)];
            lpcWork[static_cast<std::size_t>(j)] = left - reflection * right;
            if (j != i - j)
                lpcWork[static_cast<std::size_t>(i - j)] = right - reflection * left;
        }
        predictionError *= std::max(0.05f, 1.0f - reflection * reflection);
    }

    const auto damage = clamp01(parameters.damage);
    for (int i = 0; i < maxOrder; ++i)
    {
        const auto next = i < order ? std::clamp(lpcWork[static_cast<std::size_t>(i + 1)], -0.82f, 0.82f) : 0.0f;
        motion += std::abs(next - targetCoefficients[static_cast<std::size_t>(i)]);
        targetCoefficients[static_cast<std::size_t>(i)] = next * (0.82f - 0.20f * damage);
    }
    envelopeMotion = std::min(1.0f, motion);
}

void FormantWoundCore::updateDisplay() noexcept
{
    for (int column = 0; column < WoundSnapshot::columns; ++column)
    {
        const auto coeffIndex = std::min(activeOrder - 1, column * std::max(1, activeOrder) / WoundSnapshot::columns);
        const auto value = std::min(1.0f, std::abs(warpedCoefficients[static_cast<std::size_t>(std::max(0, coeffIndex))]) * 3.2f);
        envelopeHistory[static_cast<std::size_t>(column)] =
            envelopeHistory[static_cast<std::size_t>(column)] * 0.92f + value * 0.08f;
    }

    for (int y = 0; y < WoundSnapshot::rows; ++y)
    {
        for (int x = 0; x < WoundSnapshot::columns; ++x)
        {
            const auto level = envelopeHistory[static_cast<std::size_t>(x)];
            const auto threshold = 1.0f - static_cast<float>(y + 1) / static_cast<float>(WoundSnapshot::rows);
            displaySnapshot.cells[static_cast<std::size_t>(y * WoundSnapshot::columns + x)] = level > threshold ? level : 0.0f;
        }
    }

    displaySnapshot.inputRms = std::sqrt(std::max(0.0f, inputFollower));
    displaySnapshot.wetRms = std::sqrt(std::max(0.0f, wetFollower));
    displaySnapshot.residualRms = std::sqrt(std::max(0.0f, residualFollower));
    displaySnapshot.envelopeMotion = envelopeMotion;
    displaySnapshot.activeOrder = activeOrder;
    displaySnapshot.rescue = rescueActive;
    publishSnapshot(displaySnapshot);
}

int FormantWoundCore::orderFromResolution(float resolution) const noexcept
{
    return std::clamp(4 + static_cast<int>(clamp01(resolution) * 12.0f + 0.5f), 4, maxOrder);
}

int FormantWoundCore::samplesForSeconds(double seconds, int minimum, int maximum) const noexcept
{
    const auto raw = std::isfinite(seconds) ? seconds * sampleRate : static_cast<double>(minimum);
    return std::clamp(static_cast<int>(std::lround(raw)), minimum, maximum);
}

float FormantWoundCore::onePoleCoefficient(double timeSeconds) const noexcept
{
    if (! std::isfinite(timeSeconds) || timeSeconds <= 0.0)
        return 1.0f;
    return static_cast<float>(1.0 - std::exp(-1.0 / (timeSeconds * sampleRate)));
}

float FormantWoundCore::nextNoise() noexcept
{
    randomState = randomState * 1664525u + 1013904223u;
    return static_cast<float>((randomState >> 8u) & 0x00ffffffu) / 16777215.0f;
}

float FormantWoundCore::sanitize(float value) const noexcept
{
    if (! std::isfinite(value) || std::abs(value) < 1.0e-20f)
        return 0.0f;
    return std::clamp(value, -4.0f, 4.0f);
}

void FormantWoundCore::copySnapshot(WoundSnapshot& destination) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto begin = publishedSnapshot.sequence.load(std::memory_order_acquire);
        if ((begin & 1u) != 0u)
            continue;

        WoundSnapshot next;
        for (std::size_t i = 0; i < next.cells.size(); ++i)
            next.cells[i] = publishedSnapshot.cells[i].load(std::memory_order_relaxed);
        next.inputRms = publishedSnapshot.inputRms.load(std::memory_order_relaxed);
        next.wetRms = publishedSnapshot.wetRms.load(std::memory_order_relaxed);
        next.residualRms = publishedSnapshot.residualRms.load(std::memory_order_relaxed);
        next.envelopeMotion = publishedSnapshot.envelopeMotion.load(std::memory_order_relaxed);
        next.activeOrder = publishedSnapshot.activeOrder.load(std::memory_order_relaxed);
        next.rescue = publishedSnapshot.rescue.load(std::memory_order_relaxed);
        next.frozen = publishedSnapshot.frozen.load(std::memory_order_relaxed);

        const auto end = publishedSnapshot.sequence.load(std::memory_order_acquire);
        if (begin == end && (end & 1u) == 0u)
        {
            destination = next;
            return;
        }
    }

    for (std::size_t i = 0; i < destination.cells.size(); ++i)
        destination.cells[i] = publishedSnapshot.cells[i].load(std::memory_order_relaxed);
    destination.inputRms = publishedSnapshot.inputRms.load(std::memory_order_relaxed);
    destination.wetRms = publishedSnapshot.wetRms.load(std::memory_order_relaxed);
    destination.residualRms = publishedSnapshot.residualRms.load(std::memory_order_relaxed);
    destination.envelopeMotion = publishedSnapshot.envelopeMotion.load(std::memory_order_relaxed);
    destination.activeOrder = publishedSnapshot.activeOrder.load(std::memory_order_relaxed);
    destination.rescue = publishedSnapshot.rescue.load(std::memory_order_relaxed);
    destination.frozen = publishedSnapshot.frozen.load(std::memory_order_relaxed);
}

void FormantWoundCore::publishSnapshot(const WoundSnapshot& source) noexcept
{
    const auto begin = publishedSnapshot.sequence.load(std::memory_order_relaxed);
    publishedSnapshot.sequence.store(begin + 1u, std::memory_order_release);
    for (std::size_t i = 0; i < source.cells.size(); ++i)
        publishedSnapshot.cells[i].store(source.cells[i], std::memory_order_relaxed);
    publishedSnapshot.inputRms.store(source.inputRms, std::memory_order_relaxed);
    publishedSnapshot.wetRms.store(source.wetRms, std::memory_order_relaxed);
    publishedSnapshot.residualRms.store(source.residualRms, std::memory_order_relaxed);
    publishedSnapshot.envelopeMotion.store(source.envelopeMotion, std::memory_order_relaxed);
    publishedSnapshot.activeOrder.store(source.activeOrder, std::memory_order_relaxed);
    publishedSnapshot.rescue.store(source.rescue, std::memory_order_relaxed);
    publishedSnapshot.frozen.store(source.frozen, std::memory_order_relaxed);
    publishedSnapshot.sequence.store(begin + 2u, std::memory_order_release);
}
} // namespace formantwound::dsp
