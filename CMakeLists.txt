cmake_minimum_required(VERSION 3.21)
project(vulkan-template)

## Global flags
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
add_compile_options(-fdiagnostics-color=always)

## Source files
file(GLOB LIB lib/*.cc lib/*.hh)
file(GLOB SRC src/*.cc src/*.hh)

## Compiler and linker options
add_library(options INTERFACE)
target_compile_options(options INTERFACE
        -Wall -Wextra -Wundef -Wconversion -Werror=return-type
        -fno-exceptions -fno-rtti
        -Werror=nonnull -Werror=nullability
        -Werror=nullable-to-nonnull-conversion -Wnullability-completeness)
if (CMAKE_BUILD_TYPE STREQUAL Release)
    target_compile_options(options INTERFACE -O3 -DNDEBUG)
else ()
    target_compile_options(options INTERFACE -O0 -ggdb)
endif ()
target_link_libraries(options INTERFACE fmt glfw vulkan dl pthread X11 Xxf86vm Xrandr Xi)

## Executables and libraries
add_executable(vulkan-template ${SRC})
add_library(vulkan-lib SHARED ${LIB})

## Apply the options
target_link_libraries(vulkan-lib PRIVATE options)
target_link_libraries(vulkan-template PRIVATE options vulkan-lib)

