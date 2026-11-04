cmake_minimum_required(VERSION 3.20)

# Silence timestamp policy warning for CMake 3.24+
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(VoltKV
    VERSION 0.1.0
    DESCRIPTION "High-throughput in-memory sharded key-value store"
    LANGUAGES CXX
)

# -----------------------------------------------------------------------------
# C++ Standard Configuration
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Compiler Flags & Sanitizers
# -----------------------------------------------------------------------------
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wformat=2
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g3 -O0)
endif()

# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously!")
endif()

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

# -----------------------------------------------------------------------------
# Core Library: voltkv_lib
# -----------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*/*.cpp")

# If no module files exist yet, fallback to a starter dummy so add_library never breaks
if(NOT SOURCES)
    set(SOURCES src/storage/shard.cpp)
endif()

add_library(voltkv_lib STATIC ${SOURCES})

target_include_directories(voltkv_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(voltkv_lib PUBLIC pthread)

# -----------------------------------------------------------------------------
# Main Executable: voltkv_server
# -----------------------------------------------------------------------------
add_executable(voltkv_server src/main.cpp)
target_link_libraries(voltkv_server PRIVATE voltkv_lib)

# -----------------------------------------------------------------------------
# Unit Testing (GoogleTest)
# -----------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()