# mitos2 - Setup & Build Guide

## Project Structure

The mitos2 project is now organized as follows:

```
mitos2/
├── CMakeLists.txt          # Root configuration
├── src/
│   ├── CMakeLists.txt      # Library configuration
│   ├── calimitos.h         # Header-only sampling library
│   └── cali_to_csv.py      # Post-processing script
└── examples/
    ├── CMakeLists.txt      # Example applications
    ├── mm_seq_cali.cpp     # Sequential matrix multiply
    ├── mm_omp_cali.cpp     # OpenMP matrix multiply
    └── mm_mpi_cali.cpp     # MPI matrix multiply
```

## Dependencies

### Required
- **Caliper**: Your fork at `repos/maruis_caliper`
- **hwloc**: System library for hardware topology (install via: `brew install hwloc`)
- **C++20 compiler**: gcc/clang with C++20 support

### Optional
- **OpenMP**: For `mm_omp_cali` example
- **MPI**: For `mm_mpi_cali` example
- **Python 3**: For post-processing with `cali_to_csv.py`

## Build Instructions

### Step 1: Build Caliper (Your Fork)

```bash
cd ../maruis_caliper
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./install ..
cmake --build . --parallel
cmake --install .
```

### Step 2: Build mitos2

```bash
cd ../../mitos2
mkdir -p build
cd build

# Configure with your Caliper installation
cmake -DCMAKE_PREFIX_PATH=../../maruis_caliper/build/install \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# Build
cmake --build . --parallel
```

### Step 3: Run Examples

```bash
# Sequential version
./mm_seq_cali

# OpenMP version (requires OpenMP)
./mm_omp_cali

# MPI version (requires MPI)
mpirun -np 4 ./mm_mpi_cali
```

## About the Shared Library

**Important Note**: `calimitos` is a **header-only library**. This means:

- All code is in `src/calimitos.h`
- No separate `.so` compilation is needed for the header itself
- Applications link directly to Caliper and hwloc dependencies
- The CMake setup uses an INTERFACE library target, which properly handles the header-only nature

If you need to create an actual `.so` file (for binary distribution or other reasons), you would:

1. Create a `src/calimitos.cpp` that includes and exports calimitos functionality
2. Uncomment the shared library target in `src/CMakeLists.txt`
3. Recompile

The current setup is optimal for development and ensures correct header-only semantics.

## Customization

### Building Only Specific Examples

Edit `examples/CMakeLists.txt` to conditionally add examples:

```cmake
if(ENABLE_OPENMP)
    add_executable(mm_omp_cali mm_omp_cali.cpp)
    # ...
endif()
```

Then build with:
```bash
cmake -DENABLE_OPENMP=ON -DENABLE_MPI=ON ..
```

### Setting Environment Variables for calimitos

When running compiled examples, you can set:

```bash
export CALIMITOS_CALI_TO_CSV=/path/to/cali_to_csv.py
./mm_seq_cali
```

This tells calimitos where to find the post-processing script.

## Next Steps

1. Build Caliper (if not already built)
2. Run `cmake --build .` in the build directory
3. Test with `./mm_seq_cali` to verify everything works
4. Debug any linking or compilation issues

## Troubleshooting

### CMake can't find Caliper
- Ensure `CMAKE_PREFIX_PATH` points to your Caliper installation
- Or set `-Dcaliper_DIR=/path/to/caliper/cmake`

### hwloc not found
- Install: `brew install hwloc`
- Or set: `-DCMAKE_PREFIX_PATH=/usr/local/opt/hwloc`

### MPI/OpenMP examples fail to compile
- This is expected if you don't have MPI/OpenMP installed
- Remove those examples from `examples/CMakeLists.txt` if not needed
