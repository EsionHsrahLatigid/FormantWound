#include "../Source/dsp/FormantWoundCore.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace formantwound::dsp;

struct Failure final : std::exception
{
    explicit Failure(std::string messageIn) : message(std::move(messageIn)) {}
    const char* what() const noexcept override { return message.c_str(); }
    std::string message;
};

struct Metrics
{
    float rms = 0.0f;
    float peak = 0.0f;
    float dc = 0.0f;
    int zeroCrossings = 0;
    int clipped = 0;
    int uniqueBuckets = 0;
};

[[noreturn]] void fail(const std::string& message)
{
    throw Failure(message);
}

void expect(bool condition, const std::string& message)
{
    if (! condition)
        fail(message);
}

void near(float actual, float expected, float tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance)
        fail(message + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}

Metrics measure(const std::vector<float>& signal)
{
    Metrics result;
    double sum = 0.0;
    double sumSquares = 0.0;
    bool hadPrevious = false;
    float previous = 0.0f;
    std::vector<int> buckets;
    buckets.reserve(signal.size());

    for (auto sample : signal)
    {
        expect(std::isfinite(sample), "output must be finite");
        sum += sample;
        sumSquares += static_cast<double>(sample) * sample;
        result.peak = std::max(result.peak, std::abs(sample));
        if (std::abs(sample) >= 0.979f)
            ++result.clipped;
        if (hadPrevious && ((previous < 0.0f && sample >= 0.0f) || (previous >= 0.0f && sample < 0.0f)))
            ++result.zeroCrossings;
        previous = sample;
        hadPrevious = true;
        buckets.push_back(static_cast<int>(std::round(sample * 4096.0f)));
    }

    std::sort(buckets.begin(), buckets.end());
    result.uniqueBuckets = static_cast<int>(std::unique(buckets.begin(), buckets.end()) - buckets.begin());
    result.rms = signal.empty() ? 0.0f : static_cast<float>(std::sqrt(sumSquares / static_cast<double>(signal.size())));
    result.dc = signal.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(signal.size()));
    return result;
}

std::vector<float> sine(float hz, std::size_t samples, float amplitude = 0.35f)
{
    std::vector<float> result(samples);
    for (std::size_t i = 0; i < samples; ++i)
        result[i] = amplitude * std::sin(2.0f * 3.14159265358979323846f * hz * static_cast<float>(i) / 48000.0f);
    return result;
}

std::vector<float> sineAtRate(float hz, std::size_t samples, double sampleRate, float amplitude = 0.35f)
{
    std::vector<float> result(samples);
    for (std::size_t i = 0; i < samples; ++i)
    {
        const auto phase = 2.0 * 3.14159265358979323846 * static_cast<double>(hz) * static_cast<double>(i) / sampleRate;
        result[i] = amplitude * static_cast<float>(std::sin(phase));
    }
    return result;
}

std::vector<float> impulseTrain(std::size_t samples)
{
    std::vector<float> result(samples);
    for (std::size_t i = 0; i < samples; i += 97)
        result[i] = (i % 2 == 0 ? 0.8f : -0.8f);
    return result;
}

std::vector<float> seededNoise(std::size_t samples)
{
    std::vector<float> result(samples);
    unsigned state = 0xbadc0deu;
    for (auto& sample : result)
    {
        state = state * 1664525u + 1013904223u;
        sample = (static_cast<float>((state >> 8u) & 0xffffu) / 32768.0f - 1.0f) * 0.25f;
    }
    return result;
}

std::vector<float> render(FormantWoundParameters params,
                          const std::vector<float>& input,
                          const std::vector<int>& partitions,
                          int channelSeed = 0,
                          double sampleRate = 48000.0)
{
    auto core = std::make_unique<FormantWoundCore>();
    core->prepare(sampleRate, channelSeed);
    std::vector<float> output;
    output.reserve(input.size());
    std::size_t index = 0;
    std::size_t partition = 0;
    while (index < input.size())
    {
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(partitions[partition % partitions.size()]),
                                                 input.size() - index);
        for (std::size_t i = 0; i < count; ++i)
            output.push_back(core->processSample(input[index++], params));
        ++partition;
    }
    return output;
}

