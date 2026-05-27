# mitos2 - Memory-Access Sampling Framework

A modern C++20 implementation of Mitos memory-access sampling using Caliper as the backend. This framework enables efficient hardware-based sampling of memory accesses across various compute patterns.

## Overview

**calimitos** (Caliper Mitos) is a header-only library that wraps Caliper's sampling capabilities to provide:

- **Multiple sampling backends**: PEBS, IBS Op, IBS Fetch (with automatic detection)
- **Hardware monitoring**: Uses libpfm4 for event-based sampling
- **MPI support**: Transparent MPI-aware sampling with rank tagging
- **Memory-centric analysis**: Structured output compatible with MemAxes analysis tools
- **Flexible configuration**: Combine setter-based config with Caliper option strings

## Features

- **Header-only design**: Easy integration, no separate library compilation needed
- **Zero-copy sampling**: Efficient in-kernel event collection
- **Automatic source tracking**: Captures source files involved in sampled code regions
- **Hardware topology aware**: Exports hwloc XML for architecture details
- **Post-processing**: Includes Python script to convert Caliper traces to MemAxes CSV format

## Quick Start

See [BUILD.md](BUILD.md) for detailed build and run instructions.

```bash
# Build the project
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/path/to/caliper ..
cmake --build .

# Run a sequential example
./mm_seq_cali

# Run with MPI
mpirun -np 4 ./mm_mpi_cali
```

## Project Structure

- `src/calimitos.h` - Main header-only library
- `src/cali_to_csv.py` - Post-processing utility
- `examples/` - Sample applications (sequential, OpenMP, MPI matrix multiply)

## Configuration Examples

```cpp
// Using setter functions
Mitos_set_backend(backend_ibs_op);
Mitos_set_sample_event_period(4000);
Mitos_enable_mpi(true);
Mitos_begin_sampler();

// Using option string
Mitos_begin_sampler("backend=pebs,sample.period=8000");

// Custom handler for additional attributes
Mitos_set_handler_fn(my_callback, nullptr);
```

## Output

Results are organized as:

```
calimitos_<timestamp>/
├── data/
│   ├── trace.cali           # Raw Caliper trace
│   └── samples.csv          # MemAxes-format CSV
├── hardware.xml             # hwloc topology
└── src/                      # Source files involved in sampling
```

## References

- **Caliper**: https://github.com/LLNL/Caliper
- **libpfm4**: Performance monitoring library
- **hwloc**: Hardware locality library

## Author

Marius Albrecht, 2026 (Bachelor's thesis)
