find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CMOCKA cmocka)
endif()

if(CMOCKA_FOUND)
    message(STATUS "CMocka found, enabling unit tests")
    include_directories(${CMOCKA_INCLUDE_DIRS})

    macro(add_iolink_test name source)
        add_executable(${name} ${source} test_helpers.c)
        target_link_libraries(${name} iolinki ${CMOCKA_LIBRARIES})
        add_test(NAME ${name} COMMAND ${name})
    endmacro()

    # Initial test: basic initialization
    add_iolink_test(test_init test_init.c)
else()
    message(WARNING "CMocka not found, unit tests will be skipped. Install libcmocka-dev to enable them.")
endif()
