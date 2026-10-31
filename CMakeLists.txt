cmake_minimum_required(VERSION 3.21)
project(400Scalper VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # Static runtime: the .clap needs no Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The plugin is built with iPlug2, which supports Windows and macOS. On Linux only the
# engine tests are built.
if(WIN32 OR APPLE)
  option(SCALPER_BUILD_PLUGIN "Build the 400Scalper plugin (iPlug2, CLAP)" ON)
else()
  option(SCALPER_BUILD_PLUGIN "Build the 400Scalper plugin (iPlug2, CLAP)" OFF)
endif()
option(SCALPER_BUILD_TESTS "Build the DSP / engine tests" ON)

# ---------------------------------------------------------------------------
if(SCALPER_BUILD_TESTS)
  enable_testing()
  foreach(t dsp_test spectral_test core_test)
    add_executable(${t} tests/${t}.cpp)
    target_include_directories(${t} PRIVATE src)
    if(MSVC)
      target_compile_options(${t} PRIVATE /utf-8)
      target_compile_definitions(${t} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    endif()
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

# ---------------------------------------------------------------------------
if(SCALPER_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
