add_executable(pacer-stats statsreader.cpp)
target_link_libraries(pacer-stats PRIVATE pacer_shared)
