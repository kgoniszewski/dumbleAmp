// Custom standalone application (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1).
//
// Identical in behaviour to JUCE's default StandaloneFilterApp, plus:
//  - on first launch it looks for an IK Multimedia AXE I/O One and pre-selects it
//    (input 1 = Hi-Z instrument input, outputs 1+2, 48 kHz, 64-sample buffer);
//  - "mute input" defaults to OFF, because the input is a guitar, not a microphone.
// Once the user has changed anything in Audio Settings, the saved state wins.

#include <optional>

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_utils/juce_audio_utils.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#if JucePlugin_Build_Standalone

namespace dumble
{
class StandaloneApp final : public juce::JUCEApplication
{
public:
    StandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return false; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto* settings = appProperties.getUserSettings();

        if (! settings->containsKey ("shouldMuteInput"))
            settings->setValue ("shouldMuteInput", false);

        const auto preferred = findPreferredSetup();

        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            settings, false, juce::String(), preferred.has_value() ? &*preferred : nullptr);

        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            std::move (holder));

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    /** Returns a setup targeting the AXE I/O One if one is connected. */
    static std::optional<juce::AudioDeviceManager::AudioDeviceSetup> findPreferredSetup()
    {
        juce::AudioDeviceManager probe;
        juce::OwnedArray<juce::AudioIODeviceType> types;
        probe.createAudioDeviceTypes (types);

        for (auto* type : types)
        {
            type->scanForDevices();

            const auto inName  = findDevice (type->getDeviceNames (true));
            const auto outName = findDevice (type->getDeviceNames (false));

            if (inName.isEmpty() || outName.isEmpty())
                continue;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.inputDeviceName  = inName;
            setup.outputDeviceName = outName;
            setup.sampleRate = 48000.0;
            setup.bufferSize = 64;
            setup.useDefaultInputChannels  = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.setRange (0, 1, true);  // input 1: Hi-Z instrument
            setup.outputChannels.setRange (0, 2, true); // outputs 1+2 carry the same mono signal
            return setup;
        }

        return std::nullopt;
    }

    static juce::String findDevice (const juce::StringArray& names)
    {
        for (const auto& name : names)
            if (name.containsIgnoreCase ("AXE I/O") || name.containsIgnoreCase ("AXE IO"))
                return name;

        return {};
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
} // namespace dumble

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new dumble::StandaloneApp(); }

#endif
