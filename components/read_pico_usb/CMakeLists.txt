# SPDX-License-Identifier: Apache-2.0
idf_component_register(SRCS "read_pico_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES read_pico sdmmc esp_hw_support espressif__esp_tinyusb)
# Bound one core event pass so a busy host cannot starve a requested cooperative
# shutdown. No vendor sources are patched and all block callbacks finish first.
idf_component_get_property(pico_tinyusb_core espressif__tinyusb COMPONENT_LIB)
target_compile_definitions(${pico_tinyusb_core} PRIVATE CFG_TUD_TASK_EVENTS_PER_RUN=16)
