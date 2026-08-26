add_executable(vkspinner vkspinner.cpp)
# Runtime does LoadLibrary(vulkan-1) so no SDK import lib needed.
target_link_libraries(vkspinner PRIVATE user32.lib)
