add_executable(dataGenerator dataGenerator.cpp)
target_link_libraries(dataGenerator adios2::adios2 MPI::MPI_C)
