#include "PluginEditor.h"
#include "ParameterIDs.h"

namespace
{
namespace design = ehl::juce_design;
}

FormantWoundAudioProcessorEditor::FormantWoundAudioProcessorEditor(FormantWoundAudioProcessor& owner)
    : AudioProcessorEditor(owner), ownerProcessor(owner)
{
    setLookAndFeel(&lookAndFeel);
    setName("FormantWound editor");
    setComponentID("formantwound-editor");
    setTitle("FormantWound");
    setDescription("FormantWound monochrome 8-bit LPC source-filter destruction editor");
    setWantsKeyboardFocus(true);

    display.setComponentID("formantwound-envelope-display");
    addAndMakeVisible(display);

    design::styleLabel(status);
    status.setComponentID("formantwound-status");
    status.setJustificationType(juce::Justification::centredLeft);
    status.setColour(juce::Label::textColourId, design::Palette::mid());
    addAndMakeVisible(status);

    const juce::StringArray sliderNames { "ORDER", "EXCITE", "WARP", "RESEED", "DAMAGE", "FDBK", "MIX", "OUT" };
    for (std::size_t i = 0; i < sliders.size(); ++i)
        configureControl(sliders[i], labels[i], sliderNames[static_cast<int>(i)]);

    design::styleLabel(labels.back());
    labels.back().setText("HOLD", juce::dontSendNotification);
    labels.back().setJustificationType(juce::Justification::centred);
    addAndMakeVisible(labels.back());

    freezeButton.setComponentID("formantwound-freeze");
    freezeButton.setButtonText("FREEZE");
    addAndMakeVisible(freezeButton);

    sliderAttachments[0] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::resolution, sliders[0]);
    sliderAttachments[1] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::excitation, sliders[1]);
    sliderAttachments[2] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::warp, sliders[2]);
    sliderAttachments[3] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::reseed, sliders[3]);
    sliderAttachments[4] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::damage, sliders[4]);
    sliderAttachments[5] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::feedback, sliders[5]);
    sliderAttachments[6] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::mix, sliders[6]);
    sliderAttachments[7] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        ownerProcessor.parameters, formantwound::parameters::output, sliders[7]);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        ownerProcessor.parameters, formantwound::parameters::freeze, freezeButton);

    setResizable(false, false);
    setSize(defaultWidth, defaultHeight);
    startTimerHz(24);
    updateReadout();
}

FormantWoundAudioProcessorEditor::~FormantWoundAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void FormantWoundAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    design::paintEditorChrome(graphics, getLocalBounds(), "FormantWound", "LPC / SOURCE-FILTER / DAMAGE");

    auto bounds = getLocalBounds().withTrimmedTop(64).reduced(design::Metrics::margin, 0);
    auto displayBounds = bounds.removeFromTop(120);
    graphics.setColour(design::Palette::low());
    graphics.fillRect(displayBounds);
    graphics.setColour(design::Palette::mid());
    graphics.drawRect(displayBounds, 1);

    auto statusBounds = bounds.removeFromTop(24).withTrimmedTop(8);
    graphics.setColour(design::Palette::ink());
    graphics.fillRect(statusBounds);
    graphics.setColour(design::Palette::mid());
    graphics.drawRect(statusBounds, 1);

    auto controls = bounds.withTrimmedTop(8).withTrimmedBottom(design::Metrics::margin);
    graphics.setColour(design::Palette::low());
    graphics.drawRect(controls, 1);
}

void FormantWoundAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().withTrimmedTop(64).reduced(design::Metrics::margin, 0);
    display.setBounds(bounds.removeFromTop(120).reduced(8));
    status.setBounds(bounds.removeFromTop(24).withTrimmedTop(8).reduced(8, 0));
    auto controls = bounds.withTrimmedTop(8).withTrimmedBottom(design::Metrics::margin);

    const auto gap = 4;
    const auto labelH = 14;
    const auto rowH = controls.getHeight() / 2;
    auto top = controls.removeFromTop(rowH).reduced(8, 4);
    auto bottom = controls.reduced(8, 4);

    const auto topW = top.getWidth() / 5;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = top.removeFromLeft(topW).reduced(gap, 0);
        labels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(labelH));
        sliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
    auto freezeCell = top.reduced(gap, 0);
    labels.back().setBounds(freezeCell.removeFromTop(labelH));
    freezeButton.setBounds(freezeCell.reduced(4, 8));

    const auto bottomW = bottom.getWidth() / 4;
    for (int i = 4; i < 8; ++i)
    {
        auto cell = bottom.removeFromLeft(bottomW).reduced(gap, 0);
        labels[static_cast<std::size_t>(i)].setBounds(cell.removeFromTop(labelH));
        sliders[static_cast<std::size_t>(i)].setBounds(cell);
    }
}

