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

		void set_streaming_file(const char* filename);
        void prepare(double sample_rate, int sample_per_block);
        void process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& buffer);
        void release();

        static constexpr size_t MemoryBytes = aminals::Arena<>::const_align(4243648, sizeof(size_t));
        using Arena = aminals::Arena<MemoryBytes>;
        struct Impl;
    private:
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
