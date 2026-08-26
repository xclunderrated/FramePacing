add_executable(pacertest pacertest.cpp ${CMAKE_SOURCE_DIR}/core/pacing.cpp)
target_include_directories(pacertest PRIVATE ${CMAKE_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR})
target_link_libraries(pacertest PRIVATE pacer_shared winmm.lib)
