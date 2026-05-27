# Example applications using calimitos

# Sequential matrix multiply with Caliper sampling
add_executable(mm_seq_cali mm_seq_cali.cpp)
target_link_libraries(mm_seq_cali PRIVATE calimitos)
target_compile_options(mm_seq_cali PRIVATE -Wall)

# OpenMP matrix multiply with Caliper sampling
add_executable(mm_omp_cali mm_omp_cali.cpp)
target_link_libraries(mm_omp_cali PRIVATE calimitos)
target_compile_options(mm_omp_cali PRIVATE -Wall)
find_package(OpenMP REQUIRED)
target_link_libraries(mm_omp_cali PRIVATE OpenMP::OpenMP_CXX)

# MPI matrix multiply with Caliper sampling
find_package(MPI REQUIRED)
include_directories(${MPI_INCLUDE_PATH})
add_executable(mm_mpi_cali mm_mpi_cali.cpp)
target_link_libraries(mm_mpi_cali PRIVATE calimitos ${MPI_LIBRARIES})
target_compile_options(mm_mpi_cali PRIVATE -Wall)
