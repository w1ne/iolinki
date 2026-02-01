project(simple_device)

add_executable(simple_device src/main.c)

# Link against iolinki
target_link_libraries(simple_device PRIVATE iolinki)
