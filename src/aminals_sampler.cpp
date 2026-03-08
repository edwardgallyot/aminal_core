#include <iostream>

#include "aminals_sampler.hpp"
#include "aminals_disk_streamer.hpp"

using namespace aminals;

struct Sampler::Impl
{
    Impl() : disk_streamer()
    {
    }
    ~Impl()
    {
    }
    
    Disk_Streamer disk_streamer;
};

Sampler::Sampler()
    : arena(),
      impl(arena.make_unique<Sampler::Impl>())
{
}

Sampler::~Sampler()
{
}

void Sampler::prepare(double sampleRate, int samplesPerBlock)
{
    this->impl->disk_streamer.start_streaming();
}

void Sampler::process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& midi)
{
    for (const auto& metadata : midi)
    {
        auto message = metadata.getMessage();
        // For MIDI message debugging
        // std::cout << message.getDescription() << std::endl;
    }

	float* streamed_chunk;

	for (int channel = 0; channel < samples.getNumChannels(); ++channel)
	{
		this->impl->disk_streamer.try_get_chunk(&streamed_chunk, channel, samples.getNumSamples());
		float* write = samples.getWritePointer(channel);
		for (int sample = 0; sample < samples.getNumSamples(); ++sample)
		{
			write[sample] = 0.0f;
		}
	}
}

void Sampler::release()
{
    this->impl->disk_streamer.stop_streaming();
}

static_assert(Sampler::MemoryBytes >= sizeof(Sampler::Impl));
