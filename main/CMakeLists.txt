set(main_requires
    espressif__cmake_utilities
    espressif__tinyusb
    read_pico
    read_pico_transfer
    read_pico_usb
    read_pico_ble_pager
    read_pico_home_link
    read_pico_apps
    esp_http_client
    esp-tls
    read_pico_search
    espressif__qrcode
    continuous_du
    e0470_epaper_waveform
    e0470_page_turn
    read_pico_pmu
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_app_format
    esp_netif
)
idf_component_register(
    SRCS
        "app_main.c"
        "display.c"
        "sleep.c"
        "settings.c"
        "app/app_loop.c"
        "app/app_touch_buffer.c"
        "app/app_touch_guard.c"
        "app/app_registry.c"
        "system/pico_system.c"
        "system/dual_link.c"
        "system/pico_dual.c"
        "system/pico_connectivity.c"
        "system/pico_storage.c"
        "system/pico_usb.c"
        "system/pico_bluetooth.c"
        "system/pico_datetime.c"
        "system/pico_power.c"
        "system/pico_sound.c"
        "system/pico_navigation.c"
        "system/pico_ink.c"
        "system/pico_desktop.c"
        "system/pico_board_data.c"
        "system/pico_board_render.c"
        "system/pico_weread.c"
        "system/pico_weread_cover.c"
        "system/pico_launcher.c"
        "system/pico_lock_covers.c"
        "system/pico_maintenance.c"
        "system/pico_draw.c"
        "system/pico_reader.c"
        "system/pico_home.c"
        "system/pico_home_pga.c"
        "system/pico_home_portal.c"
        "system/pico_i18n.c"
        "system/pico_games.c"
        "ui/ui_kit.c"
        "ui/ui_image.c"
        "ui/ui_packed_asset.c"
        "ui/ui_gesture.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "apps/app_home.c"
        "apps/app_refresh.c"
        "apps/app_book.c"
        "apps/app_transfer.c"
        "book/book_store.c"
        "book/book_progress.c"
        "book/book_source.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_cover.c"
        "book/book_image.c"
        "book/vendor/tjpgd.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/gbk.c"
        "apps/app_touch.c"
        "apps/app_axis.c"
        "apps/app_power.c"
        "apps/app_ioe.c"
        "apps/app_pmu.c"
        "apps/app_sd.c"
        "apps/app_font_pick.c"
        "apps/app_sleep.c"
        "apps/app_selftest.c"
        "apps/app_key.c"
        "font/ttf_font.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c"
    REQUIRES ${main_requires}
    INCLUDE_DIRS "." "app" "apps" "font" "ui" "factory" "book" "system"
    EMBED_FILES
        "assets/builtin.ttf"
        "assets/serif.ttf"
        "assets/kai.ttf"
)

