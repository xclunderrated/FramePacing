add_executable(d9spinner d9spinner.cpp)
target_link_libraries(d9spinner PRIVATE pacer_shared d3d9.lib user32.lib)
