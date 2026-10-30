# Embeds a binary file as a C array header.
#   cmake -DIN=<file> -DOUT=<header> -DNAME=<symbol> -P EmbedFile.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n  " bytes "${bytes}")
file(WRITE "${OUT}.tmp" "// Generated from ${IN} - do not edit.\n#pragma once\n#include <cstddef>\nstatic const unsigned char ${NAME}[] = {\n  ${bytes}\n};\nstatic const size_t ${NAME}Size = ${size};\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