float dftMagnitude(const std::vector<float>& signal, float hz, std::size_t start)
{
    double real = 0.0;
    double imag = 0.0;
    constexpr double twoPi = 6.28318530717958647692;
    for (std::size_t i = start; i < signal.size(); ++i)
    {
        const auto phase = twoPi * static_cast<double>(hz) * static_cast<double>(i - start) / 48000.0;
        real += signal[i] * std::cos(phase);
        imag -= signal[i] * std::sin(phase);
    }
    return static_cast<float>(std::sqrt(real * real + imag * imag) / static_cast<double>(signal.size() - start));
}

void silence_and_nonfinite_are_guarded()
{
    FormantWoundParameters params;
    params.damage = 1.0f;
    params.feedback = 0.92f;
    auto silence = render(params, std::vector<float>(12000, 0.0f), { 17, 64, 511 });
    expect(measure(silence).rms == 0.0f, "silence should remain exact silence");

    FormantWoundCore core;
    core.prepare(96000.0, 1);
    for (int i = 0; i < 4000; ++i)
    {
        const auto input = i == 32 ? INFINITY : (i == 153 ? NAN : 0.1f);
        expect(std::isfinite(core.processSample(input, params)), "non-finite input should not propagate");
    }
}

void lpc_resolution_changes_envelope_response()
{
    FormantWoundParameters low;
    low.resolution = 0.0f;
    low.excitation = 0.0f;
    low.damage = 0.15f;
    low.mix = 1.0f;

    auto high = low;
    high.resolution = 1.0f;
    auto input = sine(310.0f, 48000);
    auto lowOutput = render(low, input, { 128 });
    auto highOutput = render(high, input, { 128 });
    const auto lowMetric = measure(lowOutput);
    const auto highMetric = measure(highOutput);
    float diff = 0.0f;
    for (std::size_t i = 16000; i < input.size(); ++i)
        diff += std::abs(lowOutput[i] - highOutput[i]);
    diff /= static_cast<float>(input.size() - 16000);
    std::cout << "resolution lowRms=" << lowMetric.rms << " highRms=" << highMetric.rms << " meanDiff=" << diff << '\n';
    expect(lowMetric.rms > 0.01f && highMetric.rms > 0.01f, "both LPC resolutions should stay audible");
    expect(diff > 0.001f, "LPC order/resolution should materially change the envelope response");
}

void formant_warp_moves_band_balance()
{
    FormantWoundParameters left;
    left.resolution = 0.8f;
    left.excitation = 0.2f;
    left.warp = 0.0f;
    left.damage = 0.25f;
    auto right = left;
    right.warp = 1.0f;

    auto input = seededNoise(48000);
    auto lowWarp = render(left, input, { 64 });
    auto highWarp = render(right, input, { 64 });
    const auto lowBand = dftMagnitude(lowWarp, 660.0f, 16000) + dftMagnitude(lowWarp, 1320.0f, 16000);
    const auto highBand = dftMagnitude(highWarp, 660.0f, 16000) + dftMagnitude(highWarp, 1320.0f, 16000);
    float diff = 0.0f;
    for (std::size_t i = 16000; i < lowWarp.size(); ++i)
        diff += std::abs(lowWarp[i] - highWarp[i]);
    diff /= static_cast<float>(lowWarp.size() - 16000);
    std::cout << "warp bands low=" << lowBand << " high=" << highBand << " meanDiff=" << diff << '\n';
    expect(diff > 0.001f, "formant warp should alter the reconstructed envelope response");
}

void freeze_holds_envelope_against_new_input()
{
    FormantWoundCore live;
    FormantWoundCore held;
    live.prepare(48000.0, 0);
    held.prepare(48000.0, 0);

    FormantWoundParameters params;
    params.resolution = 0.8f;
    params.excitation = 0.15f;
    params.damage = 0.3f;
    for (auto sample : sine(180.0f, 24000))
    {
        (void) live.processSample(sample, params);
        (void) held.processSample(sample, params);
    }

    params.freeze = true;
    std::vector<float> heldOutput;
    heldOutput.reserve(24000);
    for (auto sample : sine(1280.0f, 24000))
        heldOutput.push_back(held.processSample(sample, params));

    params.freeze = false;
    std::vector<float> liveOutput;
    liveOutput.reserve(24000);
    for (auto sample : sine(1280.0f, 24000))
        liveOutput.push_back(live.processSample(sample, params));

    float diff = 0.0f;
    for (std::size_t i = 8000; i < liveOutput.size(); ++i)
        diff += std::abs(liveOutput[i] - heldOutput[i]);
    diff /= static_cast<float>(liveOutput.size() - 8000);
    std::cout << "freeze live/held meanDiff=" << diff << '\n';
    expect(diff > 0.001f, "freeze should hold a stale tract against new input");
}

