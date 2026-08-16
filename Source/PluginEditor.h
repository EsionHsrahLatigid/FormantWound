#pragma once

#include "PluginProcessor.h"
#include <ehl/juce_design/EhlDesign.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>

class FormantWoundAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit FormantWoundAudioProcessorEditor(FormantWoundAudioProcessor&);
    ~FormantWoundAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 560;
    static constexpr int defaultHeight = 344;

private:
    class WoundDisplay final : public juce::Component
    {
    public:
        void setSnapshot(const formantwound::dsp::WoundSnapshot& next);
        void paint(juce::Graphics&) override;
    private:
        formantwound::dsp::WoundSnapshot snapshot;
    };

    void timerCallback() override;
    void updateReadout();
    void configureControl(juce::Slider& slider, juce::Label& label, const juce::String& text);

    FormantWoundAudioProcessor& ownerProcessor;
    ehl::juce_design::LookAndFeel lookAndFeel;
    WoundDisplay display;
    juce::Label status;
    std::array<juce::Slider, 8> sliders;
    std::array<juce::Label, 9> labels;
    juce::ToggleButton freezeButton;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FormantWoundAudioProcessorEditor)
};
