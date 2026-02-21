#include "aminals_parameter_list.hpp"

using namespace aminals;

Parameter_List::Parameter_List(Processor& p, juce::AudioProcessorValueTreeState::ParameterLayout&& layout) :
    tree(p, nullptr, juce::Identifier("ParameterList"), std::move(layout))
{

}
