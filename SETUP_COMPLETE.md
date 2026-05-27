# Project Setup Checklist

## ✅ Completed

- [x] Root CMakeLists.txt created with:
  - C++20 standard configuration
  - Caliper dependency (2.14+) finding
  - hwloc dependency finding
  - src/ and examples/ subdirectories
  
- [x] src/CMakeLists.txt created with:
  - INTERFACE library target for header-only calimitos
  - Proper include directories
  - Caliper and hwloc linking
  - PIC flags for compatibility

- [x] examples/CMakeLists.txt created with:
  - Sequential example (mm_seq_cali)
  - OpenMP example (mm_omp_cali)
  - MPI example (mm_mpi_cali)
  - Automatic dependency finding

- [x] Documentation:
  - README.md - Project overview and quick start
  - BUILD.md - Detailed build instructions
  - This checklist file

## 📋 Next Steps

### Before Building

1. **Build Your Caliper Fork**
   ```bash
   cd ../maruis_caliper
   mkdir -p build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./install ..
   cmake --build . --parallel
   cmake --install .
   ```

2. **Verify Dependencies**
   ```bash
   # Check hwloc
   brew install hwloc
   
   # Optional: Check for MPI and OpenMP
   brew install open-mpi  # if needed
   ```

3. **Configure mitos2**
   ```bash
   cd /path/to/mitos2
   mkdir -p build && cd build
   cmake -DCMAKE_PREFIX_PATH=../../maruis_caliper/build/install \
         -DCMAKE_BUILD_TYPE=Release \
         ..
   ```

4. **Build**
   ```bash
   cmake --build . --parallel
   ```

5. **Test**
   ```bash
   ./mm_seq_cali
   # Check output in calimitos_<timestamp>/ directory
   ```

### Optional Enhancements

- Create `src/calimitos.cpp` if you want to compile calimitos into a shared library
- Add installation targets to CMakeLists.txt (install rules)
- Create unit tests with CTest
- Add documentation with Doxygen

## 📝 About the Library Structure

**calimitos is a header-only library** - this is optimal because:
- No separate compilation needed for the library itself
- Applications directly compile against calimitos.h
- CMake INTERFACE target handles all dependencies automatically
- Consumers get all optimizations (inlining, etc.)

The examples link against:
1. The calimitos INTERFACE target (which brings in caliper + hwloc)
2. Additional libraries as needed (OpenMP for omp example, MPI for mpi example)

## 🔧 Troubleshooting

If CMake configuration fails:

```bash
# Check if caliper is found
cmake -DCMAKE_PREFIX_PATH=/path/to/caliper/install ..

# Set hwloc explicitly if needed
cmake -DCMAKE_PREFIX_PATH=/path/to/caliper/install:/usr/local/opt/hwloc ..

# Verify Caliper installation
ls /path/to/caliper/install/share/cmake/caliper/
```

## 📚 Key Files

| File | Purpose |
|------|---------|
| CMakeLists.txt | Root project configuration |
| src/CMakeLists.txt | calimitos interface library |
| src/calimitos.h | Header-only library (2000+ lines) |
| src/cali_to_csv.py | Post-processing for traces |
| examples/CMakeLists.txt | Example applications |
| BUILD.md | Build instructions |
| README.md | Project overview |
