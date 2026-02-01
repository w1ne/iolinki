find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)

include_directories(${CMOCKA_INCLUDE_DIRS})

macro(add_iolink_test name source)
    add_executable(${name} ${source} test_helpers.c)
    target_link_libraries(${name} iolinki ${CMOCKA_LIBRARIES})
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Initial test: basic initialization
add_iolink_test(test_init test_init.c)
