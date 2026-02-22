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

void Sampler::process(juce::AudioBuffer<float>& samples, juce::MidiBuffer& buffer)
{
}

void Sampler::release()
{
}
