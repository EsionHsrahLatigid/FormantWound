#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "PluginEditor.h"

#include <algorithm>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;
using Layout = APVTS::ParameterLayout;

std::unique_ptr<juce::AudioParameterFloat> makeFloat(const char* id,
                                                      const char* name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, defaultValue);
}
} // namespace

FormantWoundAudioProcessor::FormantWoundAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("FormantWoundState"), createParameterLayout())
{
    cacheParameterPointers();
}

Layout FormantWoundAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> values;
    values.reserve(9);
    values.push_back(makeFloat(formantwound::parameters::resolution, "LPC Resolution", { 0.0f, 1.0f, 0.001f }, 0.55f));
    values.push_back(makeFloat(formantwound::parameters::excitation, "Excitation Corruption", { 0.0f, 1.0f, 0.001f }, 0.45f));
    values.push_back(makeFloat(formantwound::parameters::warp, "Formant Warp", { 0.0f, 1.0f, 0.001f }, 0.52f));
    values.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { formantwound::parameters::freeze, 1 }, "Freeze Hold", false));
    values.push_back(makeFloat(formantwound::parameters::reseed, "Reseed", { 0.0f, 1.0f, 0.001f }, 0.0f));
    values.push_back(makeFloat(formantwound::parameters::damage, "Damage", { 0.0f, 1.0f, 0.001f }, 0.35f));
    values.push_back(makeFloat(formantwound::parameters::feedback, "Feedback", { 0.0f, 0.92f, 0.001f }, 0.18f));
    values.push_back(makeFloat(formantwound::parameters::mix, "Mix", { 0.0f, 1.0f, 0.001f }, 1.0f));
    values.push_back(makeFloat(formantwound::parameters::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f));
    return { values.begin(), values.end() };
}

void FormantWoundAudioProcessor::cacheParameterPointers()
{
    parameter.resolution = parameters.getRawParameterValue(formantwound::parameters::resolution);
    parameter.excitation = parameters.getRawParameterValue(formantwound::parameters::excitation);
    parameter.warp = parameters.getRawParameterValue(formantwound::parameters::warp);
    parameter.freeze = parameters.getRawParameterValue(formantwound::parameters::freeze);
    parameter.reseed = parameters.getRawParameterValue(formantwound::parameters::reseed);
    parameter.damage = parameters.getRawParameterValue(formantwound::parameters::damage);
    parameter.feedback = parameters.getRawParameterValue(formantwound::parameters::feedback);
    parameter.mix = parameters.getRawParameterValue(formantwound::parameters::mix);
    parameter.output = parameters.getRawParameterValue(formantwound::parameters::output);
}

void FormantWoundAudioProcessor::prepareToPlay(double sampleRate, int)
{
    setLatencySamples(0);
    for (std::size_t i = 0; i < cores.size(); ++i)
        cores[i].prepare(sampleRate, static_cast<int>(i));
}

void FormantWoundAudioProcessor::releaseResources()
{
}

void FormantWoundAudioProcessor::reset()
{
    for (auto& core : cores)
        core.reset();
}

bool FormantWoundAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

formantwound::dsp::FormantWoundParameters FormantWoundAudioProcessor::readParameters() const noexcept
{
    formantwound::dsp::FormantWoundParameters result;
    result.resolution = parameter.resolution->load();
    result.excitation = parameter.excitation->load();
    result.warp = parameter.warp->load();
    result.freeze = parameter.freeze->load() >= 0.5f;
    result.reseed = parameter.reseed->load();
    result.damage = parameter.damage->load();
    result.feedback = parameter.feedback->load();
    result.mix = parameter.mix->load();
    result.outputDb = parameter.output->load();
    return result;
}

void FormantWoundAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto numSamples = buffer.getNumSamples();
    const auto inputChannels = std::clamp(getTotalNumInputChannels(), 1, 2);
    const auto outputChannels = std::min(buffer.getNumChannels(), 2);
    if (numSamples <= 0 || outputChannels <= 0)
        return;

    for (int channel = outputChannels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, numSamples);

    const auto params = readParameters();
    auto* left = buffer.getWritePointer(0);
    auto* right = outputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto leftIn = left[sample];
        const auto rightIn = inputChannels > 1 && right != nullptr ? right[sample] : leftIn;
        left[sample] = cores[0].processSample(leftIn, params);
        if (right != nullptr)
            right[sample] = cores[1].processSample(rightIn, params);
    }
}

void FormantWoundAudioProcessor::getStateInformation(juce::MemoryBlock& destinationData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destinationData);
}

void FormantWoundAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        const auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid() && state.hasType(parameters.state.getType()))
            parameters.replaceState(state);
    }
}

bool FormantWoundAudioProcessor::copyWoundSnapshot(formantwound::dsp::WoundSnapshot& destination) const noexcept
{
    formantwound::dsp::WoundSnapshot left;
    formantwound::dsp::WoundSnapshot right;
    if (! cores[0].copySnapshot(left) || ! cores[1].copySnapshot(right))
        return false;

    destination = left;
    for (std::size_t i = 0; i < destination.cells.size(); ++i)
        destination.cells[i] = std::max(left.cells[i], right.cells[i]);
    destination.inputRms = 0.5f * (left.inputRms + right.inputRms);
    destination.wetRms = 0.5f * (left.wetRms + right.wetRms);
    destination.residualRms = 0.5f * (left.residualRms + right.residualRms);
    destination.envelopeMotion = 0.5f * (left.envelopeMotion + right.envelopeMotion);
    destination.activeOrder = std::max(left.activeOrder, right.activeOrder);
    destination.rescue = left.rescue || right.rescue;
    destination.frozen = left.frozen || right.frozen;
    return true;
}

juce::AudioProcessorEditor* FormantWoundAudioProcessor::createEditor()
{
    return new FormantWoundAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FormantWoundAudioProcessor();
}
