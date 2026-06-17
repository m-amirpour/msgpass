cmake_minimum_required(VERSION 3.16)
project(msgpass VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Warnings — we treat them as errors in production builds.
if(MSVC)
    add_compile_options(/W4 /WX /wd4996 /wd4100)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    if(NOT WIN32)
        add_compile_definitions(_POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
    endif()
endif()

if(WIN32)
    add_compile_definitions(
            _WIN32_WINNT=0x0A00
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
    )
endif()

find_package(Threads REQUIRED)

include_directories(include src)

# Platform-specific sources.
if(WIN32)
    set(OS_SOURCES
            src/os/os_socket_win32.c
            src/os/os_thread_win32.c
            src/os/os_process_win32.c
            src/os/os_time.c
    )
else()
    set(OS_SOURCES
            src/os/os_socket_posix.c
            src/os/os_thread_posix.c
            src/os/os_process_posix.c
            src/os/os_time.c
            src/mp_client_state.h
    )
endif()

# Shared library of all core logic.
add_library(msgpass_core STATIC
        src/mp_log.c
        src/mp_buf.c
        src/mp_protocol.c
        src/mp_queue.c
        src/mp_executor.c
        src/mp_client_state.c
        src/mp_dispatcher.c
        src/mp_server_st.c
        src/mp_server_mt.c
        ${OS_SOURCES}
)

target_include_directories(msgpass_core PUBLIC include src)
target_link_libraries(msgpass_core PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(msgpass_core PUBLIC ws2_32)
endif()

add_executable(msgpass_server src/mp_server.c)
target_link_libraries(msgpass_server PRIVATE msgpass_core)

add_executable(msgpass_client src/mp_client.c)
target_link_libraries(msgpass_client PRIVATE msgpass_core)

# Tests
if(NOT WIN32 OR MSVC)
    enable_testing()

    foreach(test_name protocol queue buf executor)
        add_executable(test_${test_name} tests/test_${test_name}.c)
        target_include_directories(test_${test_name} PRIVATE tests)
        target_link_libraries(test_${test_name} PRIVATE msgpass_core)
        add_test(NAME ${test_name} COMMAND test_${test_name})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endforeach()
endif()