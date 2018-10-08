add_executable(dataGenerator dataGenerator.cpp decomp.cpp)
target_link_libraries(dataGenerator adios2::adios2 MPI::MPI_C)
