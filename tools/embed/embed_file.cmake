# Embeds a file as a byte array. Byte-exact (no text-mode/encoding changes).
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<c identifier> -DNAMESPACE=<ns> -P embed_file.cmake
# Defines: const unsigned char <SYMBOL>[]; const std::size_t <SYMBOL>_size;
cmake_minimum_required(VERSION 3.28)
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" n)
math(EXPR size "${n} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")
get_filename_component(src_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
"// Generated from ${src_name} by tools/embed/embed_file.cmake. Do not edit.\n"
"#include <cstddef>\n"
"namespace ${NAMESPACE} {\n"
"extern const unsigned char ${SYMBOL}[];\n"
"extern const std::size_t ${SYMBOL}_size;\n"
"const unsigned char ${SYMBOL}[] = {\n    ${bytes}0x00\n};\n"
"const std::size_t ${SYMBOL}_size = ${size};\n"
"} // namespace ${NAMESPACE}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
