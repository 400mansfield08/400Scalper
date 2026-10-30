# 400Scalper plugin — iPlug2 + IGraphics (NanoVG / OpenGL 2 on Windows).
#
# FORMATS: CLAP only for now. To add more later, list them below (e.g. CLAP VST3 AU) and
# provide the SDKs iPlug2 needs for them; config.h already carries the VST3 / AU / AAX fields.
set(SCALPER_FORMATS CLAP)

include(FetchContent)

# ---- pinned dependencies -----------------------------------------------------
set(IPLUG2_COMMIT d82b2e99b17c8f723d3f6e2a88ffd8b656f28e5d)
set(CLAP_COMMIT a47f6badb49d948fd009998f28309cdab78979c9)          # CLAP 1.2.10
set(CLAP_HELPERS_COMMIT a380c978f429103e08afc1d17e9efbf3e3c96ee2)

FetchContent_Declare(iplug2
  URL https://github.com/iPlug2/iPlug2/archive/${IPLUG2_COMMIT}.tar.gz
  SOURCE_SUBDIR _no_cmake_)
FetchContent_Declare(clap_sdk
  URL https://github.com/free-audio/clap/archive/${CLAP_COMMIT}.tar.gz
  SOURCE_SUBDIR _no_cmake_)
FetchContent_Declare(clap_helpers
  URL https://github.com/free-audio/clap-helpers/archive/${CLAP_HELPERS_COMMIT}.tar.gz
  SOURCE_SUBDIR _no_cmake_)
FetchContent_MakeAvailable(iplug2 clap_sdk clap_helpers)

# iPlug2 looks for the CLAP SDK + helpers inside its own Dependencies folder.
file(COPY ${clap_sdk_SOURCE_DIR}/include DESTINATION ${iplug2_SOURCE_DIR}/Dependencies/IPlug/CLAP_SDK)
file(COPY ${clap_helpers_SOURCE_DIR}/include DESTINATION ${iplug2_SOURCE_DIR}/Dependencies/IPlug/CLAP_HELPERS)

set(IPLUG2_DIR ${iplug2_SOURCE_DIR})
include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# ---- embedded font -------------------------------------------------------------
set(SCALPER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(SCALPER_FONT ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/SuiGenerisRg-Regular.ttf)
add_custom_command(
  OUTPUT ${SCALPER_GEN_DIR}/FontData.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SCALPER_GEN_DIR}
  COMMAND ${CMAKE_COMMAND} -DIN=${SCALPER_FONT} -DOUT=${SCALPER_GEN_DIR}/FontData.h -DNAME=kSuiGenerisTtf
          -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
  DEPENDS ${SCALPER_FONT} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
  COMMENT "Embedding the Sui Generis font")

# ---- version (from the top-level project) -----------------------------------------
math(EXPR SCALPER_VERSION_HEX "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT HEXADECIMAL)

# iPlug2 target names must start with a letter: the targets are "Scalper-*",
# the output file is 400Scalper.clap.
iplug_add_plugin(Scalper
  SOURCES
    Scalper.cpp
    Scalper.h
    config.h
    ui/Model.h
    ui/Layout.h
    ui/Widgets.h
    ui/Scope.h
    ui/Bands.h
    ui/ScalperUI.h
    ${SCALPER_GEN_DIR}/FontData.h
  FORMATS ${SCALPER_FORMATS}
  DEFINES
    SCALPER_VERSION_STR="${PROJECT_VERSION}"
    SCALPER_VERSION_HEX=${SCALPER_VERSION_HEX}
)

foreach(fmt ${SCALPER_FORMATS})
  string(TOLOWER ${fmt} f)
  if(TARGET Scalper-${f})
    target_include_directories(Scalper-${f} PRIVATE ${PROJECT_SOURCE_DIR}/src ${SCALPER_GEN_DIR})
    set_target_properties(Scalper-${f} PROPERTIES OUTPUT_NAME "400Scalper")
    if(MSVC)
      target_compile_options(Scalper-${f} PRIVATE /utf-8 /fp:fast)
      target_compile_definitions(Scalper-${f} PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    endif()
  endif()
endforeach()