void extremes_stay_aggressive_and_bounded()
{
    FormantWoundParameters params;
    params.resolution = 1.0f;
    params.excitation = 1.0f;
    params.warp = 1.0f;
    params.reseed = 0.73f;
    params.damage = 1.0f;
    params.feedback = 0.92f;
    params.mix = 1.0f;
    params.outputDb = 12.0f;
    auto input = seededNoise(96000);
    auto impulses = impulseTrain(96000);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] += impulses[i];

    auto output = render(params, input, { 1, 17, 31, 64, 509, 1024 });
    const auto metrics = measure(output);
    std::cout << "extreme rms=" << metrics.rms << " peak=" << metrics.peak
              << " dc=" << metrics.dc << " zc=" << metrics.zeroCrossings
              << " unique=" << metrics.uniqueBuckets << " clipped=" << metrics.clipped << '\n';
    expect(metrics.rms > 0.04f, "extreme output should not collapse into near silence");
    expect(metrics.peak <= 0.981f, "final ceiling should bound output");
    expect(metrics.clipped < 128, "output should not become a clipped constant");
    expect(std::abs(metrics.dc) < 0.08f, "DC guard should control offset");
    expect(metrics.zeroCrossings > 1000, "extreme output should retain high activity");
    expect(metrics.uniqueBuckets > 256, "extreme output should not become stationary");
}

void block_partition_determinism_and_reset()
{
    FormantWoundParameters params;
    params.resolution = 0.83f;
    params.excitation = 0.71f;
    params.warp = 0.12f;
    params.reseed = 0.27f;
    params.damage = 0.88f;
    params.feedback = 0.72f;
    auto input = seededNoise(32000);

    auto first = render(params, input, { 64 });
    auto second = render(params, input, { 64 });
    expect(first == second, "same render path should be deterministic after reset");

    auto partitioned = render(params, input, { 1, 7, 31, 129, 511 });
    expect(first.size() == partitioned.size(), "partitioned render size should match");
    for (std::size_t i = 0; i < first.size(); ++i)
        near(first[i], partitioned[i], 0.0f, "sample-by-sample processing should be independent of host block partitioning");
}

void snapshot_reports_functional_state()
{
    FormantWoundCore core;
    core.prepare(48000.0, 0);
    FormantWoundParameters params;
    params.resolution = 0.9f;
    params.excitation = 0.6f;
    params.damage = 0.7f;
    for (int i = 0; i < 12000; ++i)
        (void) core.processSample(0.2f * std::sin(0.03f * static_cast<float>(i)), params);

    WoundSnapshot snapshot;
    core.copySnapshot(snapshot);
    expect(snapshot.inputRms > 0.01f, "snapshot should report input activity");
    expect(snapshot.wetRms > 0.01f, "snapshot should report wet activity");
    expect(snapshot.residualRms > 0.001f, "snapshot should report residual activity");
    expect(snapshot.activeOrder >= 4, "snapshot should report active LPC order");
    const auto active = std::count_if(snapshot.cells.begin(), snapshot.cells.end(), [](float v) { return v > 0.0f; });
    expect(active > 0, "snapshot matrix should contain envelope cells");
}