# 中文：为云端登录腾空间，启动/锁屏P4B共用无损外层；原资源字节不变，生成依赖必须在嵌入前完成。
# English: Make room for cloud login with a shared lossless splash/lock wrapper; preserve original bytes and generate before embedding.
idf_build_get_property(boot_image_python PYTHON)
# 双语源表和压缩包必须匹配，禁止悄悄嵌入旧文案。/ Reject stale bilingual catalog output.
add_custom_target(pico_i18n_catalog_check
    COMMAND ${boot_image_python} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_pico_i18n.py" --check
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/system/pico_i18n_table.inc"
        "${CMAKE_CURRENT_LIST_DIR}/system/pico_i18n_packed.inc"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_pico_i18n.py"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} pico_i18n_catalog_check)
# 手机门户只嵌入同源gzip，构建时拒绝过期生成文件。/ Reject stale portal gzip assets before compilation.
add_custom_target(pico_home_portal_asset_check
    COMMAND ${boot_image_python} "${CMAKE_CURRENT_LIST_DIR}/../tools/build_home_portal_asset.py" --check
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../components/read_pico_home_link/assets/home_link_portal.html"
        "${CMAKE_CURRENT_LIST_DIR}/../components/read_pico_home_link/home_link_portal_html_gzip.inc"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/build_home_portal_asset.py"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} pico_home_portal_asset_check)
set(pico_packed_image_outputs)
foreach(pico_image_name loading lock)
    set(pico_image_deflate "${CMAKE_CURRENT_BINARY_DIR}/${pico_image_name}_4bpp.p4b.deflate")
    add_custom_command(OUTPUT "${pico_image_deflate}"
        COMMAND ${boot_image_python} "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_boot_image.py"
            "${CMAKE_CURRENT_LIST_DIR}/assets/${pico_image_name}_4bpp.p4b" "${pico_image_deflate}"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/${pico_image_name}_4bpp.p4b"
            "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_boot_image.py"
        VERBATIM)
    list(APPEND pico_packed_image_outputs "${pico_image_deflate}")
    target_add_binary_data(${COMPONENT_LIB} "${pico_image_deflate}" BINARY)
endforeach()
add_custom_target(pico_boot_image_deflate DEPENDS ${pico_packed_image_outputs})
add_dependencies(${COMPONENT_LIB} pico_boot_image_deflate)

# 中文：只压缩冷配置/设置控制器；阅读排版、字库栅格、触摸、显示波形与游戏热路径保留项目O2。
# English: Size-optimize only cold configuration/settings controllers; keep reader/font raster, touch/display waveforms and game hot paths at project O2.
set_source_files_properties(
    "settings.c"
    "system/pico_home.c"
    "system/pico_home_pga.c"
    "system/pico_home_portal.c"
    "system/pico_i18n.c"
    "system/pico_navigation.c"
    "system/pico_connectivity.c"
    "system/pico_storage.c"
    "system/pico_usb.c"
    "system/pico_bluetooth.c"
    "system/pico_datetime.c"
    "system/pico_power.c"
    "system/pico_lock_covers.c"
    "system/pico_maintenance.c"
    "system/pico_desktop.c"
    "system/pico_board_data.c"
    "system/pico_board_render.c"
    "system/pico_weread.c"
    "system/pico_weread_cover.c"
    "system/pico_launcher.c"
    "system/pico_games.c"
    "apps/app_transfer.c"
    "apps/app_font_pick.c"
    "ui/ui_menu.c"
    "ui/ui_wifi_qr.c"
    "ui/ui_packed_asset.c"
    PROPERTIES COMPILE_OPTIONS "-Os"
)

# 中文：诊断页与应用装载只在显式操作时执行；缩小控制器以保持原4MiB APP槽，不动输入、排版、字库和面板热路径。
# English: Diagnostic pages and module loading run on explicit actions; shrink controllers within the original 4MiB APP slot without changing input, layout, fonts or panel hot paths.
set_source_files_properties(
    "apps/app_home.c"
    "apps/app_axis.c"
    "apps/app_ioe.c"
    "apps/app_pmu.c"
    "apps/app_sd.c"
    "apps/app_sleep.c"
    "apps/app_selftest.c"
    "apps/app_key.c"
    "ui/datamatrix.c"
    PROPERTIES COMPILE_OPTIONS "-Os"
)
idf_component_get_property(pico_loader_lib read_pico_apps COMPONENT_LIB)
target_compile_options(${pico_loader_lib} PRIVATE -Os)

# 中文：TLS/HTTP仅在后台连接工作器执行，协议代码单独-Os；不改变加密算法、证书或校验策略。
# English: TLS/HTTP execute in background connection workers; size-optimize protocol code without changing algorithms, certificates or verification.
# 中文：TLS/HTTP仅在后台连接工作器执行，协议代码单独-Os；不改变加密算法、证书或校验策略。
# English: TLS/HTTP execute in background connection workers; size-optimize protocol code without changing algorithms, certificates or verification.
# WeRead keeps esp_http_client/esp-tls even when home_link is thin-stubbed.
foreach(home_protocol_component esp_http_client esp_http_server esp-tls tcp_transport mbedtls)
    idf_component_get_property(home_protocol_lib ${home_protocol_component} COMPONENT_LIB)
    target_compile_options(${home_protocol_lib} PRIVATE -Os)
endforeach()

# 中文：本地账户存储和网络管理属于后台冷路径；保持原分区和阅读/输入/面板热路径。
# English: Local-account storage and network management are background cold paths; preserve partitions and reader/input/panel hot paths.
foreach(pico_configuration_component espressif__cjson nvs_flash esp_netif esp_event lwip)
    idf_component_get_property(pico_configuration_lib ${pico_configuration_component} COMPONENT_LIB)
    target_compile_options(${pico_configuration_lib} PRIVATE -Os)
endforeach()
foreach(home_crypto_target mbedtls mbedx509 tfpsacrypto builtin p256-m extras platform utilities)
    if(TARGET ${home_crypto_target})
        target_compile_options(${home_crypto_target} PRIVATE -Os)
    endif()
endforeach()

# 中文：后台HTTP解析与USB传输单独优化体积，保持协议、缓冲、任务让步和存储所有权规则不变。
# English: Size-optimize background HTTP parsing and USB transport, preserving protocols, buffers, task yielding and storage ownership.
foreach(pico_background_component http_parser read_pico_usb espressif__tinyusb espressif__esp_tinyusb)
    idf_component_get_property(pico_background_lib ${pico_background_component} COMPONENT_LIB)
    target_compile_options(${pico_background_lib} PRIVATE -Os)
endforeach()

# 中文：JSON解析与一次性二维码编码只服务后台/配置，保持算法和输出不变。
# English: JSON parsing and one-off QR encoding serve background/setup; preserve algorithms and output.
foreach(home_setup_component espressif__cjson espressif__qrcode)
    idf_component_get_property(home_setup_lib ${home_setup_component} COMPONENT_LIB)
    target_compile_options(${home_setup_lib} PRIVATE -Os)
endforeach()

# 中文：后台BLE主机与Wi-Fi认证协议按体积优化，给设备原生画板留出原4MiB槽内空间；
# 不改预编译无线控制器、HID按键解析、触摸、阅读排版或面板波形。
# English: Size-optimize background BLE-host and Wi-Fi authentication code for the original
# 4MiB slot, retaining radio binaries, HID decoding, touch, reader layout and panel waveforms.
foreach(pico_wireless_protocol bt wpa_supplicant)
    idf_component_get_property(pico_wireless_protocol_lib ${pico_wireless_protocol} COMPONENT_LIB)
    target_compile_options(${pico_wireless_protocol_lib} PRIVATE -Os)
endforeach()

# 双机实验版库按体积优化；阅读、字体、触摸与屏幕热路径显式保留O2。
# The dual profile shrinks libraries while explicitly retaining O2 on reader/font/touch/display hot paths.
target_compile_definitions(${COMPONENT_LIB} PRIVATE PICO_DUAL_READER=1)
set_source_files_properties(
    "app/app_loop.c" "app/app_touch_buffer.c" "display.c"
    "system/pico_reader.c" "system/pico_draw.c" "system/pico_ink.c"
    "apps/app_book.c" "font/ttf_font.c" "ui/ui_kit.c" "ui/ui_image.c" "ui/ui_gesture.c"
    "book/book_layout.c" "book/book_source.c" "book/book_txt.c" "book/book_epub.c"
    "book/book_image.c" "book/vendor/tjpgd.c" "book/html_text.c"
    PROPERTIES COMPILE_OPTIONS "-O2")
foreach(dual_hot_component read_pico cst836u continuous_du epdiy e0470_epaper_waveform)
    idf_component_get_property(dual_hot_lib ${dual_hot_component} COMPONENT_LIB)
    target_compile_options(${dual_hot_lib} PRIVATE -O2)
endforeach()
