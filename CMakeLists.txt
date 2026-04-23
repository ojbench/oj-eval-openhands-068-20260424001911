cmake_minimum_required(VERSION 3.10)
project(TLSFAllocator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(code main.cpp allocator.cpp)

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Include directories
target_include_directories(code PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

