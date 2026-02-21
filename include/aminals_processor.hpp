#pragma once 

#include <juce_audio_processors/juce_audio_processors.h>

namespace aminals
{
    class Processor : public juce::AudioProcessor
    {
    public:
        Processor();
        ~Processor() override;

        virtual juce::AudioProcessorEditor* create_editor() = 0;
        virtual const juce::String get_name() const = 0;
        
        virtual void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        virtual void releaseResources() override;

        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

        virtual void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;

        const juce::String getName() const override;

        bool acceptsMidi() const override;
        bool producesMidi() const override;
        bool isMidiEffect() const override;
        double getTailLengthSeconds() const override;

        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int index, const juce::String& newName) override;

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
    };
}
