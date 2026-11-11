# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
option(PICO_OTA_UPDATER "Build the small ota_1 TF updater" OFF)
if(PICO_OTA_UPDATER)
    set(PROJECT_VER "ota-updater-1")
else()
    set(PROJECT_VER "0.40.0-dual98")
endif()
# OFF：家庭 HA/打印机静态栈改为 TF tools/home-link.pga；固件仅薄宿主空状态。
# OFF: Home HA/printer static stack moves to TF tools/home-link.pga; firmware keeps thin empty host.
set(PICO_HOME_LINK_FULL OFF CACHE BOOL "Statically link full Home Assistant/printer stack")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
if(PICO_OTA_UPDATER)
    project(Read_Pico LANGUAGES C)
else()
    project(Read_Pico)
endif()

# 离线构建关闭组件管理器时，补上清单中的公开依赖。
# Restore the manifest's public dependency for offline builds without the component manager.
if("$ENV{IDF_COMPONENT_MANAGER}" STREQUAL "0")
    idf_component_get_property(esp_tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB)
    idf_component_get_property(tinyusb_lib espressif__tinyusb COMPONENT_LIB)
    target_link_libraries(${esp_tinyusb_lib} PUBLIC ${tinyusb_lib})
endif()

# 固件描述时间统一为 UTC，与概览页标识一致；保留已有编译缓存启动器。
# Compile app-description timestamps in UTC to match Overview; preserve any compiler-cache launcher.
idf_component_get_property(app_desc_lib esp_app_format COMPONENT_LIB)
get_target_property(app_desc_launcher ${app_desc_lib} C_COMPILER_LAUNCHER)
if(NOT app_desc_launcher)
    set(app_desc_launcher "")
endif()
set(app_desc_utc_launcher "${CMAKE_COMMAND}" -E env TZ=UTC)
if(app_desc_launcher)
    list(APPEND app_desc_utc_launcher ${app_desc_launcher})
endif()
set_property(TARGET ${app_desc_lib} PROPERTY C_COMPILER_LAUNCHER "${app_desc_utc_launcher}")
