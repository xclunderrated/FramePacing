add_executable(pacer-inject injector.cpp)
target_link_libraries(pacer-inject PRIVATE pacer_shared)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set_target_properties(pacer-inject PROPERTIES OUTPUT_NAME "pacer-inject86")
else()
    set_target_properties(pacer-inject PROPERTIES OUTPUT_NAME "pacer-inject")
endif()
