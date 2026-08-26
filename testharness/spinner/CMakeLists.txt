add_executable(spinner spinner.cpp)
target_link_libraries(spinner PRIVATE pacer_shared d3d11.lib dxgi.lib user32.lib)
