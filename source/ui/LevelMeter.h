#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dumble::ui
{
/** Vertical peak meter fed from the editor's timer (values come from processor atomics). */
class LevelMeter final : public juce::Component
{
public:
    void setLevel (float linearPeak)
    {
        const auto db = juce::Decibels::gainToDecibels (linearPeak, -60.0f);
        const auto newLevel = juce::jmap (db, -60.0f, 6.0f, 0.0f, 1.0f);

        if (std::abs (newLevel - level) > 0.002f)
        {
            level = newLevel;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRoundedRectangle (b, 2.0f);

        const auto filled = b.withTop (b.getBottom() - b.getHeight() * juce::jlimit (0.0f, 1.0f, level));
        const auto zeroDbY = b.getBottom() - b.getHeight() * (60.0f / 66.0f);
        g.setColour (level > 60.0f / 66.0f ? juce::Colours::red : juce::Colour (0xff6fbf4a));
        g.fillRoundedRectangle (filled, 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawHorizontalLine ((int) zeroDbY, b.getX(), b.getRight());
    }

private:
    float level = 0.0f;
};
} // namespace dumble::ui
