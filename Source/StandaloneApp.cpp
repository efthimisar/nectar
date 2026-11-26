// Nektar's own standalone application. Same as JUCE's default one, except that the audio input
// is NOT muted (Nektar never plays the input back; it only records it).
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"

namespace
{
class NektarApp final : public juce::JUCEApplication
{
public:
    NektarApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            quit();
            return;
        }

        // always open in Init: forget the last session's state (the audio device settings stay), and the old presets are cleared once, below
        appProperties.getUserSettings()->removeValue ("filterState");
        {   // once: presets made by earlier versions no longer work, so they go to the Trash (presets made after this stay)
            const auto mark = NektarProcessor::getPresetFolder().getChildFile (".cleared_v2");
            if (! mark.existsAsFile())
            {
                for (auto& f : NektarProcessor::listPresets()) f.moveToTrash();
                NektarProcessor::getPresetFolder().createDirectory();
                mark.create();
            }
        }

        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String {},
                                                                      nullptr, juce::Array<juce::StandalonePluginHolder::PluginInOuts>(), false);
        holder->getMuteInputValue() = false;
        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(), juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
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
        quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication() { return new NektarApp(); }