void sample_rate_changes_discrete_timing_without_collapsing()
{
    FormantWoundParameters params;
    params.resolution = 0.78f;
    params.excitation = 0.65f;
    params.warp = 0.42f;
    params.reseed = 0.31f;
    params.damage = 0.82f;
    params.feedback = 0.68f;
    params.mix = 1.0f;

    auto fixedInput = seededNoise(18000);
    auto at44 = render(params, fixedInput, { 64 }, 3, 44100.0);
    auto at96 = render(params, fixedInput, { 64 }, 3, 96000.0);

    float diff = 0.0f;
    for (std::size_t i = 4000; i < fixedInput.size(); ++i)
        diff += std::abs(at44[i] - at96[i]);
    diff /= static_cast<float>(fixedInput.size() - 4000);
    std::cout << "sample-rate discrete diff=" << diff << '\n';
    expect(diff > 0.0005f, "sample-rate-aware timing should not render identical discrete behavior");

    const std::array<double, 3> rates { 44100.0, 48000.0, 96000.0 };
    float referenceRms = 0.0f;
    for (auto rate : rates)
    {
        const auto samples = static_cast<std::size_t>(std::lround(rate * 0.35));
        auto input = sineAtRate(260.0f, samples, rate, 0.28f);
        auto output = render(params, input, { 37, 128, 511 }, 4, rate);
        const auto metrics = measure(output);
        std::cout << "sample-rate rate=" << rate << " rms=" << metrics.rms << " peak=" << metrics.peak
                  << " zc=" << metrics.zeroCrossings << '\n';
        expect(metrics.rms > 0.005f, "sample-rate-aware render should remain audible");
        expect(metrics.peak <= 0.981f, "sample-rate-aware render should remain bounded");
        expect(metrics.zeroCrossings > 50, "sample-rate-aware render should remain active");
        if (referenceRms == 0.0f)
        {
            referenceRms = metrics.rms;
        }
        else
        {
            const auto ratio = metrics.rms / referenceRms;
            expect(ratio > 0.25f && ratio < 4.0f, "perceptual gain should stay in the same range across sample rates");
        }
    }
}

void concurrent_snapshot_copy_remains_finite()
{
    FormantWoundCore core;
    core.prepare(96000.0, 9);

    std::atomic<bool> done { false };
    std::atomic<int> failures { 0 };
    std::thread reader([&] {
        while (! done.load(std::memory_order_acquire))
        {
            WoundSnapshot snapshot;
            core.copySnapshot(snapshot);
            bool ok = std::isfinite(snapshot.inputRms)
                   && std::isfinite(snapshot.wetRms)
                   && std::isfinite(snapshot.residualRms)
                   && std::isfinite(snapshot.envelopeMotion)
                   && snapshot.activeOrder >= 0
                   && snapshot.activeOrder <= 16;
            for (auto value : snapshot.cells)
                ok = ok && std::isfinite(value);
            if (! ok)
                failures.fetch_add(1, std::memory_order_relaxed);
        }
    });

    FormantWoundParameters params;
    params.resolution = 0.9f;
    params.excitation = 0.7f;
    params.warp = 0.36f;
    params.damage = 0.74f;
    params.feedback = 0.66f;
    params.mix = 1.0f;
    for (int i = 0; i < 120000; ++i)
    {
        params.freeze = (i / 17000) % 2 == 1;
        params.reseed = static_cast<float>((i / 11000) % 10) * 0.07f;
        const auto input = 0.23f * std::sin(0.017f * static_cast<float>(i))
                         + 0.11f * std::sin(0.071f * static_cast<float>(i));
        expect(std::isfinite(core.processSample(input, params)), "concurrent snapshot stress output should be finite");
    }

    done.store(true, std::memory_order_release);
    reader.join();
    expect(failures.load(std::memory_order_relaxed) == 0, "concurrent snapshot reader should only observe finite state");

    WoundSnapshot finalSnapshot;
    core.copySnapshot(finalSnapshot);
    expect(finalSnapshot.inputRms > 0.001f, "concurrent snapshot stress should publish input activity");
    expect(finalSnapshot.wetRms > 0.001f, "concurrent snapshot stress should publish wet activity");
}

} // namespace

int main()
{
    try
    {
        silence_and_nonfinite_are_guarded();
        lpc_resolution_changes_envelope_response();
        formant_warp_moves_band_balance();
        freeze_holds_envelope_against_new_input();
        extremes_stay_aggressive_and_bounded();
        block_partition_determinism_and_reset();
        snapshot_reports_functional_state();
        sample_rate_changes_discrete_timing_without_collapsing();
        concurrent_snapshot_copy_remains_finite();
        std::cout << "FormantWound DSP tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