void FormantWoundAudioProcessorEditor::timerCallback()
{
    formantwound::dsp::WoundSnapshot snapshot;
    if (ownerProcessor.copyWoundSnapshot(snapshot))
        display.setSnapshot(snapshot);
    updateReadout();
}

void FormantWoundAudioProcessorEditor::updateReadout()
{
    formantwound::dsp::WoundSnapshot snapshot;
    if (! ownerProcessor.copyWoundSnapshot(snapshot))
        return;

    status.setText(juce::String::formatted("%s   LPC%02d   IN %.4f   RES %.4f   WET %.4f   MOT %.3f",
                                           snapshot.rescue ? "RESCUE" : (snapshot.frozen ? "HOLD" : "LIVE"),
                                           snapshot.activeOrder,
                                           snapshot.inputRms,
                                           snapshot.residualRms,
                                           snapshot.wetRms,
                                           snapshot.envelopeMotion),
                   juce::dontSendNotification);
}

void FormantWoundAudioProcessorEditor::configureControl(juce::Slider& slider,
                                                        juce::Label& label,
                                                        const juce::String& text)
{
    label.setComponentID("formantwound-label-" + text.toLowerCase());
    design::styleLabel(label);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);

    slider.setComponentID("formantwound-control-" + text.toLowerCase());
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, text == "OUT" ? 58 : 48, 18);
    slider.setColour(juce::Slider::trackColourId, design::Palette::paper());
    slider.setColour(juce::Slider::backgroundColourId, design::Palette::low());
    slider.setColour(juce::Slider::thumbColourId, design::Palette::paper());
    slider.setColour(juce::Slider::textBoxTextColourId, design::Palette::paper());
    slider.setColour(juce::Slider::textBoxOutlineColourId, design::Palette::mid());
    addAndMakeVisible(slider);
}

void FormantWoundAudioProcessorEditor::WoundDisplay::setSnapshot(const formantwound::dsp::WoundSnapshot& next)
{
    snapshot = next;
    repaint();
}

void FormantWoundAudioProcessorEditor::WoundDisplay::paint(juce::Graphics& graphics)
{
    graphics.fillAll(design::Palette::ink());
    const auto area = getLocalBounds();
    const auto cellW = juce::jmax(1, area.getWidth() / formantwound::dsp::WoundSnapshot::columns);
    const auto cellH = juce::jmax(1, area.getHeight() / formantwound::dsp::WoundSnapshot::rows);

    for (int y = 0; y < formantwound::dsp::WoundSnapshot::rows; ++y)
    {
        for (int x = 0; x < formantwound::dsp::WoundSnapshot::columns; ++x)
        {
            const auto value = snapshot.cells[static_cast<std::size_t>(y * formantwound::dsp::WoundSnapshot::columns + x)];
            auto cell = juce::Rectangle<int>(area.getX() + x * cellW,
                                             area.getY() + y * cellH,
                                             juce::jmax(1, cellW - 1),
                                             juce::jmax(1, cellH - 1));
            graphics.setColour(value > 0.66f ? design::Palette::paper()
                              : value > 0.25f ? design::Palette::mid()
                              : design::Palette::low());
            if (value > 0.0f)
                graphics.fillRect(cell);
            else
                graphics.drawRect(cell, 1);
        }
    }

    if (snapshot.rescue || snapshot.frozen)
    {
        graphics.setColour(design::Palette::paper());
        for (int x = area.getX(); x < area.getRight(); x += snapshot.frozen ? 12 : 8)
            graphics.drawVerticalLine(x, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    if (hasKeyboardFocus(true))
    {
        graphics.setColour(design::Palette::paper());
        graphics.drawRect(area, 2);
    }
}
