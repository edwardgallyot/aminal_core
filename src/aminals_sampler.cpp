#include <iostream>

#include "aminals_sampler.hpp"
#include "aminals_disk_streamer.hpp"

using namespace aminals;

struct Sampler::Impl
{
    Impl(Sampler::Arena& a) : arena(a)
    {
    }
    ~Impl()
    {
    }
    
    Sampler::Arena& arena;
    Disk_Streamer disk_streamer;
};

Sampler::Sampler()
    : arena(),
      impl(arena.make_unique<Sampler::Impl>(arena))
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
}

void Sampler::release()
{
    this->impl->disk_streamer.stop_streaming();
}
