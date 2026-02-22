#include <iostream>

#include "aminals_sampler.hpp"

using namespace aminals;

Sampler::Sampler()
    : arena()
{
}

void Sampler::prepare(double sampleRate, int samplesPerBlock)
{
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
}
