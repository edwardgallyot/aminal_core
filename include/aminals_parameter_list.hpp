#pragma once 

#include "aminals_processor.hpp"

namespace aminals
{
    class Parameter_List
    {
    public:
        Parameter_List(Processor& p, juce::AudioProcessorValueTreeState::ParameterLayout&& layout);
		const juce::AudioProcessorValueTreeState& get_tree() { return this->tree; }
    private:
        juce::AudioProcessorValueTreeState tree; 
    };
}
