# Create an INTERFACE library for our C module.
add_library(usermod_mcufont INTERFACE)

# Add our source files to the lib
target_sources(usermod_mcufont INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modmcufont.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_bwfont.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_encoding.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_font.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_justify.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_kerning.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_rlefont.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_scaledfont.c
    ${CMAKE_CURRENT_LIST_DIR}/mcufont/decoder/mf_wordwrap.c


)

# Add the current directory as an include directory.
target_include_directories(usermod_mcufont INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_mcufont)
