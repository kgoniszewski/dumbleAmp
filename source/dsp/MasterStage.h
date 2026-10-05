#pragma once

#include <juce_dsp/juce_dsp.h>

#include "CircuitConstants.h"
#include "LinearNetwork.h"
#include "OnePole.h"
#include "Pots.h"

namespace dumble
{
/**
    ProcessorChain element: mix node -> C49 .01u -> Master (U41, 1M audio) with Accent (C50 .001u
    across the top of the pot, U42) -> C9 .02u -> first PI grid (R10 1M leak).

    The mix node is driven through its Thevenin resistance (R84 || R85 = 110k).
*/
class MasterStage
{
public:
    static constexpr int kUpdateInterval = 32;

    MasterStage()
    {
        using namespace circuit;
        constexpr int g = decltype (net)::ground;
        net.setNumNodes (numNodes);
        net.setSourceNode (source);

        net.addResistor (source, mix, kMixR * 0.5);              // R84 || R85
        net.addCapacitor (mix, top, kMixCoupling);               // C49
        eTop = net.addResistor (top, wiper, kMasterPot * 0.5);   // U41 A-wiper
        eBottom = net.addResistor (wiper, g, kMasterPot * 0.5);  // U41 wiper-B
        net.addCapacitor (top, accentNode, kAccentCap);          // C50
        eAccent = net.addResistor (accentNode, wiper, kSwitchOpen); // U42
        net.addCapacitor (wiper, grid, kPiInputCap);             // C9
        net.addResistor (grid, g, kPiGridLeak);                  // R10
    }

    void setMaster (float knob0to10) noexcept { master.setTargetValue (knob0to10); }
    void setAccent (bool on) noexcept
    {
        if (on != accent)
        {
            accent = on;
            changed = true;
        }
    }

    void prepare (const juce::dsp::ProcessSpec& spec) noexcept
    {
        master.reset (spec.sampleRate, 0.03);
        net.prepare (spec.sampleRate);
        reset();
    }

    void reset() noexcept
    {
        master.setCurrentAndTargetValue (master.getTargetValue());
        update();
        net.reset();
        countdown = 0;
    }

    template <typename Context>
    void process (const Context& context) noexcept
    {
        processMono (context, [this] (float x) noexcept { return processSample (x); });
    }

    float processSample (float mixVolts) noexcept
    {
        if (--countdown <= 0)
        {
            countdown = kUpdateInterval;
            if (master.isSmoothing())
                master.skip (kUpdateInterval);
            if (master.isSmoothing() || changed)
                update();
        }

        net.step (mixVolts);
        return (float) net.voltage (grid);
    }

private:
    enum { source, mix, top, wiper, accentNode, grid, numNodes };

    void update() noexcept
    {
        const auto r = potAudio (master.getCurrentValue());
        net.setValue (eTop, circuit::kMasterPot * (1.0 - r));
        net.setValue (eBottom, circuit::kMasterPot * r);
        net.setValue (eAccent, accent ? kSwitchClosed : kSwitchOpen);
        net.build();
        changed = false;
    }

    LinearNetwork<numNodes, 10> net;
    juce::SmoothedValue<float> master { 5.0f };
    int eTop = 0, eBottom = 0, eAccent = 0;
    bool accent = false, changed = true;
    int countdown = 0;
};
} // namespace dumble
