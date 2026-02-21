#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
namespace aminals
{
    class Sampler
    {
    public:
        Sampler();
        void prepare(double sampleRate, int samplesPerBlock);
        void process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& buffer);
        void release();
    };
}
