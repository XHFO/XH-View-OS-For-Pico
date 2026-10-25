# 共享模型与TLS适配；PICO_HOME_LINK_FULL=OFF 时仅薄宿主桩+JSON隔离器。
# Shared model/TLS adapter; with PICO_HOME_LINK_FULL=OFF only thin-host stubs + JSON isolator.
if(NOT DEFINED PICO_HOME_LINK_FULL)
    set(PICO_HOME_LINK_FULL OFF CACHE BOOL "Statically link full Home Assistant/printer stack")
endif()
if(PICO_HOME_LINK_FULL)
    set(HOME_LINK_SRCS "home_link_model.c" "home_link_json.c" "home_link_web.c" "bambu_auth.c" "read_pico_home_link.c")
    set(HOME_LINK_REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_timer espressif__cjson)
else()
    set(HOME_LINK_SRCS "home_link_stubs.c" "home_link_json.c")
    set(HOME_LINK_REQUIRES nvs_flash esp_timer espressif__cjson)
endif()
idf_component_register(SRCS ${HOME_LINK_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${HOME_LINK_REQUIRES})
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
idf_component_get_property(home_cjson_dir espressif__cjson COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${home_cjson_dir}/cJSON")
if(PICO_HOME_LINK_FULL)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PICO_HOME_LINK_FULL=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PICO_HOME_LINK_FULL=0)
endif()
