# Writes assets.c: the font and every SYN-OS theme file as C arrays, so the
# program is one file with nothing beside it. Run by CMakeLists.txt with
# -DFONT=, -DTHEME_DIR= and -DOUT=.
file(READ "${FONT}" hex HEX)
string(LENGTH "${hex}" hexlen)
math(EXPR font_len "${hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font "${hex}")

file(GLOB themes "${THEME_DIR}/*.theme")
list(SORT themes)
set(text "")
foreach(t ${themes})
  file(READ "${t}" one)
  string(APPEND text "${one}\n")
endforeach()
file(WRITE "${OUT}.themes" "${text}")
file(READ "${OUT}.themes" hex HEX)
file(REMOVE "${OUT}.themes")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," theme_bytes "${hex}")

file(WRITE "${OUT}"
  "/* Made by cmake/embed.cmake. */\n"
  "#include \"assets.h\"\n"
  "const unsigned char ASSET_FONT[] = {${font}};\n"
  "const int ASSET_FONT_LEN = ${font_len};\n"
  "const char ASSET_THEMES[] = {${theme_bytes}0};\n")
