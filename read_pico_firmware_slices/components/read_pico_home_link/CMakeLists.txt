# 共享模型与TLS适配；默认不启动工作器。/ Shared model and TLS adapter; no worker starts by default.
idf_component_register(SRCS "home_link_model.c" "home_link_json.c" "home_link_web.c" "bambu_auth.c" "read_pico_home_link.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_timer espressif__cjson)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)
idf_component_get_property(home_cjson_dir espressif__cjson COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${home_cjson_dir}/cJSON")
