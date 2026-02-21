#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "aminals_processor.hpp"

namespace aminals
{
    class Editor : public juce::AudioProcessorEditor
    {
    public:
        explicit Editor (Processor&);
        ~Editor() override;

        //==============================================================================
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        // This reference is provided as a quick way for your editor to
        // access the processor object that created it.
        Processor& processorRef;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
    };
}
