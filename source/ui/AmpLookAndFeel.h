#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dumble::ui
{
/** Black tolex / silver panel styling with chicken-head-ish knobs. */
class AmpLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AmpLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       panelText);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,               panelText);
        setColour (juce::ToggleButton::textColourId,        panelText);
        setColour (juce::ToggleButton::tickColourId,        jewel);
        setColour (juce::ComboBox::backgroundColourId,      juce::Colour (0xff1c1c1c));
        setColour (juce::ComboBox::textColourId,            juce::Colours::white);
        setColour (juce::TextButton::buttonColourId,        juce::Colour (0xff2a2a2a));
        setColour (juce::TextButton::textColourOffId,       juce::Colours::white);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);

        // scale ticks 0..10
        g.setColour (panelText.withAlpha (0.6f));
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const auto p1 = centre.getPointOnCircumference (radius * 0.92f, a);
            const auto p2 = centre.getPointOnCircumference (radius, a);
            g.drawLine ({ p1, p2 }, 1.5f);
        }

        // knob body
        const auto knobR = radius * 0.78f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), centre.translated (-knobR, -knobR),
                                                 juce::Colour (0xff0d0d0d), centre.translated (knobR, knobR), false));
        g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre));
        g.setColour (juce::Colour (0xff6b6b6b));
        g.drawEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre), 1.2f);

        // pointer
        g.setColour (juce::Colours::white);
        g.drawLine ({ centre.getPointOnCircumference (knobR * 0.25f, angle),
                      centre.getPointOnCircumference (knobR * 0.95f, angle) }, 2.5f);
    }

    void drawFaceplate (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        g.fillAll (juce::Colour (0xff121212));

        const auto plate = area.toFloat().reduced (10.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3b3e), plate.getTopLeft(),
                                                 juce::Colour (0xff1f1f22), plate.getBottomLeft(), false));
        g.fillRoundedRectangle (plate, 8.0f);
        g.setColour (juce::Colour (0xff8a8a8a));
        g.drawRoundedRectangle (plate, 8.0f, 1.0f);

        const auto scale = (float) area.getWidth() / 900.0f;
        g.setColour (jewel);
        g.setFont (juce::FontOptions (30.0f * scale).withStyle ("Italic"));
        g.drawText (title, area.withHeight (juce::roundToInt (70 * scale)).reduced (juce::roundToInt (28 * scale), 0),
                    juce::Justification::centredLeft);

        g.setColour (panelText.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.0f * scale));
        g.drawText ("circuit-modelled SSS 002  |  5751 / 7025 / 4x6L6GC",
                    area.withHeight (juce::roundToInt (70 * scale)).reduced (juce::roundToInt (28 * scale), 0),
                    juce::Justification::centredRight);
    }

private:
    const juce::Colour panelText { 0xffd8d4c8 };
    const juce::Colour jewel { 0xffc9a64a };
};
} // namespace dumble::ui
