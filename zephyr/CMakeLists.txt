zephyr_library()

zephyr_include_directories(../include ../src)

zephyr_library_sources(
    ../src/iolink_core.c
    ../src/crc.c
    ../src/dll.c
    ../src/isdu.c
    ../src/events.c
    ../src/data_storage.c
    ../src/params.c
    ../src/device_info.c
    ../src/phy_generic.c
    ../src/platform.c
    ../src/platform/zephyr/time_utils.c
)

zephyr_library_sources_ifdef(CONFIG_IOLINK_PHY_VIRTUAL ../src/phy_virtual.c)
zephyr_library_sources_ifdef(CONFIG_IOLINK_PHY_UART    ../src/platform/zephyr/phy_uart.c)

