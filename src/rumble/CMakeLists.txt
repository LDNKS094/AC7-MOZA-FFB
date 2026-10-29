cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(AC7RumbleTrace LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use the x64 MSVC toolchain on Windows.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Shared, vendored dependency; no sibling checkout required.
set(MINHOOK "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/minhook")
add_library(minhook STATIC
    "${MINHOOK}/src/buffer.c" "${MINHOOK}/src/hook.c"
    "${MINHOOK}/src/trampoline.c" "${MINHOOK}/src/hde/hde64.c")
target_include_directories(minhook PUBLIC "${MINHOOK}/include")
add_library(AC7RumbleTrace SHARED observer.cpp preview.cpp native_input.cpp raw_channel.cpp playback.cpp settings.cpp)
target_link_libraries(AC7RumbleTrace PRIVATE minhook)
target_compile_options(AC7RumbleTrace PRIVATE /W4 /O2)
set_target_properties(AC7RumbleTrace PROPERTIES PREFIX "" SUFFIX ".asi")
