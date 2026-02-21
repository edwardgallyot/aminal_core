#pragma once 

#include "aminals_processor.hpp"

namespace aminals
{
    class Parameter_List
    {
    public:
        Parameter_List(Processor& p, juce::AudioProcessorValueTreeState::ParameterLayout&& layout);
    private:
        juce::AudioProcessorValueTreeState tree; 
    };
}
