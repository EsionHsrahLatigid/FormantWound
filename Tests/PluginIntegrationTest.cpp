#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ParameterIDs.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>

namespace
{
struct Failure final : std::exception
{
    explicit Failure(std::string messageIn) : message(std::move(messageIn)) {}
    const char* what() const noexcept override { return message.c_str(); }
    std::string message;
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

float rmsOf(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    int count = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            expect(std::isfinite(data[sample]), "processor output should be finite");
            sum += static_cast<double>(data[sample]) * data[sample];
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

juce::RangedAudioParameter* parameterById(FormantWoundAudioProcessor& processor, const char* id)
{
    auto* parameter = processor.parameters.getParameter(id);
    expect(parameter != nullptr, std::string("missing parameter ") + id);
    return parameter;
}

void renderBlock(FormantWoundAudioProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    expect(midi.isEmpty(), "effect should clear MIDI because it does not expose MIDI I/O");
}

void identity_and_layout()
{
    FormantWoundAudioProcessor processor;
    expect(processor.getName() == "FormantWound", "processor name should match product");
    expect(! processor.acceptsMidi(), "processor should not accept MIDI");
    expect(! processor.producesMidi(), "processor should not produce MIDI");
    expect(! processor.isMidiEffect(), "processor should not be a MIDI effect");
    expect(processor.getTailLengthSeconds() > 0.0, "processor should report bounded tail");

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    expect(processor.isBusesLayoutSupported(layout), "stereo input/output should be supported");
}

void parameter_contract_and_state_roundtrip()
{
    FormantWoundAudioProcessor processor;
    const char* ids[] = {
        formantwound::parameters::resolution,
        formantwound::parameters::excitation,
        formantwound::parameters::warp,
        formantwound::parameters::freeze,
        formantwound::parameters::reseed,
        formantwound::parameters::damage,
        formantwound::parameters::feedback,
        formantwound::parameters::mix,
        formantwound::parameters::output
    };
    for (auto* id : ids)
        (void) parameterById(processor, id);

    parameterById(processor, formantwound::parameters::damage)->setValueNotifyingHost(1.0f);
    parameterById(processor, formantwound::parameters::freeze)->setValueNotifyingHost(1.0f);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    expect(state.getSize() > 0, "state XML should be serialized");

    FormantWoundAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    expect(restored.parameters.getRawParameterValue(formantwound::parameters::freeze)->load() > 0.5f,
           "state roundtrip should restore freeze");
}

void processing_is_audible_and_editor_constructs()
{
    FormantWoundAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    parameterById(processor, formantwound::parameters::resolution)->setValueNotifyingHost(1.0f);
    parameterById(processor, formantwound::parameters::excitation)->setValueNotifyingHost(1.0f);
    parameterById(processor, formantwound::parameters::damage)->setValueNotifyingHost(1.0f);
    parameterById(processor, formantwound::parameters::feedback)->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> buffer(2, 4096);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = 0.25f * std::sin(2.0f * 3.14159265358979323846f * 220.0f * static_cast<float>(sample) / 48000.0f);
        buffer.setSample(0, sample, value);
        buffer.setSample(1, sample, value);
    }

    renderBlock(processor, buffer);
    expect(rmsOf(buffer) > 0.01f, "plugin processing should remain audible");

    formantwound::dsp::WoundSnapshot snapshot;
    processor.copyWoundSnapshot(snapshot);
    expect(snapshot.activeOrder >= 4, "processor should expose snapshot order");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    expect(editor != nullptr, "native editor should construct");
    expect(editor->getWidth() == FormantWoundAudioProcessorEditor::defaultWidth, "editor width should match compact UI contract");
    expect(editor->getHeight() == FormantWoundAudioProcessorEditor::defaultHeight, "editor height should match compact UI contract");
}

void reset_and_bypass_like_dry_mix()
{
    FormantWoundAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    parameterById(processor, formantwound::parameters::mix)->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> buffer(2, 256);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        buffer.setSample(0, sample, sample == 0 ? 0.75f : 0.0f);
        buffer.setSample(1, sample, sample == 0 ? -0.75f : 0.0f);
    }
    auto original = buffer;
    renderBlock(processor, buffer);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            expect(std::abs(buffer.getSample(channel, sample) - original.getSample(channel, sample)) < 1.0e-6f,
                   "zero mix should preserve dry signal");
    processor.reset();
}
} // namespace

int main()
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        identity_and_layout();
        parameter_contract_and_state_roundtrip();
        processing_is_audible_and_editor_constructs();
        reset_and_bypass_like_dry_mix();
        std::cout << "FormantWound integration tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
