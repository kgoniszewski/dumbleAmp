#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ac30::ui
{
/** Copper control panel over a diamond grille cloth, cream "chicken head" knobs. */
class Ac30LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    Ac30LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,    ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,            ink);
        setColour (juce::ToggleButton::textColourId,     ink);
        setColour (juce::ToggleButton::tickColourId,     juce::Colour (0xff7a1c12));
        setColour (juce::ToggleButton::tickDisabledColourId, ink.withAlpha (0.6f));
        setColour (juce::ComboBox::backgroundColourId,   juce::Colour (0xff2a1d14));
        setColour (juce::ComboBox::textColourId,         cream);
        setColour (juce::ComboBox::outlineColourId,      ink.withAlpha (0.5f));
        setColour (juce::PopupMenu::backgroundColourId,  juce::Colour (0xff2a1d14));
        setColour (juce::PopupMenu::textColourId,        cream);
        setColour (juce::TextButton::buttonColourId,     juce::Colour (0xff2a1d14));
        setColour (juce::TextButton::textColourOffId,    cream);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (5.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + pos * (endAngle - startAngle);

        // scale 0..10
        g.setColour (ink.withAlpha (0.75f));
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            g.drawLine ({ centre.getPointOnCircumference (radius * 0.9f, a),
                          centre.getPointOnCircumference (radius, a) }, i % 5 == 0 ? 2.0f : 1.2f);
        }

        // skirt + chicken-head pointer
        const auto skirt = radius * 0.62f;
        g.setColour (juce::Colour (0xff1b1b1b));
        g.fillEllipse (juce::Rectangle<float> (skirt * 2.0f, skirt * 2.0f).withCentre (centre));

        juce::Path head;
        head.addRoundedRectangle (-skirt * 0.22f, -skirt * 1.18f, skirt * 0.44f, skirt * 2.0f, skirt * 0.2f);
        head.addEllipse (-skirt * 0.45f, -skirt * 0.45f, skirt * 0.9f, skirt * 0.9f);
        const auto transform = juce::AffineTransform::rotation (angle).translated (centre);
        g.setGradientFill (juce::ColourGradient (cream, centre.translated (-skirt, -skirt),
                                                 juce::Colour (0xffb9ad92), centre.translated (skirt, skirt), false));
        g.fillPath (head, transform);
        g.setColour (juce::Colour (0xff5d5446));
        g.strokePath (head, juce::PathStrokeType (1.0f), transform);
        g.setColour (ink);
        g.drawLine ({ centre.getPointOnCircumference (skirt * 0.2f, angle),
                      centre.getPointOnCircumference (skirt * 1.05f, angle) }, 1.6f);
    }

    /** Grille cloth with the copper control strip at the top (height in px). */
    void drawCabinet (juce::Graphics& g, juce::Rectangle<int> area, int panelHeight, float scale)
    {
        // black tolex
        g.fillAll (juce::Colour (0xff141210));

        // diamond grille cloth
        auto cloth = area.toFloat().reduced (12.0f * scale).withTrimmedTop ((float) panelHeight + 6.0f * scale);
        g.setColour (juce::Colour (0xff3b2a23));
        g.fillRoundedRectangle (cloth, 4.0f);
        g.saveState();
        g.reduceClipRegion (cloth.toNearestInt());
        const auto step = 22.0f * scale;
        g.setColour (juce::Colour (0xff6b5546).withAlpha (0.55f));
        for (auto d = -cloth.getHeight(); d < cloth.getWidth() + cloth.getHeight(); d += step)
        {
            g.drawLine (cloth.getX() + d, cloth.getY(), cloth.getX() + d + cloth.getHeight(), cloth.getBottom(), 1.2f);
            g.drawLine (cloth.getX() + d, cloth.getBottom(), cloth.getX() + d + cloth.getHeight(), cloth.getY(), 1.2f);
        }
        g.restoreState();

        // copper control panel
        const auto panel = area.toFloat().reduced (12.0f * scale).withHeight ((float) panelHeight);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd59a63), panel.getTopLeft(),
                                                 juce::Colour (0xff9c6234), panel.getBottomLeft(), false));
        g.fillRoundedRectangle (panel, 5.0f);
        g.setColour (juce::Colour (0xff5a3518));
        g.drawRoundedRectangle (panel, 5.0f, 1.5f);
    }

    void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, float scale)
    {
        g.setColour (ink);
        g.setFont (juce::FontOptions (12.5f * scale).withStyle ("Bold"));
        g.drawText (text, area, juce::Justification::centred);
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX() + 8.0f * scale, (float) area.getRight() - 8.0f * scale);
    }

    void drawNameplate (juce::Graphics& g, juce::Rectangle<int> area, float scale)
    {
        g.setColour (cream);
        g.setFont (juce::FontOptions (30.0f * scale).withStyle ("Bold"));
        g.drawText ("AC30 C2", area, juce::Justification::centred);
        g.setColour (cream.withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.0f * scale));
        g.drawText ("circuit-modelled Top Boost  |  3 x 12AX7 / 4 x EL84", area.translated (0, juce::roundToInt (26 * scale)),
                    juce::Justification::centred);
    }

    const juce::Colour ink { 0xff2b1a0e };
    const juce::Colour cream { 0xfff1e6cf };
};
} // namespace ac30::ui
