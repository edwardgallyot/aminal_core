project(aminal_core LANGUAGES CXX)

add_library(aminal_core STATIC
    src/aminals_editor.cpp
    src/aminals_processor.cpp
    src/aminals_parameter_list.cpp
    src/aminals_sampler.cpp
    src/aminals_disk_streamer.cpp
)

target_link_libraries(aminal_core PRIVATE
    juce::juce_core
)

target_include_directories(aminal_core
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src
)

set_target_properties(aminal_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(
    aminal::core ALIAS aminal_core
)

