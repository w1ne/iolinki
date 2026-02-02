# Mock Zephyr Configuration for Verification
message(STATUS "=== MOCKING ZEPHYR BUILD SYSTEM ===")

# Define Zephyr macros used in our project
function(zephyr_library)
    add_library(zephyr_interface INTERFACE)
    # We create an alias to satisfy target_link_libraries if used
    if(NOT TARGET drivers__sensor__iolinki)
        add_library(drivers__sensor__iolinki ALIAS zephyr_interface)
    endif()
endfunction()

function(zephyr_library_sources)
    foreach(src ${ARGN})
        # Verify file existence logic...
        # Note: zephyr_library_sources in the module is called from the module dir.
        # So CMAKE_CURRENT_SOURCE_DIR is the module dir.
        if(IS_ABSOLUTE "${src}")
            set(abs_src "${src}")
        else()
            set(abs_src "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        endif()
        
        if(NOT EXISTS "${abs_src}")
             message(FATAL_ERROR "Zephyr Source Validation Failed: File not found: ${abs_src}")
        else()
             message(STATUS "Verified source exists: ${src}")
        endif()
    endforeach()
endfunction()

function(zephyr_library_include_directories)
endfunction()

# Define Kernel stub variables
set(ZEPHYR_BASE ${CMAKE_CURRENT_LIST_DIR})

# Create Mock App Target (so zephyr_app can append sources to it)
if(NOT TARGET app)
    # Create a dummy file to satisfy add_executable
    file(WRITE ${CMAKE_BINARY_DIR}/mock_kernel_entry.c "void _mock_entry(void){}")
    add_executable(app ${CMAKE_BINARY_DIR}/mock_kernel_entry.c)
endif()
