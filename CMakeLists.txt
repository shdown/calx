cmake_minimum_required (VERSION 3.0.0)
project (calx C)

set (CMAKE_C_STANDARD 99)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wcast-qual -Wshadow -Winit-self")

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-crossjumping")
endif ()

function (calx_add_stringified_file dest src target_name)
    add_custom_command (
        OUTPUT "${dest}"
        COMMAND "${PROJECT_SOURCE_DIR}/cstringify.sh" ARGS "${CMAKE_CURRENT_SOURCE_DIR}/${src}" "${dest}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        MAIN_DEPENDENCY "${src}"
        VERBATIM)
    add_custom_target ("${target_name}" DEPENDS "${dest}")
endfunction ()

calx_add_stringified_file ("stdlib_calx.generated.inc" "stdlib.calx" "stringify-stdlib")

file (GLOB sources "*.c" "libdeci/*.c" "libdeci-kara/*.c" "libdeci-ntt/*.c" "libdeci-newt/*.c")
add_executable (calx ${sources})
add_dependencies (calx "stringify-stdlib")
include_directories ("libdeci")
target_link_libraries (calx PUBLIC readline)
target_compile_definitions (calx PUBLIC
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
    -DCALX_HANDLE_SEGV=0
)

include (GNUInstallDirs)
install (TARGETS calx DESTINATION ${CMAKE_INSTALL_BINDIR})
