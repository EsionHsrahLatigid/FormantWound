#pragma once

#include "dsp/FormantWoundCore.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

class FormantWoundAudioProcessor final : public juce::AudioProcessor
{
public:
    FormantWoundAudioProcessor();
    ~FormantWoundAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.3; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    [[nodiscard]] bool copyWoundSnapshot(formantwound::dsp::WoundSnapshot& destination) const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct ParameterPointers
    {
        std::atomic<float>* resolution = nullptr;
        std::atomic<float>* excitation = nullptr;
        std::atomic<float>* warp = nullptr;
        std::atomic<float>* freeze = nullptr;
        std::atomic<float>* reseed = nullptr;
        std::atomic<float>* damage = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
    } parameter;

    void cacheParameterPointers();
    [[nodiscard]] formantwound::dsp::FormantWoundParameters readParameters() const noexcept;

    std::array<formantwound::dsp::FormantWoundCore, 2> cores;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FormantWoundAudioProcessor)
};
