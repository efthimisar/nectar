cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
project(Nektar VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(juce)

# Signalsmith Stretch (MIT) - pitch/time library, header-only. Fetched without its own CMake files.
FetchContent_Declare(sstretch
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da
  SOURCE_SUBDIR not-used)
FetchContent_Declare(slinear
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
  GIT_TAG 0.6.6
  GIT_SHALLOW ON
  SOURCE_SUBDIR not-used)
FetchContent_MakeAvailable(sstretch slinear)

juce_add_binary_data(NektarAssets SOURCES
  assets/Carlito-Regular.ttf
  assets/Carlito-Bold.ttf)

juce_add_plugin(Nektar
  COMPANY_NAME "Prisma Audio"
  PRODUCT_NAME "Nektar"
  BUNDLE_ID "audio.prisma.nektar"
  PLUGIN_MANUFACTURER_CODE Prsm
  PLUGIN_CODE Nktr
  FORMATS Standalone VST3
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  VST3_CATEGORIES Instrument Synth
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Nektar needs the microphone to record sounds into your library."
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Nektar PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/SamplePlayer.cpp
  Source/SpectralStretch.cpp
  Source/SineModel.cpp
  Source/SampleCleaner.cpp)

# Own standalone app (so the microphone input is not muted by default and we can go full screen later)
target_sources(Nektar_Standalone PRIVATE Source/StandaloneApp.cpp)
target_compile_definitions(Nektar_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_include_directories(Nektar SYSTEM PRIVATE
  ${sstretch_SOURCE_DIR}/include
  ${slinear_SOURCE_DIR}/include)

target_compile_definitions(Nektar PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Nektar PRIVATE
  NektarAssets
  juce::juce_audio_utils
  juce::juce_dsp
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
