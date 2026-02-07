zephyr_library()

zephyr_include_directories(../include)

zephyr_library_sources(
    ../src/iolink_core.c
    ../src/phy_virtual.c
    ../src/crc.c
    ../src/dll.c
    ../src/isdu.c
    ../src/events.c
    ../src/data_storage.c
    ../src/params.c
    ../src/device_info.c
    ../src/platform.c
    ../src/platform/zephyr/time_utils.c
)
