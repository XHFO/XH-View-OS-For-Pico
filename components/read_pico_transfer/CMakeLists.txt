# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 独立热点传书组件与嵌入页面。/ Standalone hotspot transfer component and embedded page.
idf_component_register(SRCS "read_pico_transfer.c" "read_pico_connectivity.c" "read_pico_management.c" "read_pico_content.c"
    INCLUDE_DIRS "include" "."
    REQUIRES read_pico_home_link read_pico_search read_pico read_pico_apps mbedtls esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs esp_timer espressif__cjson)

# 中文：仅体积优化后台传输与配网协议，不改变Wi-Fi驱动、缓冲或校验。
# English: Size-optimize background transfer/provisioning only; retain Wi-Fi driver, buffers and validation.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

# 网页确定性压缩，避免占用TF或扩大APP分区。/ Deterministic web-page compression, without TF or partition changes.
idf_build_get_property(management_python PYTHON)
foreach(web_page manage upload)
    set(web_gzip "${CMAKE_CURRENT_BINARY_DIR}/${web_page}.html.gz")
    add_custom_command(OUTPUT "${web_gzip}"
        COMMAND ${management_python} "${CMAKE_CURRENT_LIST_DIR}/compress_management.py" "${CMAKE_CURRENT_LIST_DIR}/${web_page}.html" "${web_gzip}"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${web_page}.html"
                "${CMAKE_CURRENT_LIST_DIR}/assets/${web_page}.html.gz"
                "${CMAKE_CURRENT_LIST_DIR}/compress_management.py")
    add_custom_target(pico_${web_page}_html DEPENDS "${web_gzip}")
    add_dependencies(${COMPONENT_LIB} pico_${web_page}_html)
    target_add_binary_data(${COMPONENT_LIB} "${web_gzip}" BINARY)
endforeach()
