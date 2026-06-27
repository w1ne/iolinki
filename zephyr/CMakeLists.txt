# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later

if(CONFIG_IOLINKI)

zephyr_library()

# Public API headers (iolinki/*) and internal headers (platform/zephyr/*) are
# exported globally so that both the library and the application can include
# them, e.g. <iolinki/iolink.h> and <platform/zephyr/phy_uart.h>.
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../src)

zephyr_library_sources(
    ../src/iolink_core.c
    ../src/crc.c
    ../src/frame.c
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

# Map Kconfig tunables onto the core's #ifndef-guarded config.h macros. These
# are applied globally so the values are consistent across the library and any
# application translation units that include <iolinki/config.h>.
zephyr_compile_definitions(
    IOLINK_ISDU_BUFFER_SIZE=${CONFIG_IOLINK_ISDU_BUFFER_SIZE}
    IOLINK_EVENT_QUEUE_SIZE=${CONFIG_IOLINK_EVENT_QUEUE_SIZE}
    IOLINK_PD_IN_MAX_SIZE=${CONFIG_IOLINK_PD_IN_MAX_SIZE}
    IOLINK_PD_OUT_MAX_SIZE=${CONFIG_IOLINK_PD_OUT_MAX_SIZE}
)

endif() # CONFIG_IOLINKI
