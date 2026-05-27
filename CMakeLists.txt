cmake_minimum_required(VERSION 3.12)

project(mitos2 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fPIC")

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
message("cmake moduule path ${CMAKE_MODULE_PATH}")


# Find Caliper dependency
# Assumes caliper is installed or available via CMAKE_PREFIX_PATH
#find_package(caliper 2.14 REQUIRED)
find_package(caliper REQUIRED)

# Find hwloc (required by calimitos)
find_package(hwloc REQUIRED)

find_package(Python REQUIRED)

find_package(MPI REQUIRED)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(examples)