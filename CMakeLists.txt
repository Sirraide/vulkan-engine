cmake_minimum_required(VERSION 3.21)
project(vulkan-template)

## Global flags
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
add_compile_options(-fdiagnostics-color=always)

## Library source files
file(GLOB LIB lib/*.cc lib/*.hh)
file(GLOB IMGUI
        imgui/*.cpp imgui/*.h
        imgui/backends/imgui_impl_vulkan.h imgui/backends/imgui_impl_vulkan.cpp
        imgui/backends/imgui_impl_glfw.h imgui/backends/imgui_impl_glfw.cpp)

set_source_files_properties(
        ${IMGUI}
        PROPERTIES
        COMPILE_FLAGS "-Wno-conversion -Wno-sign-conversion -Wno-implicit-int-float-conversion")

## Application source files
file(GLOB SRC src/*.cc src/*.hh)

## Compiler and linker options
add_library(options INTERFACE)
target_include_directories(options INTERFACE imgui)
target_compile_options(options INTERFACE
        -Wall -Wextra -Wundef -Wconversion -Werror=return-type
        -fno-rtti)
if (CMAKE_BUILD_TYPE STREQUAL Release)
    target_compile_options(options INTERFACE -O3 -DNDEBUG)
else ()
    target_compile_options(options INTERFACE -O0 -ggdb -fno-omit-frame-pointer)
    target_link_options(options INTERFACE -O0 -ggdb -Wl,--export-dynamic -fno-omit-frame-pointer)
endif ()
target_link_libraries(options INTERFACE fmt glfw vulkan dl pthread X11 Xxf86vm Xrandr Xi stdc++_libbacktrace)

## Executables and libraries
add_library(vulkan-engine SHARED ${LIB} ${IMGUI})

## Apply the options
target_link_libraries(vulkan-engine PRIVATE options)

