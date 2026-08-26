add_executable(pacer-svc service.cpp service_core.cpp service_core.h)
target_link_libraries(pacer-svc PRIVATE pacer_shared)
