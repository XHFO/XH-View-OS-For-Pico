# 蓝牙低功耗翻页器与有界 HID 报告解析。/ BLE page turner and bounded HID report parser.
idf_component_register(SRCS "read_pico_ble_pager.c" "read_pico_dual_ble.c" "pager_hid.c"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_timer)

# 中文：只压缩后台连接管理；HID按键解码保持项目优化档。
# English: Size-optimize background connection management only; retain the HID decoder optimization profile.
set_source_files_properties("read_pico_ble_pager.c" PROPERTIES COMPILE_OPTIONS "-Os")
