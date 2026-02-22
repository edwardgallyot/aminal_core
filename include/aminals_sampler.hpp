#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "aminals_arena.hpp"

namespace aminals
{
    class Sampler
    {
    public:
        Sampler();
        ~Sampler();
        
        void prepare(double sampleRate, int samplesPerBlock);
        void process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& buffer);
        void release();
        
        static constexpr size_t MemoryBytes = 1024;
        using Arena = aminals::Arena<MemoryBytes>;
        
    private:
        struct Impl;
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
