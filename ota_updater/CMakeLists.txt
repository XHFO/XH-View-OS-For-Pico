cmake_minimum_required(VERSION 3.22)
set(PROJECT_VER "ota-updater-1")
set(EXTRA_COMPONENT_DIRS
    ../components/read_pico
    ../components/epdiy
    ../components/e0470_epaper_waveform
    ../components/sc7a20h
    ../components/cst836u
    ../components/read_pico_pmu
    ../components/sy7636a
    ../components/fca9555
    ../components/pwm_audio)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(ReadPicoUpdater)
