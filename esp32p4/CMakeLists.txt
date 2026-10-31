cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET "esp32p4")
set(PROJECT_VER "0.1.1")
include(${CMAKE_CURRENT_SOURCE_DIR}/../esp-devkit/devkit.cmake)
devkit_idf_init(COMPONENT_DIRS ../components ../app)
project(Tab5fMP3Player)

# This image is installed through the resident Tab5 Console into ota_1.
# Block direct flash targets so an accidental command cannot overwrite Console,
# the factory C6 image or the shared partition table.
add_custom_target(tab5_fmp3_sd_only
    COMMAND ${CMAKE_COMMAND} -E echo
            "fMP3 Player is an SD app; install it from the Tab5 Console SD Apps library."
    COMMAND ${CMAKE_COMMAND} -E false)
foreach(flash_target flash app-flash bootloader-flash partition-table-flash)
    if(TARGET ${flash_target})
        add_dependencies(${flash_target} tab5_fmp3_sd_only)
    endif()
endforeach()
