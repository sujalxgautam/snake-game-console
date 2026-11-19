# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.15)
project(snake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put the binary next to the sources so `./snake` works the same way as the
# g++ one-liner documented in the README.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Snake uses std::this_thread::sleep_for for its fixed-timestep loop.
find_package(Threads REQUIRED)
add_executable(snake main.cpp)
target_link_libraries(snake PRIVATE Threads::Threads)