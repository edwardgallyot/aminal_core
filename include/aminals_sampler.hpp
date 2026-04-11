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
		void set_total_sizes(const unsigned long long* sizes, size_t count);
		void set_attack(float attack_seconds);
		void set_release(float release_seconds);
		void set_offsets(const unsigned long long* offsets, size_t count);
		void set_voice_assignments(long long* voice_assignments, size_t count);
		void set_midi_map(const char* midi_map, size_t count = 127);
		void set_sample_names(const char** sample_names, size_t count);
		void set_channel_strides(const unsigned long long* channel_strides, size_t count);
        void prepare(double sample_rate, int sample_per_block);
        void process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& buffer);
        void release();

        static constexpr size_t MemoryBytes = aminals::Arena<>::const_align(272722304, sizeof(size_t));
        using Arena = aminals::Arena<MemoryBytes>;
        struct Impl;
    private:
        Arena arena;
        Arena::Ptr<Impl> impl;
    };
}
