cmake_minimum_required(VERSION 3.21)
project(400Scalper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  # Static runtime: the .clap needs no Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
  option(SCALPER_WITH_GUI "Build the plugin window (WebView2)" ON)
else()
  option(SCALPER_WITH_GUI "Build the plugin window" OFF)   # Windows first; macOS/Linux UI later
endif()
option(SCALPER_BUILD_TESTS "Build DSP / plugin tests" ON)

# ---------------------------------------------------------------------------
# Dependencies (pinned). Set FETCHCONTENT_SOURCE_DIR_CLAP / _CHOC to use local copies.
include(FetchContent)
FetchContent_Declare(clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG a47f6badb49d948fd009998f28309cdab78979c9)   # CLAP 1.2.10
FetchContent_Declare(choc
  GIT_REPOSITORY https://github.com/Tracktion/choc.git
  GIT_TAG 8af031c2ba4f5723f4b0a5ca14e0b4984af15b0e)
FetchContent_GetProperties(clap)
if(NOT clap_POPULATED)
  FetchContent_Populate(clap)
endif()
FetchContent_GetProperties(choc)
if(NOT choc_POPULATED)
  FetchContent_Populate(choc)
endif()

# ---------------------------------------------------------------------------
# Embed the UI (HTML + font) into the binary.
set(UI_HTML ${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html)
set(UI_FONT ${CMAKE_CURRENT_SOURCE_DIR}/ui/fonts/SuiGenerisRg-Regular.ttf)
set(UI_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/UiResources.h)
add_custom_command(
  OUTPUT ${UI_HEADER}
  COMMAND ${CMAKE_COMMAND} -DOUT=${UI_HEADER} -DHTML=${UI_HTML} -DFONT=${UI_FONT}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedUi.cmake
  DEPENDS ${UI_HTML} ${UI_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedUi.cmake
  COMMENT "Embedding UI resources")
add_custom_target(scalper_ui_resources DEPENDS ${UI_HEADER})

# ---------------------------------------------------------------------------
set(SCALPER_SOURCES src/Plugin.cpp src/GuiJson.cpp)
if(SCALPER_WITH_GUI)
  list(APPEND SCALPER_SOURCES src/Gui.cpp)
endif()

add_library(400Scalper MODULE ${SCALPER_SOURCES})
add_dependencies(400Scalper scalper_ui_resources)
target_include_directories(400Scalper PRIVATE
  src ${clap_SOURCE_DIR}/include ${choc_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(400Scalper PRIVATE
  SCALPER_WITH_GUI=$<BOOL:${SCALPER_WITH_GUI}>
  SCALPER_VERSION="${PROJECT_VERSION}")
set_target_properties(400Scalper PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "400Scalper")

if(MSVC)
  target_compile_options(400Scalper PRIVATE /W3 /permissive- /utf-8 /fp:fast)
  target_compile_definitions(400Scalper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(400Scalper PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

if(WIN32)
  target_link_libraries(400Scalper PRIVATE ole32 oleaut32 shell32 shlwapi user32 version)
elseif(APPLE)
  set_target_properties(400Scalper PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap)
endif()

# ---------------------------------------------------------------------------
if(SCALPER_BUILD_TESTS)
  enable_testing()
  add_executable(dsp_test tests/dsp_test.cpp)
  target_include_directories(dsp_test PRIVATE src)
  add_test(NAME dsp_test COMMAND dsp_test)

  add_executable(host_test tests/host_test.cpp src/Plugin.cpp src/GuiJson.cpp)
  add_dependencies(host_test scalper_ui_resources)
  target_include_directories(host_test PRIVATE src ${clap_SOURCE_DIR}/include ${choc_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_compile_definitions(host_test PRIVATE SCALPER_WITH_GUI=0 SCALPER_VERSION="${PROJECT_VERSION}")
  if(MSVC)
    target_compile_options(host_test PRIVATE /utf-8)
    target_compile_definitions(host_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  endif()
  add_test(NAME host_test COMMAND host_test)
endif()
