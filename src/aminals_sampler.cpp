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

void Sampler::set_streaming_file(const char* filename)
{
	this->impl->disk_streamer.set_streaming_file(filename);
}

void Sampler::prepare(double sample_rate, int samples_per_block)
{
    this->impl->disk_streamer.start_streaming(sample_rate, samples_per_block);
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
		if (!this->impl->disk_streamer.try_get_chunk(&streamed_chunk, channel, samples.getNumSamples()))
		{
			std::cout << "channel " << channel << " failed!" << std::endl;
		}
		float* write = samples.getWritePointer(channel);
		for (int sample = 0; sample < samples.getNumSamples(); ++sample)
		{
			write[sample] = streamed_chunk[sample];
		}
	}
}

void Sampler::release()
{
    this->impl->disk_streamer.stop_streaming();
}

static_assert(Sampler::MemoryBytes == sizeof(Sampler::Impl));
