set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(SRCS "main.c" "board.c" "tx_guard.c" "rx_guard.c"
    "${ROOT}/examples/reference_device/reference_device.c"
    "${ROOT}/src/phy_l6362a.c" "${ROOT}/src/crc.c" "${ROOT}/src/frame.c"
    "${ROOT}/src/dll.c" "${ROOT}/src/isdu.c" "${ROOT}/src/events.c"
    "${ROOT}/src/params.c" "${ROOT}/src/data_storage.c"
    "${ROOT}/src/device_info.c" "${ROOT}/src/device.c"
    INCLUDE_DIRS "${ROOT}/include" "${ROOT}/examples/reference_device"
    REQUIRES driver esp_timer)
