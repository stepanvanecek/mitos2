# calimitos library
# Since calimitos.h is a header-only library, we create an INTERFACE library
# that properly exposes its dependencies (caliper and hwloc)

add_library(calimitos INTERFACE)

# Add the include directory for calimitos.h
target_include_directories(calimitos INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Link caliper - handle both modern (namespaced) and spack targets
# Spack's caliper may provide plain "caliper" target, not caliper::caliper
if(TARGET caliper::caliper)
    target_link_libraries(calimitos INTERFACE caliper::caliper)
elseif(TARGET caliper)
    target_link_libraries(calimitos INTERFACE caliper)
else()
    # Fallback to classic variables
    if(caliper_INCLUDE_DIR)
        target_include_directories(calimitos INTERFACE ${caliper_INCLUDE_DIR})
    endif()
    if(caliper_LIBRARIES)
        target_link_libraries(calimitos INTERFACE ${caliper_LIBRARIES})
    endif()
    message(WARNING "Caliper targets not found; using classic variables.")
endif()

# Link required dependencies through the interface
target_link_libraries(calimitos INTERFACE
    hwloc::hwloc
)

# Optionally compile flags for consumers
target_compile_options(calimitos INTERFACE -fPIC)

# For those who want to build an actual .so library from the header-only code,
# we provide an optional calimitos_shared target. This is useful for testing
# and for cases where you want to ensure consistent compilation.
# Note: This is optional and most users should just use the INTERFACE target above.

# Uncomment the following lines if you want to build a shared library:
# add_library(calimitos_shared SHARED calimitos.cpp)
# target_link_libraries(calimitos_shared PUBLIC calimitos)
# target_compile_options(calimitos_shared PRIVATE -fPIC -Wall)

# Make the library available to the project
set(calimitos_FOUND TRUE CACHE INTERNAL "calimitos found")

install(PROGRAMS cali_to_csv.py DESTINATION .)